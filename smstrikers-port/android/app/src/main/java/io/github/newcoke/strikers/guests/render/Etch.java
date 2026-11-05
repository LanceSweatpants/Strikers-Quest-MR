package io.github.newcoke.strikers.guests.render;

import java.util.Arrays;

/**
 * etchres.py: each guest's outline for the etching on your controllers when it's your captain (xr_fx.cpp make_etch,
 * xr_fx_mesh.frag sdGuest) and the home ground banners' emblem: the guest standing in its idle seen from
 * three-quarters in front (orthographic, fitted to its box), its silhouette (Blender Workbench there, a supersampled
 * rasteriser here: coverage over half, rendered 3x the cell), then a distance map per 128 x 128 cell (the same numbers,
 * float32 as numpy) of a 16 x 32 atlas (r135; etchres.py --grid 16 --cell 128, as the headset has it: guests/etch.r8,
 * 2048 x 4096 bytes; the cell = the guest's place in roster.txt, the franchise-only forms keeping their place empty),
 * 128 = the outline, below inside, above outside, a quarter of a cell either way over the range. A cell with no guest
 * stays 255 (far outside: nothing etched), as the game's own default. Cell i sits at column i % 16, row i / 16, where
 * the 16 x 16 atlas of 1.15-1.16 (r121-r134, 2048 x 2048) had it: that one is this one's top half (upgrade).
 */
public final class Etch {
    private Etch() {}

    /** r135: 16 columns x 32 rows of 128 x 128 (xr_fx.h kEtchCells = 512; 1.15-1.16: 16 x 16). */
    public static final int CELL = 128, COLS = 16, ROWS = 32, CELLS = COLS * ROWS, RES = 3 * CELL;
    static final double RANGE = 0.25, MARGIN = 0.1;
    /** The share of a pixel's (filtered) coverage that makes it inside (etchres.py: alpha over 0.5). */
    static final double THRESHOLD = 0.5;

    /** The empty atlas (every texel far outside). */
    public static byte[] atlas() {
        final byte[] a = new byte[COLS * CELL * ROWS * CELL];
        Arrays.fill(a, (byte) 255);
        return a;
    }

    /** One guest's cell into the atlas. */
    public static void put(byte[] atlas, int cell, byte[] v) {
        final int r = cell / COLS, c = cell % COLS;
        for (int y = 0; y < CELL; y++) System.arraycopy(v, y * CELL, atlas, (r * CELL + y) * COLS * CELL + c * CELL, CELL);
    }

    /**
     * An etch.r8 already there as this layout's atlas: one of this size as it is, a 16 x 16 one of 128 x 128 cells (1.15-1.16,
     * 2048 x 2048) as the top half (its cells where they were, the rest empty); null for anything else (1.14's 64 px cells).
     */
    public static byte[] upgrade(byte[] old) {
        final byte[] a = atlas();
        if (old == null || (old.length != a.length && old.length != COLS * CELL * 16 * CELL)) return null;
        System.arraycopy(old, 0, a, 0, old.length);
        return a;
    }

    /**
     * The silhouette (RES x RES, top row first) of a posed mesh (Strikers space, facing +X), seen as etchres.py's camera:
     * fitted to the box of the vertices its triangles use (Blender's OBJ import keeps no others), the coverage filtered as
     * Workbench's anti-aliasing does it (Blackman-Harris, 1 pixel round) and kept where over a half.
     */
    public static boolean[] silhouette(Mesh m, int samples) {
        return silhouette(m, samples, 0, 0);
    }

