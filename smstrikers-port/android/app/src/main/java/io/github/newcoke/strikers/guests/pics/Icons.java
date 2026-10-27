package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.Arrays;
import java.util.List;

/**
 * The score bar's team banners for guest captains (tools/outside/iconres.py, the same numbers): Strikers'
 * banner (art/fe/CaptainIconsUI.Res: the shape and the light-to-dark sweep, the median of the game's own ten)
 * with the guest's series emblem from Melee's in-match HUD (IfAll.usd) drawn on it filled with a dark outline,
 * lit from above. guests/icons/<pack>.gtx and <pack>_r.gtx (mirrored): the game's own record header, 256 x 64
 * CI8, 4 levels, RGB5A3 palette after them (22304 bytes, the size of the one it stands in for).
 */
final class Icons {
    static final int W = 256, H = 64, LEVELS = 4;

    final byte[] header;
    final int size;
    private final int[] bg;  // W * H * 4 (r, g, b, a)
    final int top, bottom;

    /** From CaptainIconsUI.Res's records. */
    Icons(List<Res.Rec> recs) throws IOException {
        final byte[] r0 = recs.get(0).b;
        if (Bin.u16(r0, 0x0E) != W || Bin.u16(r0, 0x10) != H || Bin.u32(r0, 0) != LEVELS)
            throw new IOException("CaptainIconsUI: not a 256 x 64 x 4 banner");
        header = Arrays.copyOf(r0, 0x20);
        size = r0.length;
        // median_banner
        final int n = recs.size();
        final int[][] imgs = new int[n][];
        for (int i = 0; i < n; i++) imgs[i] = decode(recs.get(i).b);
        bg = new int[W * H * 4];
        int first = -1, last = -1;
        final int[] col = new int[n];
        final int[] alpha = new int[W];
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                for (int i = 0; i < n; i++) col[i] = imgs[i][(y * W + x) * 4 + 3];
                Arrays.sort(col);
                alpha[x] = col[n / 2];
            }
            int xl = -1, xr = -1;
            for (int x = 0; x < W; x++) {
                if (alpha[x] > 128) {
                    if (xl < 0) xl = x;
                    xr = x;
                }
            }
            if (xl < 0) continue;  // (transparent row)
            if (first < 0) first = y;
            last = y;
            final int[] left = end(imgs, y, xl, xl + 12), right = end(imgs, y, Math.max(xl, xr - 12), xr + 1);
            for (int x = 0; x < W; x++) {
                double t = (x - xl - 6) / (double) Math.max(1, xr - xl - 12);
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                final int o = (y * W + x) * 4;
                for (int ch = 0; ch < 3; ch++) bg[o + ch] = (int) (left[ch] * (1 - t) + right[ch] * t + 0.5);
                bg[o + 3] = alpha[x];
            }
        }
        if (first < 0) throw new IOException("CaptainIconsUI: no banner");
        top = first;
        bottom = last + 1;
    }

    /** iconres.decode: level 0 of a 256 x 64 CI8 record whose palette follows all its levels. */
    private static int[] decode(byte[] rec) {
        final int w = Bin.u16(rec, 0x0E), h = Bin.u16(rec, 0x10);
        final int levels = Bin.u32(rec, 0);
        int total = 0;
        for (int k = 0; k < levels; k++) total += (w >> k) * (h >> k);
        final int[] pal = new int[256];
        for (int j = 0; j < 256; j++) pal[j] = Res.rgb5a3(Bin.u16(rec, 0x20 + total + 2 * j));
        final int[] px = new int[W * H * 4];
        int k = 0;
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++) {
                        final int c = pal[rec[0x20 + k++] & 0xFF];
                        final int o = ((ty + y) * W + tx + x) * 4;
                        px[o] = (c >> 16) & 255;
                        px[o + 1] = (c >> 8) & 255;
                        px[o + 2] = c & 255;
                        px[o + 3] = c >>> 24;
                    }
        return px;
    }

    /** The median colour of the ten's opaque pixels in [x0, x1) of row y. */
    private static int[] end(int[][] imgs, int y, int x0, int x1) {
        final int[][] cs = new int[3][imgs.length * Math.max(0, x1 - x0)];
        int n = 0;
        for (int[] im : imgs)
            for (int x = x0; x < x1 && x < W; x++) {
                final int o = (y * W + x) * 4;
                if (im[o + 3] > 128) {
                    for (int ch = 0; ch < 3; ch++) cs[ch][n] = im[o + ch];
                    n++;
                }
            }
        if (n == 0) return new int[3];
        final int[] out = new int[3];
        for (int ch = 0; ch < 3; ch++) {
            final int[] v = Arrays.copyOf(cs[ch], n);
            Arrays.sort(v);
            out[ch] = v[n / 2];
        }
        return out;
    }

    /**
     * One series' banner picture (RGBA ints, W x H): emblem = the emblem's RGBA (ew x eh); fill / line = its
     * colours.
     */
    int[] compose(byte[] emblem, int ew, int eh, int[] fill, int[] line) {
        final double[] cov = emblemAlpha(emblem, ew, eh, bottom - top - 8, 118, top + (bottom - top) / 2.0);
        final double[] ring = dilate(cov, 3);
        final int[] out = new int[W * H * 4];
        for (int y = 0; y < H; y++) {
            final double shade = 1.18 - 0.36 * y / (H - 1);  // lit from above, as the game's emblems
            for (int x = 0; x < W; x++) {
                final int i = y * W + x, o = i * 4;
                final double rg = ring[i], f = cov[i];
                for (int ch = 0; ch < 3; ch++) {
                    double c = bg[o + ch] * (1 - rg) + line[ch] * rg;
                    c = c * (1 - f) + Math.min(255.0, fill[ch] * shade) * f;
                    out[o + ch] = (int) (c + 0.5);
                }
                out[o + 3] = bg[o + 3];  // the banner's own shape
            }
        }
        return out;
    }

    /** iconres.emblem_alpha: the emblem's coverage (0..1) on the banner, `height` px tall, centred at (cx, cy). */
    private static double[] emblemAlpha(byte[] rgba, int w, int h, int height, int cx, double cy) {
        int x0 = Integer.MAX_VALUE, x1 = Integer.MIN_VALUE, y0 = Integer.MAX_VALUE, y1 = Integer.MIN_VALUE;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if ((rgba[(y * w + x) * 4 + 3] & 255) > 16) {
                    x0 = Math.min(x0, x);
                    x1 = Math.max(x1, x + 1);
                    y0 = Math.min(y0, y);
                    y1 = Math.max(y1, y + 1);
                }
        final double s = height / (double) (y1 - y0);
        final double ew = (x1 - x0) * s;
        final double[] a = new double[W * H];
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                final double fx = x0 + (x + 0.5 - (cx - ew / 2)) / s - 0.5;
                final double fy = y0 + (y + 0.5 - (cy - height / 2.0)) / s - 0.5;
                if (x0 - 1 <= fx && fx <= x1 && y0 - 1 <= fy && fy <= y1) a[y * W + x] = sampleAlpha(rgba, w, h, fx, fy) / 255.0;
            }
        }
        return a;
    }

    /** iconres.sample's alpha: bilinear at (fx, fy), edges clamped (the same sums in the same order). */
    private static double sampleAlpha(byte[] img, int w, int h, double fx, double fy) {
        final int x0 = (int) fx, y0 = (int) fy;
        final double tx = fx - x0, ty = fy - y0;
        double acc = 0.0;
        for (int dy = 0; dy < 2; dy++) {
            final double wy = dy == 0 ? 1 - ty : ty;
            for (int dx = 0; dx < 2; dx++) {
                final double wx = dx == 0 ? 1 - tx : tx;
                final int x = Math.min(Math.max(x0 + dx, 0), w - 1), y = Math.min(Math.max(y0 + dy, 0), h - 1);
                acc += (img[(y * w + x) * 4 + 3] & 255) * wx * wy;
            }
        }
        return acc;
    }

    private static double[] dilate(double[] a, int r) {
        final double[] out = new double[W * H];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                double m = 0.0;
                for (int dy = -r; dy <= r; dy++)
                    for (int dx = -r; dx <= r; dx++)
                        if (dx * dx + dy * dy <= r * r + 1) {
                            final int yy = y + dy, xx = x + dx;
                            if (0 <= yy && yy < H && 0 <= xx && xx < W && a[yy * W + xx] > m) m = a[yy * W + xx];
                        }
                out[y * W + x] = m;
            }
        return out;
    }

    /** The record for a banner picture (RGBA ints, W x H), mirrored or not. */
    byte[] record(int[] img, boolean mirror) throws IOException {
        int[] lv = img;
        if (mirror) {
            lv = new int[W * H * 4];
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    System.arraycopy(img, (y * W + x) * 4, lv, (y * W + W - 1 - x) * 4, 4);
        }
        final int[][] codes = new int[LEVELS][];
        final int[] ws = new int[LEVELS], hs = new int[LEVELS];
        int w = W, h = H;
        for (int l = 0; l < LEVELS; l++) {
            ws[l] = w;
            hs[l] = h;
            codes[l] = new int[w * h];
            for (int i = 0; i < w * h; i++)
                codes[l][i] = Res.toRgb5a3(lv[i * 4], lv[i * 4 + 1], lv[i * 4 + 2], lv[i * 4 + 3]);
            if (l == LEVELS - 1) break;
            // iconres.levels: alpha-weighted 2 x 2 average
            final int nw = w / 2, nh = h / 2;
            final int[] q = new int[nw * nh * 4];
            for (int y = 0; y < nh; y++)
                for (int x = 0; x < nw; x++) {
                    int at = 0;
                    final int[] o4 = new int[4];
                    int k = 0;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            o4[k] = ((2 * y + dy) * w + 2 * x + dx) * 4;
                            at += lv[o4[k] + 3];
                            k++;
                        }
                    final int o = (y * nw + x) * 4;
                    if (at == 0) continue;
                    for (int ch = 0; ch < 3; ch++) {
                        int s = 0;
                        for (int j = 0; j < 4; j++) s += lv[o4[j] + ch] * lv[o4[j] + 3];
                        q[o + ch] = (int) ((double) s / at + 0.5);
                    }
                    q[o + 3] = at / 4;
                }
            lv = q;
            w = nw;
            h = nh;
        }
        final byte[] rec = Res.encodeCi8(header, codes, ws, hs);
        if (rec.length != size) throw new IOException("icon record " + rec.length + " bytes, the game's " + size);
        return rec;
    }
}
