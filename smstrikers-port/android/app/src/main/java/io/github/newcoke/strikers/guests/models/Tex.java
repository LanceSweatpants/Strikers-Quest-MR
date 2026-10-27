package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.GxTex;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

/** Texture rules of pokemon2obj.py / meleepack.py: TObj pictures, wrap modes, colour ops, GX RGBA8 records. */
final class Tex {
    private Tex() {}

    static final int GX_CLAMP = 0, GX_REPEAT = 1, GX_MIRROR = 2;
    static final int TEX_COORD_UV = 0, TEX_COLORMAP_BLEND = 3, TEX_COLORMAP_MODULATE = 4;

    /** A decoded picture (rgba is never changed once made: callers copy before changing it). */
    static final class Pic {
        final byte[] rgba;
        final int w, h, data;

        Pic(byte[] rgba, int w, int h, int data) {
            this.rgba = rgba;
            this.w = w;
            this.h = h;
            this.data = data;
        }
    }

    /** pokemon2obj.Textures: a TObj's GX image decoded once (key: data, w, h, format, palette). */
    static final class Cache {
        private final Hsd d;
        private final Map<String, byte[]> cache = new HashMap<>();

        Cache(Hsd d) {
            this.d = d;
        }

        Pic rgba(int tobj) throws IOException {
            final int img = d.u32(tobj + 0x4C);
            final int data = d.u32(img), w = d.u16(img + 4), h = d.u16(img + 6), fmt = d.u32(img + 8);
            final int tl = d.u32(tobj + 0x50);
            final String key = data + "/" + w + "/" + h + "/" + fmt + "/" + tl;
            byte[] px = cache.get(key);
            if (px == null) {
                if (!GxTex.known(fmt)) throw new IOException("texture format " + fmt);
                int[] pal = null;
                if ((fmt == 8 || fmt == 9) && tl != 0) {
                    final int pfmt = d.u32(tl + 4);
                    pal = GxTex.palette(d.b, Hsd.D + d.u32(tl), d.u16(tl + 0x0C), pfmt);
                } else if (fmt == 8 || fmt == 9) {
                    throw new IOException("palette texture without a palette");
                }
                if (Hsd.D + data + GxTex.sizeOf(fmt, w, h) > d.b.length) throw new IOException("texture past the end");
                px = GxTex.decode(d.b, Hsd.D + data, fmt, w, h, pal);
                cache.put(key, px);
            }
            return new Pic(px, w, h, data);
        }
    }

    static int[] tobjList(Hsd d, int mobj) {
        int n = 0;
        for (int t = d.u32(mobj + 0x08); t != 0; t = d.u32(t + 0x04)) n++;
        final int[] out = new int[n];
        n = 0;
        for (int t = d.u32(mobj + 0x08); t != 0; t = d.u32(t + 0x04)) out[n++] = t;
        return out;
    }

    static int wrapIndex(int i, int n, int mode) {
        if (mode == GX_REPEAT) return Math.floorMod(i, n);
        if (mode == GX_MIRROR) {
            final int k = Math.floorMod(i, 2 * n);
            return k < n ? k : 2 * n - 1 - k;
        }
        return Math.min(Math.max(i, 0), n - 1);
    }

    /** pokemon2obj.bake_wrap -> rgba of (w * ns) x (h * nt). */
    static byte[] bakeWrap(byte[] rgba, int w, int h, int s0, int ns, int t0, int nt, int ws, int wt) {
        final int ow = w * ns, oh = h * nt;
        final int[] cols = new int[ow];
        for (int x = 0; x < ow; x++) cols[x] = wrapIndex(s0 * w + x, w, ws);
        final byte[] out = new byte[ow * oh * 4];
        for (int y = 0; y < oh; y++) {
            final int sy = wrapIndex(t0 * h + y, h, wt);
            for (int x = 0; x < ow; x++) System.arraycopy(rgba, (sy * w + cols[x]) * 4, out, (y * ow + x) * 4, 4);
        }
        return out;
    }

    /** pokemon2obj.avg_colour */
    static double[] avgColour(byte[] rgba) {
        final int n = rgba.length / 4;
        long wsum = 0;
        for (int i = 0; i < n; i++) wsum += rgba[i * 4 + 3] & 0xFF;
        final double[] out = new double[3];
        for (int c = 0; c < 3; c++) {
            long s = 0;
            if (wsum != 0) {
                for (int i = 0; i < n; i++) s += (long) (rgba[i * 4 + c] & 0xFF) * (rgba[i * 4 + 3] & 0xFF);
                out[c] = (double) s / (double) wsum / 255.0;
            } else {
                for (int i = 0; i < n; i++) s += rgba[i * 4 + c] & 0xFF;
                out[c] = (double) s / (double) n / 255.0;
            }
        }
        return out;
    }

