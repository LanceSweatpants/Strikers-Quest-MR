package io.github.newcoke.strikers.guests.charged;

import io.github.newcoke.strikers.guests.Bin;

import java.util.ArrayList;

/**
 * Mario Strikers Charged's NL chunks (nlChunk.h; tools/outside/rlgdump.py chunks): big-endian u32 id, u32 size; the
 * data starts at +8 aligned up to 1 << ((id >> 24) & 15), the next chunk follows the size padded to 4. A chunk's
 * data runs to +8 + size (as the Python tools slice it).
 */
final class Chunks {
    private Chunks() {}

    /** One chunk: id (& 0x80FFFFFF), where it starts, its data start and end (end may pass the buffer). */
    static final class C {
        final int id, at, ds, end;

        C(int id, int at, int ds, int end) {
            this.id = id;
            this.at = at;
            this.ds = ds;
            this.end = end;
        }

        /** The data as the Python tools slice it (b[ds:end], clamped to the buffer). */
        byte[] data(byte[] b) {
            return slice(b, ds, end);
        }
    }

    static ArrayList<C> list(byte[] b, int start, int end) {
        final ArrayList<C> out = new ArrayList<>();
        long p = start;
        while (p + 8 <= end) {
            final int cid = Bin.u32(b, (int) p);
            final long size = Bin.u32L(b, (int) p + 4);
            final int a = (cid & 0x0F000000) >>> 24;
            final long ds = a != 0 ? ((p + 8 + (1L << a) - 1) & ~((1L << a) - 1)) : p + 8;
            long nxt = p + 8 + size;
            nxt += (-nxt) & 3;
            out.add(new C(cid & 0x80FFFFFF, (int) p, clampInt(ds), clampInt(p + 8 + size)));
            p = nxt;
        }
        return out;
    }

    static C first(byte[] b, int start, int end) {
        final ArrayList<C> l = list(b, start, end);
        if (l.isEmpty()) throw new IllegalArgumentException("no chunk");
        return l.get(0);
    }

    private static int clampInt(long v) {
        return (int) Math.min(v, Integer.MAX_VALUE);
    }

    /** Python's b[from:to] for 0 <= from, to: clamped to the buffer, empty when to <= from. */
    static byte[] slice(byte[] b, long from, long to) {
        final int f = (int) Math.min(from, b.length), t = (int) Math.min(to, b.length);
        if (t <= f) return new byte[0];
        return Bin.slice(b, f, t - f);
    }
}
