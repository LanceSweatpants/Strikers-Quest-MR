package io.github.newcoke.strikers.guests.pics;

/**
 * GX CMPR (8 x 8 tiles of 2 x 2 DXT1 blocks) as tools/outside/kitres.py's encode_cmpr makes it, float for
 * float: its numpy float32 maths in the same order (sums run first to last from 0, each product rounded, as
 * numpy's einsum / reduce loops do them for these shapes), so the endpoints (the pixels furthest along the
 * block's main axis, PCA by 8 power steps) and each texel's choice come out the same.
 */
final class Cmpr {
    private Cmpr() {}

    /** RGBA float image (h x w, 4 floats a pixel, alpha 0 / 255) -> CMPR bytes, into out at o; returns the end. */
    static int encode(float[] img, int w, int h, byte[] out, int o) {
        final float[] rgb = new float[48];
        final boolean[] clear = new boolean[16];
        final float[] proj = new float[16];
        for (int ty = 0; ty < h; ty += 8) {
            for (int tx = 0; tx < w; tx += 8) {
                for (int sb = 0; sb < 4; sb++) {
                    final int sy = ty + (sb >> 1) * 4, sx = tx + (sb & 1) * 4;
                    boolean anyClear = false;
                    for (int i = 0; i < 16; i++) {
                        final int p = ((sy + (i >> 2)) * w + sx + (i & 3)) * 4;
                        rgb[i * 3] = img[p];
                        rgb[i * 3 + 1] = img[p + 1];
                        rgb[i * 3 + 2] = img[p + 2];
                        clear[i] = img[p + 3] < 128f;
                        anyClear |= clear[i];
                    }
                    o = block(rgb, clear, anyClear, proj, out, o);
                }
            }
        }
        return o;
    }

    private static int block(float[] rgb, boolean[] clear, boolean anyClear, float[] proj, byte[] out, int o) {
        // mean over the 16 (a sum from 0, first to last), centred
        float m0 = 0f, m1 = 0f, m2 = 0f;
        for (int i = 0; i < 16; i++) {
            m0 += rgb[i * 3];
            m1 += rgb[i * 3 + 1];
            m2 += rgb[i * 3 + 2];
        }
        m0 /= 16f;
        m1 /= 16f;
        m2 /= 16f;
        final float[] cen = new float[48];
        for (int i = 0; i < 16; i++) {
            cen[i * 3] = rgb[i * 3] - m0;
            cen[i * 3 + 1] = rgb[i * 3 + 1] - m1;
            cen[i * 3 + 2] = rgb[i * 3 + 2] - m2;
        }
        // covariance: einsum("nki,nkj->nij"), k first to last
        final float[] cov = new float[9];
        for (int k = 0; k < 16; k++)
            for (int i = 0; i < 3; i++) {
                final float v = cen[k * 3 + i];
                for (int j = 0; j < 3; j++) cov[i * 3 + j] = v * cen[k * 3 + j] + cov[i * 3 + j];
            }
        // 8 power steps from (1, 1, 1)
        float a0 = 1f, a1 = 1f, a2 = 1f;
        for (int it = 0; it < 8; it++) {
            final float b0 = (cov[0] * a0 + cov[1] * a1) + cov[2] * a2;
            final float b1 = (cov[3] * a0 + cov[4] * a1) + cov[5] * a2;
            final float b2 = (cov[6] * a0 + cov[7] * a1) + cov[8] * a2;
            final float n = (float) Math.sqrt((b0 * b0 + b1 * b1) + b2 * b2) + 1e-6f;
            a0 = b0 / n;
            a1 = b1 / n;
            a2 = b2 / n;
        }
        int imax = 0, imin = 0;
        for (int i = 0; i < 16; i++) {
            proj[i] = clear[i] ? 0f : (cen[i * 3] * a0 + cen[i * 3 + 1] * a1) + cen[i * 3 + 2] * a2;
            if (proj[i] > proj[imax]) imax = i;
            if (proj[i] < proj[imin]) imin = i;
        }
        int c0 = to565(rgb[imax * 3], rgb[imax * 3 + 1], rgb[imax * 3 + 2]);
        int c1 = to565(rgb[imin * 3], rgb[imin * 3 + 1], rgb[imin * 3 + 2]);
        // four colours (c0 > c1), or three + clear (c0 <= c1) where the block has see-through texels
        final boolean swap = anyClear ? c0 > c1 : c0 < c1;
        if (swap) {
            final int t = c0;
            c0 = c1;
            c1 = t;
        }
        final float[] pal = new float[12];
        from565(c0, pal, 0);
        from565(c1, pal, 3);
        final boolean four = c0 > c1;
        for (int ch = 0; ch < 3; ch++) {
            final float e0 = pal[ch], e1 = pal[3 + ch];
            if (four) {
                pal[6 + ch] = (2f * e0 + e1) / 3f;
                pal[9 + ch] = (e0 + 2f * e1) / 3f;
            } else {
                pal[6 + ch] = (e0 + e1) / 2f;
                pal[9 + ch] = 1e6f;
            }
        }
        long bits = 0;
        for (int i = 0; i < 16; i++) {
            int idx = 0;
            float best = 0f;
            for (int j = 0; j < 4; j++) {
                final float d0 = rgb[i * 3] - pal[j * 3], d1 = rgb[i * 3 + 1] - pal[j * 3 + 1], d2 = rgb[i * 3 + 2] - pal[j * 3 + 2];
                final float d = (d0 * d0 + d1 * d1) + d2 * d2;
                if (j == 0 || d < best) {
                    best = d;
                    idx = j;
                }
            }
            if (clear[i] && !four) idx = 3;
            if (c0 == c1 && !clear[i]) idx = 0;
            bits |= (long) (idx & 3) << (30 - 2 * i);
        }
        out[o] = (byte) (c0 >> 8);
        out[o + 1] = (byte) c0;
        out[o + 2] = (byte) (c1 >> 8);
        out[o + 3] = (byte) c1;
        out[o + 4] = (byte) (bits >> 24);
        out[o + 5] = (byte) (bits >> 16);
        out[o + 6] = (byte) (bits >> 8);
        out[o + 7] = (byte) bits;
        return o + 8;
    }

    private static int q(float v, int max) {
        int c = (int) Math.rint(v);
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        return (c * max + 127) / 255;
    }

    static int to565(float r, float g, float b) {
        return (q(r, 31) << 11) | (q(g, 63) << 5) | q(b, 31);
    }

    private static void from565(int v, float[] out, int o) {
        final int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        out[o] = (r << 3) | (r >> 2);
        out[o + 1] = (g << 2) | (g >> 4);
        out[o + 2] = (b << 3) | (b >> 2);
    }

    /** kitres.mips: the next level (2 x 2 mean, summed first to last in rows, alpha cut at 128). */
    static float[] half(float[] p, int w, int h) {
        final int nw = w / 2, nh = h / 2;
        final float[] q = new float[nw * nh * 4];
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                final int a = ((2 * y) * w + 2 * x) * 4, b = a + 4, c = a + w * 4, d = c + 4, o = (y * nw + x) * 4;
                for (int ch = 0; ch < 4; ch++) q[o + ch] = (((p[a + ch] + p[b + ch]) + p[c + ch]) + p[d + ch]) / 4f;
                q[o + 3] = q[o + 3] >= 128f ? 255f : 0f;
            }
        return q;
    }
}