    /** pokemon2obj.colour_op: null = the texture as it is. */
    static ColourOp colourOp(int cm, double blend, double[] diffuse) {
        if (cm == TEX_COLORMAP_MODULATE) return c -> new double[] {c[0] * diffuse[0], c[1] * diffuse[1], c[2] * diffuse[2]};
        if (cm == TEX_COLORMAP_BLEND && blend < 0.999)
            return c -> new double[] {diffuse[0] * (1 - blend) + c[0] * blend, diffuse[1] * (1 - blend) + c[1] * blend,
                diffuse[2] * (1 - blend) + c[2] * blend};
        return null;
    }

    interface ColourOp {
        double[] apply(double[] c);
    }

    /** pokemon2obj.apply_colour (a new array). */
    static byte[] applyColour(byte[] rgba, ColourOp op) {
        final byte[] out = rgba.clone();
        final double[] c = new double[3];
        for (int i = 0; i < out.length; i += 4) {
            c[0] = (out[i] & 0xFF) / 255.0;
            c[1] = (out[i + 1] & 0xFF) / 255.0;
            c[2] = (out[i + 2] & 0xFF) / 255.0;
            final double[] r = op.apply(c);
            for (int k = 0; k < 3; k++) out[i + k] = (byte) Math.min(255, Math.max(0, (int) Math.rint(r[k] * 255)));
        }
        return out;
    }

    /** meleepack.sample_bilinear -> (r, g, b, a) floats. */
    static double[] sampleBilinear(byte[] rgba, int w, int h, double s, double t, int ws, int wt) {
        final double x = s * w - 0.5, y = t * h - 0.5;
        final double x0 = Math.floor(x), y0 = Math.floor(y);
        final double fx = x - x0, fy = y - y0;
        final int ix0 = (int) x0, iy0 = (int) y0;
        final double[] out = new double[4];
        for (int dy = 0; dy < 2; dy++) {
            final double wy = dy == 0 ? 1.0 - fy : fy;
            final int row = wrapIndex(iy0 + dy, h, wt) * w;
            for (int dx = 0; dx < 2; dx++) {
                final double wx = dx == 0 ? 1.0 - fx : fx;
                final double wgt = wx * wy;
                if (wgt <= 0.0) continue;
                final int o = (row + wrapIndex(ix0 + dx, w, ws)) * 4;
                for (int c = 0; c < 4; c++) out[c] += (rgba[o + c] & 0xFF) * wgt;
            }
        }
        return out;
    }

    /** meleepack.gx_rgba8: RGBA -> GX RGBA8 tiles (4 x 4: 32 bytes AR, then 32 bytes GB; edges repeated). */
    static byte[] gxRgba8(byte[] rgba, int w, int h) {
        final int bw = (w + 3) / 4, bh = (h + 3) / 4;
        final byte[] out = new byte[bw * bh * 64];
        int p = 0;
        for (int by = 0; by < h; by += 4) {
            for (int bx = 0; bx < w; bx += 4) {
                for (int i = 0; i < 16; i++) {
                    final int px = Math.min(bx + (i & 3), w - 1), py = Math.min(by + (i >> 2), h - 1);
                    final int o = (py * w + px) * 4;
                    out[p + i * 2] = rgba[o + 3];
                    out[p + i * 2 + 1] = rgba[o];
                    out[p + 32 + i * 2] = rgba[o + 1];
                    out[p + 32 + i * 2 + 1] = rgba[o + 2];
                }
                p += 64;
            }
        }
        return out;
    }

    /** meleepack.texture_record: GXTextureHeader (big-endian) + RGBA8 tiles; alpha bits 1 when any texel < 250. */
    static byte[] record(byte[] rgba, int w, int h) {
        boolean see = false;
        for (int i = 3; i < rgba.length; i += 4) {
            if ((rgba[i] & 0xFF) < 250) {
                see = true;
                break;
            }
        }
        final byte[] tiles = gxRgba8(rgba, w, h);
        final byte[] out = new byte[32 + tiles.length];
        out[3] = 1;
        out[7] = 3;
        out[8] = 8;
        out[9] = 8;
        out[10] = 8;
        out[11] = (byte) (see ? 1 : 0);
        out[14] = (byte) (w >> 8);
        out[15] = (byte) w;
        out[16] = (byte) (h >> 8);
        out[17] = (byte) h;
        System.arraycopy(tiles, 0, out, 32, tiles.length);
        return out;
    }

    /** GX RGBA8 tiles back to RGBA (colo_shiny.rgba8_decode). */
    static byte[] rgba8(byte[] b, int off, int w, int h) {
        return GxTex.decode(b, off, GxTex.RGBA8, w, h, null);
    }
}