    /** shiftX / shiftY: the picture moved by that much of a pixel (right, down). */
    public static boolean[] silhouette(Mesh m, int samples, double shiftX, double shiftY) {
        final double[] lo = {1e30, 1e30, 1e30}, hi = {-1e30, -1e30, -1e30};
        final boolean[] used = new boolean[m.nv];
        for (int i = 0; i < m.nt * 3; i++) used[m.tri[i]] = true;
        for (int i = 0; i < m.nv; i++) {
            if (!used[i]) continue;
            for (int c = 0; c < 3; c++) {
                lo[c] = Math.min(lo[c], m.p[i * 3 + c]);
                hi[c] = Math.max(hi[c], m.p[i * 3 + c]);
            }
        }
        final boolean[] mask = new boolean[RES * RES];
        if (m.nt == 0) return mask;
        final double[] centre = new double[3];
        double size = 0;
        for (int c = 0; c < 3; c++) {
            centre[c] = (lo[c] + hi[c]) / 2;
            size += (hi[c] - lo[c]) * (hi[c] - lo[c]);
        }
        size = Math.max(Math.sqrt(size), 1e-3);
        final double az = Math.toRadians(35), el = Math.toRadians(8);
        final double[] d = {Math.cos(az) * Math.cos(el), -Math.sin(az) * Math.cos(el), Math.sin(el)};  // towards the camera
        // camera: looks along -d, up as near the world's Z as goes; right = up x d
        final double[] x = {-d[1], d[0], 0};
        Portrait.norm(x);
        final double[] y = new double[3];
        Portrait.cross(d, x, y);
        Portrait.norm(y);
        final double scale = size * 1.05;
        final int nv = m.nv;
        final float[] sx = new float[nv], sy = new float[nv];
        final int s = Math.max(1, samples), grid = RES * s;
        for (int i = 0; i < nv; i++) {
            final double qx = m.p[i * 3] - centre[0], qy = m.p[i * 3 + 1] - centre[1], qz = m.p[i * 3 + 2] - centre[2];
            sx[i] = (float) ((((qx * x[0] + qy * x[1] + qz * x[2]) / scale + 0.5) * RES + shiftX) * s);
            sy[i] = (float) (((0.5 - (qx * y[0] + qy * y[1] + qz * y[2]) / scale) * RES + shiftY) * s);
        }
        // coverage on the sample grid
        final boolean[] hit = new boolean[grid * grid];
        for (int k = 0; k < m.nt; k++) {
            final int a = m.tri[k * 3], b = m.tri[k * 3 + 1], c = m.tri[k * 3 + 2];
            final float ax = sx[a], bx = sx[b], cx = sx[c], ay = sy[a], by = sy[b], cy = sy[c];
            final float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            if (!(Math.abs(area) > 1e-12f)) continue;
            final float inv = 1f / area;
            final int xa = Math.max(0, (int) Math.floor(Math.min(ax, Math.min(bx, cx)) - 0.5f));
            final int xb = Math.min(grid - 1, (int) Math.ceil(Math.max(ax, Math.max(bx, cx)) - 0.5f));
            final int ya = Math.max(0, (int) Math.floor(Math.min(ay, Math.min(by, cy)) - 0.5f));
            final int yb = Math.min(grid - 1, (int) Math.ceil(Math.max(ay, Math.max(by, cy)) - 0.5f));
            for (int yy = ya; yy <= yb; yy++) {
                final float pyf = yy + 0.5f;
                for (int xx = xa; xx <= xb; xx++) {
                    if (hit[yy * grid + xx]) continue;
                    final float pxf = xx + 0.5f;
                    final float w1 = ((cx - pxf) * (ay - pyf) - (ax - pxf) * (cy - pyf)) * inv;
                    final float w2 = ((ax - pxf) * (by - pyf) - (bx - pxf) * (ay - pyf)) * inv;
                    if (w1 < 0 || w2 < 0 || w1 + w2 > 1) continue;
                    hit[yy * grid + xx] = true;
                }
            }
        }
        // Workbench's anti-aliasing: each pixel the Blackman-Harris weighted coverage within a pixel of its centre
        final int span = 3 * s;
        final float[] wt = new float[span * span];
        float wsum = 0;
        for (int j = 0; j < span; j++)
            for (int i = 0; i < span; i++) {
                final double dx = (i + 0.5) / s - 1.5, dy = (j + 0.5) / s - 1.5;
                wt[j * span + i] = (float) blackmanHarris(Math.sqrt(dx * dx + dy * dy), 2.0);
                wsum += wt[j * span + i];
            }
        for (int py = 0; py < RES; py++)
            for (int px = 0; px < RES; px++) {
                float acc = 0;
                for (int j = 0; j < span; j++) {
                    final int gy = (py - 1) * s + j;
                    if (gy < 0 || gy >= grid) continue;
                    for (int i = 0; i < span; i++) {
                        final int gx = (px - 1) * s + i;
                        if (gx < 0 || gx >= grid) continue;
                        if (hit[gy * grid + gx]) acc += wt[j * span + i];
                    }
                }
                mask[py * RES + px] = acc > (float) THRESHOLD * wsum;
            }
        return mask;
    }

