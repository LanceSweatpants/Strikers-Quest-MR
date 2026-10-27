package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * A HAL sysdolphin (HSD) archive: Melee's PlXxNr.dat, the dat inside a Colosseum .pkx (melee2obj.Dat). 0x20 header
 * (file size, data size, relocation count, root count, ref count), data at 0x20, then the relocation table, the roots
 * (offset, name offset) and their names. Offsets below are from the start of the data (as the file's pointers).
 */
final class Hsd {
    static final int D = 0x20;

    final byte[] b;
    final int dataSize;
    final List<String> rootNames = new ArrayList<>();
    final List<Integer> rootOffs = new ArrayList<>();

    Hsd(byte[] b) throws IOException {
        if (b.length < 0x20) throw new IOException("not an HSD archive");
        this.b = b;
        dataSize = Bin.u32(b, 4);
        final int nrel = Bin.u32(b, 8), nroot = Bin.u32(b, 12), nref = Bin.u32(b, 16);
        final int rel = D + dataSize;
        final int roots = rel + nrel * 4;
        final int strs = roots + (nroot + nref) * 8;
        if (dataSize < 0 || roots < 0 || strs > b.length) throw new IOException("bad HSD header");
        for (int i = 0; i < nroot; i++) {
            rootOffs.add(Bin.u32(b, roots + i * 8));
            rootNames.add(Bin.cstr(b, strs + Bin.u32(b, roots + i * 8 + 4)));
        }
    }

    int root(String name) {
        for (int i = 0; i < rootNames.size(); i++) if (rootNames.get(i).equals(name)) return rootOffs.get(i);
        return -1;
    }

    int rootEndingWith(String suffix) {
        for (int i = 0; i < rootNames.size(); i++) if (rootNames.get(i).endsWith(suffix)) return rootOffs.get(i);
        return -1;
    }

    int u32(int o) {
        return Bin.u32(b, D + o);
    }

    int u16(int o) {
        return Bin.u16(b, D + o);
    }

    int s16(int o) {
        return Bin.s16(b, D + o);
    }

    int u8(int o) {
        return b[D + o] & 0xFF;
    }

    int s8(int o) {
        return b[D + o];
    }

    double f32(int o) {
        return Bin.f32(b, D + o);
    }

    double[] f32(int o, int n) {
        final double[] v = new double[n];
        for (int i = 0; i < n; i++) v[i] = Bin.f32(b, D + o + 4 * i);
        return v;
    }

    /** Bytes in data space (a copy). */
    byte[] raw(int o, int n) {
        return Bin.slice(b, D + o, n);
    }

    /** The file length minus the header (Python: len(d.b) - D). */
    int dataLimit() {
        return b.length - D;
    }
}
