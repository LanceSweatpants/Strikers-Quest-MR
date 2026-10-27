package io.github.newcoke.strikers.guests;

/**
 * GameCube / Wii (GX) tiled textures to RGBA (tools/outside/gxtex.py's decode, the same numbers). Formats by GX
 * number: 0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 8 C4, 9 C8, 14 CMPR. Palettes: 0 IA8, 1 RGB565,
 * 2 RGB5A3.
 */
public final class GxTex {
    private GxTex() {}

    public static final int I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5, RGBA8 = 6, C4 = 8, C8 = 9,
            CMPR = 14;

    public static int tileW(int fmt) {
        switch (fmt) {
            case I4: case I8: case IA4: case C4: case C8: case CMPR: return 8;
            default: return 4;
        }
    }

    public static int tileH(int fmt) {
        switch (fmt) {
            case I4: case C4: case CMPR: return 8;
            case I8: case IA4: case C8: return 4;
            default: return 4;
        }
    }

    public static int bpp(int fmt) {
        switch (fmt) {
            case I4: case C4: case CMPR: return 4;
            case I8: case IA4: case C8: return 8;
            case RGBA8: return 32;
            default: return 16;
        }
    }

    public static boolean known(int fmt) {
        return fmt == I4 || fmt == I8 || fmt == IA4 || fmt == IA8 || fmt == RGB565 || fmt == RGB5A3 || fmt == RGBA8
                || fmt == C4 || fmt == C8 || fmt == CMPR;
    }

    /** Bytes of a w x h image in this format (whole tiles). */
    public static int sizeOf(int fmt, int w, int h) {
        final int tw = tileW(fmt), th = tileH(fmt);
        final int bw = (w + tw - 1) / tw, bh = (h + th - 1) / th;
        return bw * bh * tw * th * bpp(fmt) / 8;
    }

    /** RGB565 as packed 0xAARRGGBB. */
    public static int c565(int v) {
        final int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        return argb((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
    }

    public static int c5a3(int v) {
        if ((v & 0x8000) != 0) {
            final int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            return argb((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255);
        }
        final int a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        return argb(r * 17, g * 17, b * 17, (a << 5) | (a << 2) | (a >> 1));
    }

    public static int argb(int r, int g, int b, int a) {
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    /** The palette (big-endian u16 entries) as packed colours. */
    public static int[] palette(byte[] pal, int palOff, int count, int palFmt) {
        int[] p = new int[count];
        for (int i = 0; i < count; i++) {
            final int v = Bin.u16(pal, palOff + i * 2);
            p[i] = palFmt == 2 ? c5a3(v) : palFmt == 1 ? c565(v) : argb(v & 255, v & 255, v & 255, v >> 8);
        }
        return p;
    }

    /** RGBA bytes (w*h*4, top row first) of the image at data[off]. pal: from palette(), or null. */
    public static byte[] decode(byte[] data, int off, int fmt, int w, int h, int[] pal) {
        final byte[] out = new byte[w * h * 4];
        final int tw = tileW(fmt), th = tileH(fmt);
        int p = off;  // a byte offset, or (I4 / C4) a nibble offset from off
        int nib = 0;
        final int[] cols = new int[4];
        for (int by = 0; by < h; by += th) {
            for (int bx = 0; bx < w; bx += tw) {
                if (fmt == CMPR) {  // 2 x 2 DXT1 sub-blocks of 4 x 4
                    for (int sb = 0; sb < 4; sb++) {
                        final int c0 = Bin.u16(data, p), c1 = Bin.u16(data, p + 2);
                        final long bits = Bin.u32L(data, p + 4);
                        p += 8;
                        final int a = c565(c0), b = c565(c1);
                        cols[0] = a;
                        cols[1] = b;
                        if (c0 > c1) {
                            cols[2] = mix(a, b, 2, 1, 3);
                            cols[3] = mix(a, b, 1, 2, 3);
                        } else {
                            cols[2] = mix(a, b, 1, 1, 2);
                            cols[3] = 0;
                        }
                        final int sx = bx + (sb & 1) * 4, sy = by + (sb >> 1) * 4;
                        for (int i = 0; i < 16; i++)
                            put(out, w, h, sx + (i & 3), sy + (i >> 2), cols[(int) ((bits >> (30 - 2 * i)) & 3)]);
                    }
                } else if (fmt == RGBA8) {  // the AR plane, then the GB plane
                    for (int i = 0; i < 16; i++) {
                        final int a = data[p + i * 2] & 0xFF, r = data[p + i * 2 + 1] & 0xFF;
                        final int g = data[p + 32 + i * 2] & 0xFF, b = data[p + 32 + i * 2 + 1] & 0xFF;
                        put(out, w, h, bx + (i & 3), by + (i >> 2), argb(r, g, b, a));
                    }
                    p += 64;
                } else {
                    for (int y = 0; y < th; y++) {
                        for (int x = 0; x < tw; x++) {
                            int c;
                            if (fmt == I4 || fmt == C4) {
                                final int v0 = data[off + (nib >> 1)] & 0xFF;
                                final int v = (nib & 1) == 0 ? v0 >> 4 : v0 & 15;
                                nib++;
                                c = fmt == C4 ? pal[v] : argb(v * 17, v * 17, v * 17, v * 17);
                            } else if (fmt == I8 || fmt == C8) {
                                final int v = data[p++] & 0xFF;
                                c = fmt == C8 ? pal[v] : argb(v, v, v, v);
                            } else if (fmt == IA4) {
                                final int v = data[p++] & 0xFF;
                                final int i = (v & 15) * 17;
                                c = argb(i, i, i, (v >> 4) * 17);
                            } else if (fmt == IA8) {
                                final int a = data[p] & 0xFF, i = data[p + 1] & 0xFF;
                                p += 2;
                                c = argb(i, i, i, a);
                            } else {
                                final int v = Bin.u16(data, p);
                                p += 2;
                                c = fmt == RGB565 ? c565(v) : c5a3(v);
                            }
                            put(out, w, h, bx + x, by + y, c);
                        }
                    }
                }
            }
        }
        return out;
    }

    /** (ka * a + kb * b) / div per colour channel (integer, as Python's //), alpha 255. */
    private static int mix(int a, int b, int ka, int kb, int div) {
        int r = (ka * ((a >> 16) & 255) + kb * ((b >> 16) & 255)) / div;
        int g = (ka * ((a >> 8) & 255) + kb * ((b >> 8) & 255)) / div;
        int bl = (ka * (a & 255) + kb * (b & 255)) / div;
        return argb(r, g, bl, 255);
    }

    private static void put(byte[] out, int w, int h, int x, int y, int c) {
        if (x < w && y < h) {
            final int o = (y * w + x) * 4;
            out[o] = (byte) (c >> 16);
            out[o + 1] = (byte) (c >> 8);
            out[o + 2] = (byte) c;
            out[o + 3] = (byte) (c >>> 24);
        }
    }
}
