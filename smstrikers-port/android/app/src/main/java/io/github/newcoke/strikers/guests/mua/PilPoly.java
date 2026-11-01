package io.github.newcoke.strikers.guests.mua;

import java.util.Arrays;

/**
 * Pillow's ImageDraw.polygon(fill) on an 8-bit image (hero_icons.py's silhouettes), as its C code does it: the
 * vertices rounded to whole pixels, an edge table, a scan line per row with float32 intersections, spans from
 * ROUND_UP(left) to ROUND_DOWN(right), horizontal edges as lines.
 */
final class PilPoly {
    private PilPoly() {}

    static final class Edge {
        int d, x0, y0, xmin, ymin, xmax, ymax;
        float dx;
    }

    /** C's roundf: halves away from zero. */
    static float roundf(float f) {
        return (float) (f >= 0 ? Math.floor(f + 0.5) : -Math.floor(-f + 0.5));
    }

    static int roundUp(float f) {
        return (int) (f >= 0.0 ? Math.floor((float) (f + 0.5f)) : -Math.floor((float) (Math.abs(f) + 0.5f)));
    }

    static int roundDown(float f) {
        return (int) (f >= 0.0 ? Math.ceil((float) (f - 0.5f)) : -Math.ceil((float) (Math.abs(f) - 0.5f)));
    }

    /** The vertex coordinate as _imaging.c hands it on (double -> int: C's cast, toward zero; measured on Pillow 13). */
    static int coord(double v) {
        return (int) v;
    }

    private static Edge edge(int x0, int y0, int x1, int y1) {
        final Edge e = new Edge();
        if (x0 <= x1) {
            e.xmin = x0;
            e.xmax = x1;
        } else {
            e.xmin = x1;
            e.xmax = x0;
        }
        if (y0 <= y1) {
            e.ymin = y0;
            e.ymax = y1;
        } else {
            e.ymin = y1;
            e.ymax = y0;
        }
        if (y0 == y1) {
            e.d = 0;
            e.dx = 0.0f;
        } else {
            e.dx = ((float) (x1 - x0)) / (float) (y1 - y0);
            e.d = y0 == e.ymin ? 1 : -1;
        }
        e.x0 = x0;
        e.y0 = y0;
        return e;
    }

    private static void hline(byte[] img, int w, int h, int x0, int y0, int x1, int ink) {
        if (y0 < 0 || y0 >= h) return;
        if (x0 < 0) x0 = 0;
        else if (x0 >= w) return;
        if (x1 < 0) return;
        else if (x1 >= w) x1 = w - 1;
        if (x0 <= x1) Arrays.fill(img, y0 * w + x0, y0 * w + x1 + 1, (byte) ink);
    }

    /** A filled polygon (xy: x0 y0 x1 y1 ... as doubles) on img (w x h, one byte a pixel). */
    static void fill(byte[] img, int w, int h, double[] xy, int ink) {
        final int count = xy.length / 2;
        final int[] p = new int[count * 2];
        for (int i = 0; i < p.length; i++) p[i] = coord(xy[i]);
        final Edge[] e = new Edge[count];
        int n = 0, i;
        for (i = 0; i < count - 1; i++) {
            final int x0 = p[i * 2], y0 = p[i * 2 + 1], x1 = p[i * 2 + 2], y1 = p[i * 2 + 3];
            if (y0 == y1 && i != 0 && y0 == p[i * 2 - 1]) {
                final Edge last = e[n - 1];
                if (x1 > x0 && x0 > p[i * 2 - 2]) {
                    last.xmax = x1;
                    continue;
                } else if (x1 < x0 && x0 < p[i * 2 - 2]) {
                    last.xmin = x1;
                    continue;
                }
            }
            e[n++] = edge(x0, y0, x1, y1);
        }
        if (p[i * 2] != p[0] || p[i * 2 + 1] != p[1]) e[n++] = edge(p[i * 2], p[i * 2 + 1], p[0], p[1]);
        generic(img, w, h, e, n, ink);
    }

    private static void generic(byte[] img, int w, int h, Edge[] e, int n, int ink) {
        if (n <= 0) return;
        int ymin = h - 1, ymax = 0;
        final Edge[] table = new Edge[n];
        int ec = 0;
        for (int i = 0; i < n; i++) {
            if (ymin > e[i].ymin) ymin = e[i].ymin;
            if (ymax < e[i].ymax) ymax = e[i].ymax;
            if (e[i].ymin == e[i].ymax) {
                hline(img, w, h, e[i].xmin, e[i].ymin, e[i].xmax, ink);
                continue;
            }
            table[ec++] = e[i];
        }
        if (ymin < 0) ymin = 0;
        if (ymax > h) ymax = h;
        final float[] xx = new float[ec * 2];
        for (; ymin <= ymax; ymin++) {
            int j = 0;
            for (int i = 0; i < ec; i++) {
                final Edge cur = table[i];
                if (ymin >= cur.ymin && ymin <= cur.ymax) {
                    xx[j++] = (float) (ymin - cur.y0) * cur.dx + (float) cur.x0;
                    if (ymin == cur.ymax && ymin < ymax) {
                        xx[j] = xx[j - 1];
                        j++;
                    } else if ((ymin == cur.ymin || ymin == cur.ymax) && cur.dx != 0) {
                        // connect discontiguous corners (with an earlier edge meeting it on this row)
                        for (int k = 0; k < i; k++) {
                            final Edge other = table[k];
                            if ((ymin != other.ymin && ymin != other.ymax) || other.dx == 0) continue;
                            if (roundf(xx[j - 1]) == roundf((float) (ymin - other.y0) * other.dx + (float) other.x0)) {
                                final int offset = ymin == cur.ymax ? -1 : 1;
                                final float adj = (float) (ymin + offset - cur.y0) * cur.dx + (float) cur.x0;
                                if (ymin + offset >= other.ymin && ymin + offset <= other.ymax) {
                                    final float adjOther = (float) (ymin + offset - other.y0) * other.dx + (float) other.x0;
                                    if (xx[j - 1] > adj + 1 && xx[j - 1] > adjOther + 1) {
                                        xx[j - 1] = roundf(Math.max(adj, adjOther)) + 1;
                                    } else if (xx[j - 1] < adj - 1 && xx[j - 1] < adjOther - 1) {
                                        xx[j - 1] = roundf(Math.min(adj, adjOther)) - 1;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            final float[] s = Arrays.copyOf(xx, j);
            Arrays.sort(s);
            for (int k = 1; k < j; k += 2) hline(img, w, h, roundUp(s[k - 1]), ymin, roundDown(s[k]), ink);
        }
    }
}
