package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.Arrays;

/**
 * Genius Sonority FSYS archives (Pokemon Colosseum), tools/outside/fsys.py: entries, LZSS-compressed ones included
 * (Okumura LZSS: 4 KB ring, 18-byte matches, start 0xFEE, zero-filled).
 */
final class Fsys {
    private Fsys() {}

    static final int TYPE_PKX = 0x1E;

    /** The first entry of this file type, unpacked (null: none). */
    static byte[] first(byte[] b, int type) throws IOException {
        if (b.length < 0x44 || b[0] != 'F' || b[1] != 'S' || b[2] != 'Y' || b[3] != 'S') throw new IOException("not an FSYS archive");
        final int count = Bin.u32(b, 0x0C), table = Bin.u32(b, 0x40);
        for (int k = 0; k < count; k++) {
            final int e = Bin.u32(b, table + 4 * k);
            final int ident = Bin.u32(b, e), start = Bin.u32(b, e + 4), usize = Bin.u32(b, e + 8);
            if (((ident >> 8) & 0xFF) != type) continue;
            if (start + 4 <= b.length && b[start] == 'L' && b[start + 1] == 'Z' && b[start + 2] == 'S' && b[start + 3] == 'S') {
                final int dsize = Bin.u32(b, start + 4), csize = Bin.u32(b, start + 8);
                return lzss(b, start + 16, Math.min(b.length, start + csize), dsize);
            }
            return Arrays.copyOfRange(b, start, Math.min(b.length, start + usize));
        }
        return null;
    }

    /** fsys.lzss over src[from, to) -> size bytes (fewer if the data runs out). */
    static byte[] lzss(byte[] src, int from, int to, int size) {
        final byte[] out = new byte[Math.max(0, size) + 18];
        final byte[] ring = new byte[4096];
        int r = 0xFEE, i = from, n = 0, flags = 0;
        while (n < size && i < to) {
            flags >>= 1;
            if ((flags & 0x100) == 0) {
                flags = (src[i] & 0xFF) | 0xFF00;
                i++;
                if (i >= to) break;
            }
            if ((flags & 1) != 0) {
                final byte c = src[i++];
                out[n++] = c;
                ring[r] = c;
                r = (r + 1) & 0xFFF;
            } else {
                if (i + 1 >= to) break;
                final int a = src[i] & 0xFF, bb = src[i + 1] & 0xFF;
                i += 2;
                final int pos = a | ((bb & 0xF0) << 4), ln = (bb & 0x0F) + 3;
                for (int k = 0; k < ln; k++) {
                    final byte c = ring[(pos + k) & 0xFFF];
                    if (n < out.length) out[n] = c;
                    n++;
                    ring[r] = c;
                    r = (r + 1) & 0xFFF;
                }
            }
        }
        return Arrays.copyOf(out, Math.min(n, size));
    }
}
