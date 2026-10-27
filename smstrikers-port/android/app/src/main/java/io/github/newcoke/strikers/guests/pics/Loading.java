package io.github.newcoke.strikers.guests.pics;

import java.io.IOException;
import java.util.Arrays;

/**
 * Guest loading pictures (tools/outside/loadres.py guests, the same numbers): a guest's portrait fitted into
 * Strikers' 256 x 256 loading picture (art/fe/LoadingScreensUI.Res), the right one mirrored, plus its dark
 * outline (_bg) and white flash (_white): guests/loading/<pack>_<l|r>[_bg|_white].gtx, each a GX CI8 record with
 * an RGB5A3 palette and the game's own header (the loader replaces a picture's pixels in place: same size).
 */
final class Loading {
    static final int SIZE = 256, MARGIN = 8;
    static final String[] SUFFIX = {"_l", "_l_bg", "_l_white", "_r", "_r_bg", "_r_white"};

    final byte[] header;

    Loading(byte[] header) {
        this.header = Arrays.copyOf(header, 0x20);
    }

    /** The six records, in SUFFIX order, for a portrait (RGBA, w x h). */
    byte[][] make(byte[] rgba, int w, int h) throws IOException {
        final byte[][] out = new byte[6][];
        for (int side = 0; side < 2; side++) {
            final byte[] main = fit(rgba, w, h, side == 1);
            out[side * 3] = encode(main);
            out[side * 3 + 1] = encode(silhouette(main, 49, 65, 90, 255, 4));  // the game's own outline navy
            out[side * 3 + 2] = encode(silhouette(main, 255, 255, 255, 255, 0));
        }
        return out;
    }

    private byte[] encode(byte[] rgba) {
        return Res.encodeCi8(header, new int[][] {Res.codes(rgba)}, new int[] {SIZE}, new int[] {SIZE});
    }

    /** loadres.fit: the picture whole, scaled (nearest) to fill SIZE less a margin, bottom-centred, on clear. */
    static byte[] fit(byte[] rgba, int w, int h, boolean mirror) {
        final int room = SIZE - 2 * MARGIN;
        final double s = Math.min(room / (double) w, room / (double) h);
        final int dw = Math.max(1, (int) (w * s)), dh = Math.max(1, (int) (h * s));
        final int ox = (SIZE - dw) / 2, oy = SIZE - MARGIN - dh;
        final byte[] out = new byte[SIZE * SIZE * 4];
        for (int y = 0; y < dh; y++) {
            final int sy = Math.min(h - 1, (int) (y / s));
            for (int x = 0; x < dw; x++) {
                int sx = Math.min(w - 1, (int) (x / s));
                if (mirror) sx = w - 1 - sx;
                final int ty = oy + y, tx = ox + x;
                if (ty < 0 || ty >= SIZE || tx < 0 || tx >= SIZE) continue;
                System.arraycopy(rgba, (sy * w + sx) * 4, out, (ty * SIZE + tx) * 4, 4);
            }
        }
        return out;
    }

    /** loadres.silhouette: the picture's shape (alpha > 127) in one colour, grown by `grow` px (a square). */
    static byte[] silhouette(byte[] rgba, int r, int g, int b, int a, int grow) {
        final int n = SIZE * SIZE;
        boolean[] mask = new boolean[n];
        for (int i = 0; i < n; i++) mask[i] = (rgba[i * 4 + 3] & 255) > 127;
        if (grow > 0) {
            final boolean[] row = new boolean[n];  // any of the row's mask within grow
            for (int y = 0; y < SIZE; y++) {
                int last = -1_000_000;  // the last masked x seen
                for (int x = 0; x < SIZE; x++) {
                    if (mask[y * SIZE + x]) last = x;
                    if (x - last <= grow) row[y * SIZE + x] = true;
                }
                last = 1_000_000;
                for (int x = SIZE - 1; x >= 0; x--) {
                    if (mask[y * SIZE + x]) last = x;
                    if (last - x <= grow) row[y * SIZE + x] = true;
                }
            }
            final boolean[] grown = new boolean[n];
            for (int x = 0; x < SIZE; x++) {
                int last = -1_000_000;
                for (int y = 0; y < SIZE; y++) {
                    if (row[y * SIZE + x]) last = y;
                    if (y - last <= grow) grown[y * SIZE + x] = true;
                }
                last = 1_000_000;
                for (int y = SIZE - 1; y >= 0; y--) {
                    if (row[y * SIZE + x]) last = y;
                    if (last - y <= grow) grown[y * SIZE + x] = true;
                }
            }
            for (int i = 0; i < n; i++) grown[i] |= mask[i];
            mask = grown;
        }
        final byte[] out = new byte[n * 4];
        for (int i = 0; i < n; i++)
            if (mask[i]) {
                out[i * 4] = (byte) r;
                out[i * 4 + 1] = (byte) g;
                out[i * 4 + 2] = (byte) b;
                out[i * 4 + 3] = (byte) a;
            }
        return out;
    }
}
