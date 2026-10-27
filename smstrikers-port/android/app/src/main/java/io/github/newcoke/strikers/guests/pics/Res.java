package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Out;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Strikers' .res picture bundles and their GX CI8 records (tools/outside/loadres.py): u32 header size (0x20),
 * u32 count, then from 0x20 entries of (u32 name hash, u32 offset / 32, u32 size). Record: GXTextureHeader (0x20:
 * u32 levels at 0, u16 w / h at 0x0E / 0x10, u32 palette count at 0x14) + 8 x 4-tiled CI8 + RGB5A3 palette.
 */
final class Res {
    private Res() {}

    static final class Rec {
        final int hash;
        final byte[] b;

        Rec(int hash, byte[] b) {
            this.hash = hash;
            this.b = b;
        }
    }

    /** loadres.name_hash (nlStringHash of the entry's path). */
    static int nameHash(String s) {
        int h = 0xFFFFFFFF;
        for (byte c : s.getBytes(StandardCharsets.UTF_8)) h = h + (h << 5) + (c & 0xFF);
        return h;
    }

    static List<Rec> records(byte[] b) {
        final int n = Bin.u32(b, 4);
        List<Rec> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) {
            final int e = 0x20 + 12 * i;
            final long off = Bin.u32L(b, e + 4) * 32, size = Bin.u32L(b, e + 8);
            final int from = (int) Math.min(off, b.length), to = (int) Math.min(off + size, b.length);
            out.add(new Rec(Bin.u32(b, e), Arrays.copyOfRange(b, from, to)));
        }
        return out;
    }

    static Rec find(List<Rec> recs, int hash) {
        for (Rec r : recs) if (r.hash == hash) return r;
        return null;
    }

    // ---- RGB5A3 as loadres.py reads / writes it (its own rounding: r * 255 // 31, a * 255 // 7) ----

    /** loadres.rgb5a3 as 0xAARRGGBB. */
    static int rgb5a3(int v) {
        if ((v & 0x8000) != 0) {
            final int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            return (255 << 24) | ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
        }
        final int a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        return ((a * 255 / 7) << 24) | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
    }

    static int toRgb5a3(int r, int g, int b, int a) {
        if (a >= 0xE0) return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
        return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
    }

    /**
     * Level 0 of a CI8 record whose palette follows all its levels -> RGBA (w*h*4), loadres.py's colours (the
     * loading pictures have 1 level: loadres.decode_ci8; the cup ones 6: as iconres.decode reads them).
     */
    static byte[] decodeCi8(byte[] rec, int[] wh) {
        final int w = Bin.u16(rec, 0x0E), h = Bin.u16(rec, 0x10);
        final int levels = Math.max(1, Bin.u32(rec, 0));
        int total = 0;
        for (int k = 0; k < levels; k++) total += (w >> k) * (h >> k);
        final int npal = Bin.u32(rec, 0x14);
        final int[] pal = new int[npal];
        for (int i = 0; i < npal; i++) pal[i] = rgb5a3(Bin.u16(rec, 0x20 + total + 2 * i));
        final byte[] px = new byte[w * h * 4];
        int i = 0x20;
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++) {
                        final int k = rec[i++] & 0xFF;
                        final int c = k < npal ? pal[k] : 0;
                        final int o = ((ty + y) * w + tx + x) * 4;
                        px[o] = (byte) (c >> 16);
                        px[o + 1] = (byte) (c >> 8);
                        px[o + 2] = (byte) c;
                        px[o + 3] = (byte) (c >>> 24);
                    }
        wh[0] = w;
        wh[1] = h;
        return px;
    }

    /**
     * loadres.encode_ci8 / iconres.encode: levels of RGB5A3 codes (row-major, each w[l] x h[l]) -> header[:0x20] +
     * 8 x 4-tiled CI8 of every level + 256 RGB5A3 palette entries. Palette: clear first, then the codes most used
     * in level 0 (ties: the first seen first), the rest to the nearest of those.
     */
    static byte[] encodeCi8(byte[] header, int[][] codes, int[] w, int[] h) {
        final int clear = toRgb5a3(0, 0, 0, 0);
        final int[] count = new int[65536];
        final int[] first = new int[65536];
        final int[] seen = new int[65536];
        int nSeen = 0;
        final int[] c0 = codes[0];
        for (int i = 0; i < c0.length; i++) {
            final int c = c0[i];
            if (count[c]++ == 0) {
                first[c] = i;
                seen[nSeen++] = c;
            }
        }
        // sorted by -count (stable: dict order = first seen), clear left out, the first 255
        Integer[] order = new Integer[nSeen];
        for (int i = 0; i < nSeen; i++) order[i] = seen[i];
        Arrays.sort(order, (a, b) -> count[a] != count[b] ? Integer.compare(count[b], count[a])
                : Integer.compare(first[a], first[b]));
        final int[] pal = new int[256];
        int np = 0;
        pal[np++] = clear;
        for (int i = 0; i < nSeen && np < 256; i++) if (order[i] != clear) pal[np++] = order[i];
        final int[] index = new int[65536];
        Arrays.fill(index, -1);
        for (int k = np - 1; k >= 0; k--) index[pal[k]] = k;
        final int[] palR = new int[np], palG = new int[np], palB = new int[np], palA = new int[np];
        for (int k = 0; k < np; k++) {
            final int c = rgb5a3(pal[k]);
            palR[k] = (c >> 16) & 255;
            palG[k] = (c >> 8) & 255;
            palB[k] = c & 255;
            palA[k] = c >>> 24;
        }
        int total = 0;
        for (int l = 0; l < codes.length; l++) total += w[l] * h[l];
        final Out o = new Out();
        o.bytes(header, 0, 0x20);
        final byte[] data = new byte[total];
        int d = 0;
        for (int l = 0; l < codes.length; l++) {
            final int[] cl = codes[l];
            final int wl = w[l], hl = h[l];
            for (int ty = 0; ty < hl; ty += 4)
                for (int tx = 0; tx < wl; tx += 8)
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 8; x++) {
                            final int c = cl[(ty + y) * wl + tx + x];
                            int k = index[c];
                            if (k < 0) {
                                final int v = rgb5a3(c);
                                final int r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255, a = v >>> 24;
                                int best = 0;
                                long bestD = Long.MAX_VALUE;
                                for (int j = 0; j < np; j++) {
                                    final int dr = palR[j] - r, dg = palG[j] - g, db = palB[j] - b, da = palA[j] - a;
                                    final long dist = (long) dr * dr + dg * dg + db * db + 2L * da * da;
                                    if (dist < bestD) {
                                        bestD = dist;
                                        best = j;
                                    }
                                }
                                k = best;
                                index[c] = k;  // (a nearest-colour cache: never one of the palette's own codes)
                            }
                            data[d++] = (byte) k;
                        }
        }
        o.bytes(data);
        for (int k = 0; k < 256; k++) o.be16(k < np ? pal[k] : clear);
        return o.toBytes();
    }

    /** RGBA bytes -> RGB5A3 codes. */
    static int[] codes(byte[] rgba) {
        final int[] c = new int[rgba.length / 4];
        for (int i = 0; i < c.length; i++)
            c[i] = toRgb5a3(rgba[i * 4] & 255, rgba[i * 4 + 1] & 255, rgba[i * 4 + 2] & 255, rgba[i * 4 + 3] & 255);
        return c;
    }
}
