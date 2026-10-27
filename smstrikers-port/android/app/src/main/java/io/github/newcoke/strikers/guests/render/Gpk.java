package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * A guest pack as the game reads it (xr_guest.cpp load_pack; tools/outside/gpk2obj.py, retargetsim.read_pack):
 * GPK1 (own clips: Charged) or GPK2 (retarget: Melee, Colosseum), little-endian.
 */
public final class Gpk {
    public final boolean v2;
    public final int mode;
    public final float height;
    public final String name;
    public final Node[] nodes;
    /** texture hash -> {offset, size} of its record (GX texture header + data) in {@link #b}. */
    public final Map<Integer, int[]> tex = new LinkedHashMap<>();
    public final List<Packet> packets = new ArrayList<>();
    public final List<Anim> anims = new ArrayList<>();
    public final byte[] b;

    public static final class Node {
        public int id, parent, map;
        public double[] trans = new double[3];
        public boolean hasInv;
        public double[][] inv, bindWorld, bindLocal;
    }

    public static final class Packet {
        public int texHash, flags, nv;
        /** per vertex: x y z, nx ny nz, u v */
        public float[] pos, nrm, uv;
        /** per vertex: 4 node indices, 4 weights */
        public int[] node;
        public float[] w;
        public int[] strip;

        /** r69 soft (blended, see-through: Gastly's gas) or r73 flame. */
        public boolean soft() {
            return (flags & 0xC) != 0;
        }

        public int flameFrame() {
            return (flags & 0x4) != 0 ? flags & 3 : -1;
        }
    }

    public static final class Anim {
        public String name;
        public int keys;
        public int at;  // offset of the key data in b (keys x nodes x 7 f32)
        public boolean[] hasTrans;
    }

    public Gpk(byte[] b) throws IOException {
        this.b = b;
        if (b.length < 64) throw new IOException("not a guest pack");
        final String magic = Nl.ascii(b, 0, 4);
        if (!magic.equals("GPK1") && !magic.equals("GPK2")) throw new IOException("not a guest pack: " + magic);
        v2 = magic.equals("GPK2");
        final int nn = Bin.le32(b, 8), npk = Bin.le32(b, 12), nt = Bin.le32(b, 16), na = Bin.le32(b, 20);
        height = Bin.lef32(b, 24);
        name = Bin.cstr(b, 28, 32);
        mode = v2 ? Bin.le32(b, 60) : 0;
        int p = v2 ? 64 : 60;
        nodes = new Node[nn];
        for (int i = 0; i < nn; i++) {
            final Node n = new Node();
            n.id = Bin.le32(b, p);
            n.parent = Bin.le32(b, p + 4);
            for (int k = 0; k < 3; k++) n.trans[k] = Bin.lef32(b, p + 8 + 4 * k);
            n.hasInv = Bin.le32(b, p + 20) != 0;
            n.inv = rows(b, p + 24);
            if (v2) {
                n.map = Bin.le32(b, p + 88);
                n.bindWorld = rows(b, p + 92);
            }
            nodes[i] = n;
            p += v2 ? 156 : 88;
        }
        for (int i = 0; i < nt; i++) {
            final int h = Bin.le32(b, p), sz = Bin.le32(b, p + 4);
            tex.put(h, new int[] {p + 8, sz});
            p += 8 + ((sz + 3) & ~3);
        }
        for (int i = 0; i < npk; i++) {
            final Packet k = new Packet();
            k.texHash = Bin.le32(b, p);
            final int nv0 = Bin.le32(b, p + 4);
            k.flags = nv0 >>> 28;
            final int nv = nv0 & 0x0FFFFFFF, ni = Bin.le32(b, p + 8);
            p += 12;
            k.nv = nv;
            k.pos = new float[nv * 3];
            k.nrm = new float[nv * 3];
            k.uv = new float[nv * 2];
            k.node = new int[nv * 4];
            k.w = new float[nv * 4];
            for (int v = 0; v < nv; v++, p += 52) {
                for (int c = 0; c < 3; c++) {
                    k.pos[v * 3 + c] = Bin.lef32(b, p + 4 * c);
                    k.nrm[v * 3 + c] = Bin.lef32(b, p + 12 + 4 * c);
                }
                k.uv[v * 2] = Bin.lef32(b, p + 24);
                k.uv[v * 2 + 1] = Bin.lef32(b, p + 28);
                for (int c = 0; c < 4; c++) {
                    k.node[v * 4 + c] = b[p + 32 + c] & 0xFF;
                    k.w[v * 4 + c] = Bin.lef32(b, p + 36 + 4 * c);
                }
            }
            k.strip = new int[ni];
            for (int s = 0; s < ni; s++) k.strip[s] = Bin.le16(b, p + 2 * s);
            p += (ni * 2 + 3) & ~3;
            packets.add(k);
        }
        for (int a = 0; a < na && p + 52 <= b.length; a++) {
            final Anim an = new Anim();
            an.name = Bin.cstr(b, p, 48);
            an.keys = Bin.le32(b, p + 48);
            an.at = p + 52;
            p = an.at + an.keys * nn * 28;
            an.hasTrans = new boolean[nn];
            for (int i = 0; i < nn; i++) an.hasTrans[i] = b[p + i] != 0;
            p += (nn + 3) & ~3;
            anims.add(an);
        }
        if (v2) {
            // retargetsim.read_pack: a parent at or after the node is no parent; then the bind relative to it
            for (int i = 0; i < nn; i++) {
                final Node n = nodes[i];
                if (n.parent >= i) n.parent = -1;
                n.bindLocal = n.parent < 0 ? n.bindWorld : Py.mul(n.bindWorld, Py.inverse(nodes[n.parent].bindWorld));
            }
        }
    }

    private static double[][] rows(byte[] b, int o) {
        final double[][] m = new double[4][4];
        for (int i = 0; i < 16; i++) m[i / 4][i % 4] = Bin.lef32(b, o + 4 * i);
        return m;
    }

    public Anim anim(String n) {
        for (Anim a : anims)
            if (a.name.equalsIgnoreCase(n)) return a;
        return null;
    }

    /** The texture's record: GX header (big-endian: levels, engine format, ..., w at 0x0E, h at 0x10), data at 0x20. */
    public int[] record(int hash) {
        return tex.get(hash);
    }
}
