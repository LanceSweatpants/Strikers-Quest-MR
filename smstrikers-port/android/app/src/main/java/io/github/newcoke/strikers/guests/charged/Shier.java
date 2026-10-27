package io.github.newcoke.strikers.guests.charged;

import io.github.newcoke.strikers.guests.Bin;

import java.util.ArrayList;

/**
 * A Charged NL skeleton (.shier; tools/outside/shier.py read(path, charged=True)): node ids (nlStringLowerHash of
 * the biped bone names), parents and bind translations.
 */
final class Shier {
    final int n;
    final int[] ids, parents;
    /** The translations part (n x 3 big-endian floats) and where it starts, or null when there is none. */
    final byte[] trans;

    private Shier(int n, int[] ids, int[] parents, byte[] trans) {
        this.n = n;
        this.ids = ids;
        this.parents = parents;
        this.trans = trans;
    }

    static Shier read(byte[] b) {
        final Chunks.C top = Chunks.first(b, 0, b.length);
        final ArrayList<Chunks.C> cs = Chunks.list(b, top.ds, top.end);
        final byte[][] parts = new byte[cs.size()][];
        for (int i = 0; i < parts.length; i++) parts[i] = cs.get(i).data(b);
        final long nl = Bin.u32L(parts[0], 8);
        if (nl > 4096) throw new IllegalArgumentException("shier: " + nl + " nodes");
        final int n = (int) nl;
        final int[] ids = new int[n], parents = new int[n];
        for (int i = 0; i < n; i++) {
            ids[i] = Bin.u32(parts[2], i * 4);
            parents[i] = Bin.u32(parts[3], i * 4);
        }
        // the last part of n x 12 bytes (as shier.py: the translation offsets)
        byte[] trans = null;
        for (byte[] p : parts) if (p.length == n * 12) trans = p;
        return new Shier(n, ids, parents, trans);
    }
}
