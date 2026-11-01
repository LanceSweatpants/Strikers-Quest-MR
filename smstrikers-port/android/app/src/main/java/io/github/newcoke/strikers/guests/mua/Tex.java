package io.github.newcoke.strikers.guests.mua;

import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** meleepack.py's texture record (GX RGBA8 with its header) and its name hash (nlStringLowerHash). */
final class Tex {
    private Tex() {}

    /** texture_record: GXTextureHeader (big-endian) + RGBA8 tiles; alpha bit 1 when any texel's alpha < 250. */
    static byte[] record(byte[] rgba, int w, int h) {
        boolean see = false;
        for (int i = 3; i < rgba.length; i += 4) if ((rgba[i] & 0xFF) < 250) {
            see = true;
            break;
        }
        final int bw = (w + 3) / 4, bh = (h + 3) / 4;
        final byte[] out = new byte[32 + bw * bh * 64];
        out[3] = 1;          // levels
        out[7] = 3;          // format RGBA8
        out[8] = 8;
        out[9] = 8;
        out[10] = 8;
        out[11] = (byte) (see ? 1 : 0);
        out[12] = 0;         // B 0
        out[14] = (byte) (w >> 8);
        out[15] = (byte) w;
        out[16] = (byte) (h >> 8);
        out[17] = (byte) h;
        int o = 32;
        for (int by = 0; by < h; by += 4) {
            for (int bx = 0; bx < w; bx += 4) {
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        final int px = Math.min(bx + x, w - 1), py = Math.min(by + y, h - 1);
                        final int s = (py * w + px) * 4;
                        final int i = (y * 4 + x) * 2;
                        out[o + i] = rgba[s + 3];
                        out[o + i + 1] = rgba[s];
                        out[o + 32 + i] = rgba[s + 1];
                        out[o + 32 + i + 1] = rgba[s + 2];
                    }
                }
                o += 64;
            }
        }
        return out;
    }

    static int lowerHash(String s) {
        int h = 0xFFFFFFFF;
        for (byte b : s.toLowerCase(Locale.ROOT).getBytes(StandardCharsets.UTF_8)) h = h + (h << 5) + (b & 0xFF);
        return h;
    }
}