    /** etchres.fit: the silhouette's box into the cell less a margin, the signed distance (cell units) -> CELL x CELL bytes. */
    public static byte[] cell(boolean[] mask) {
        int x0 = Integer.MAX_VALUE, x1 = -1, y0 = Integer.MAX_VALUE, y1 = -1;
        for (int yy = 0; yy < RES; yy++)
            for (int xx = 0; xx < RES; xx++)
                if (mask[yy * RES + xx]) {
                    x0 = Math.min(x0, xx);
                    x1 = Math.max(x1, xx + 1);
                    y0 = Math.min(y0, yy);
                    y1 = Math.max(y1, yy + 1);
                }
        final byte[] out = new byte[CELL * CELL];
        if (x1 < 0) {
            Arrays.fill(out, (byte) bytes((float) RANGE));
            return out;
        }
        final double span = Math.max(x1 - x0, y1 - y0) / (1.0 - 2 * MARGIN);
        final double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
        final int n = CELL * 4;
        final double[] u = new double[n];
        final int[] gx = new int[n], gy = new int[n];
        for (int i = 0; i < n; i++) {
            u[i] = (i + 0.5) / n - 0.5;
            gx[i] = clip((int) (cx + u[i] * span), 0, RES - 1);
            gy[i] = clip((int) (cy + u[i] * span), 0, RES - 1);
        }
        final boolean[] inside = new boolean[n * n];
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
                inside[r * n + c] = mask[gy[r] * RES + gx[c]] && Math.abs(u[c] * span) < span && Math.abs(u[r] * span) < span;
        // the outline: inside points with an outside one beside them (4-way, the border outside)
        int ne = 0;
        float[] pts = new float[1024];
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++) {
                if (!inside[r * n + c]) continue;
                final boolean up = r > 0 && inside[(r - 1) * n + c], dn = r + 1 < n && inside[(r + 1) * n + c];
                final boolean lf = c > 0 && inside[r * n + c - 1], rt = c + 1 < n && inside[r * n + c + 1];
                if (up && dn && lf && rt) continue;
                if (ne * 2 + 2 > pts.length) pts = Arrays.copyOf(pts, pts.length * 2);
                pts[ne * 2] = (float) ((c + 0.5) / n - 0.5);
                pts[ne * 2 + 1] = (float) ((r + 0.5) / n - 0.5);
                ne++;
            }
        // the nearest outline point: the same float32 sums as etchres.py's brute force, only the far points skipped
        // (buckets searched ring by ring until no nearer point can be left: the very same minimum)
        final int nb = 64;
        final int[] start = new int[nb * nb + 1], bucketOf = new int[ne];
        for (int e = 0; e < ne; e++) {
            final int bx = clip((int) Math.floor((pts[e * 2] + 0.5) * nb), 0, nb - 1);
            final int by = clip((int) Math.floor((pts[e * 2 + 1] + 0.5) * nb), 0, nb - 1);
            bucketOf[e] = by * nb + bx;
            start[bucketOf[e] + 1]++;
        }
        for (int b = 0; b < nb * nb; b++) start[b + 1] += start[b];
        final float[] bp = new float[ne * 2];
        final int[] fill = Arrays.copyOf(start, nb * nb);
        for (int e = 0; e < ne; e++) {
            final int at = fill[bucketOf[e]]++;
            bp[at * 2] = pts[e * 2];
            bp[at * 2 + 1] = pts[e * 2 + 1];
        }
        final double h = 1.0 / nb;
        for (int r = 0; r < CELL; r++) {
            final float qy = (float) ((r + 0.5) / CELL - 0.5);
            final int qby = clip((int) Math.floor((qy + 0.5) * nb), 0, nb - 1);
            for (int c = 0; c < CELL; c++) {
                final float qx = (float) ((c + 0.5) / CELL - 0.5);
                final int qbx = clip((int) Math.floor((qx + 0.5) * nb), 0, nb - 1);
                float best = 1e9f;
                for (int ring = 0; ring <= nb; ring++) {
                    // every point in a ring further out is at least ring * h away
                    if (ring > 0 && best < (ring - 1) * h * ((ring - 1) * h) * 0.999) break;
                    for (int by = qby - ring; by <= qby + ring; by++) {
                        if (by < 0 || by >= nb) continue;
                        final boolean edgeRow = by == qby - ring || by == qby + ring;
                        for (int bx = qbx - ring; bx <= qbx + ring; bx += edgeRow ? 1 : Math.max(1, 2 * ring)) {
                            if (bx < 0 || bx >= nb) continue;
                            final int b = by * nb + bx;
                            for (int e = start[b]; e < start[b + 1]; e++) {
                                final float dx = qx - bp[e * 2], dy = qy - bp[e * 2 + 1];
                                final float dd = dx * dx + dy * dy;
                                if (dd < best) best = dd;
                            }
                        }
                    }
                }
                final float dist = (float) Math.sqrt(best);
                int in = 0;
                for (int a = 0; a < 4; a++)
                    for (int b = 0; b < 4; b++)
                        if (inside[(r * 4 + a) * n + c * 4 + b]) in++;
                final float sdf = in * 2 > 16 ? -dist : dist;  // the mean of 16 over a half
                out[r * CELL + c] = (byte) bytes(sdf);
            }
        }
        return out;
    }

    /** clip(rint(128 + sdf / RANGE * 127), 0, 255), in float32 as numpy. */
    static int bytes(float sdf) {
        final float v = 128f + sdf / (float) RANGE * 127f;
        final double r = Math.rint(v);
        return (int) Math.max(0, Math.min(255, r));
    }

    /** workbench_effect_antialiasing.c filter_blackman_harris (width = the window's whole width). */
    static double blackmanHarris(double x, double width) {
        if (x > width * 0.5) return 0;
        final double t = 2 * Math.PI * Math.max(0, Math.min(1, x / width + 0.5));
        return 0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t);
    }

    private static int clip(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}
