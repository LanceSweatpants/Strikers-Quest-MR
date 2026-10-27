package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.GxTex;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A guest standing posed, as the PC tools wrote it to OBJ for Blender (retargetsim.py main / gpk2obj.py): skinned
 * positions and normals, UVs, the strips as triangles (every other one turned), one material per packet.
 */
public final class Mesh {
    public int nv, nt;
    public float[] p, n, uv;
    public int[] tri, triMat;
    public final List<Material> mats = new ArrayList<>();

    public static final class Material {
        public byte[] rgba;  // null: no picture (Blender's Kd 1 1 1: white)
        public int w, h;
        public boolean alpha;  // some texel not opaque
        public boolean soft;
        public int flameFrame = -1;
    }

    /** ENGINE_FMT (the record header's engine format -> GX format). */
    static final int[] ENGINE_FMT = {4, 5, 14, 6, 1, 0, 1, 3, 9};

    /** A texture record (GX header + data) as RGBA, or null when it can't be read (retargetsim: no map_Kd). */
    static Material material(Gpk pack, int hash, Map<Integer, Material> cache) {
        final Material cached = cache.get(hash);
        if (cached != null) return cached;
        final Material m = new Material();
        final int[] rec = pack.record(hash);
        if (rec != null && rec[1] >= 0x20) {
            final byte[] b = pack.b;
            final int o = rec[0];
            final int efmt = Bin.u32(b, o + 4), w = Bin.u16(b, o + 0x0E), h = Bin.u16(b, o + 0x10);
            if (efmt >= 0 && efmt < ENGINE_FMT.length && ENGINE_FMT[efmt] != 9 && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                final int fmt = ENGINE_FMT[efmt];
                if (0x20 + GxTex.sizeOf(fmt, w, h) <= rec[1] && o + 0x20 + GxTex.sizeOf(fmt, w, h) <= b.length) {
                    m.rgba = GxTex.decode(b, o + 0x20, fmt, w, h, null);
                    m.w = w;
                    m.h = h;
                    for (int i = 3; i < m.rgba.length; i += 4)
                        if ((m.rgba[i] & 0xFF) != 255) {
                            m.alpha = true;
                            break;
                        }
                }
            }
        }
        cache.put(hash, m);
        return m;
    }

    /**
     * retargetsim's CPU skin: world = each node's world matrix (rows), skin = inverse bind x world; positions the
     * weighted sum over the weights (no weight: the root's world), normals turned the same way and normalised.
     */
    public static Mesh skinned(Gpk pack, double[][][] world, boolean withFlames) {
        final int nn = pack.nodes.length;
        final float[][] skin = new float[nn][];
        for (int i = 0; i < nn; i++) {
            final double[][] s = Py.mul(pack.nodes[i].inv, world[i]);
            skin[i] = flat(s);
        }
        final float[] root = flat(world[0]);
        return build(pack, (v, pk, out) -> {
            double px = 0, py = 0, pz = 0, tw = 0, nx = 0, ny = 0, nz = 0;
            final float x = pk.pos[v * 3], y = pk.pos[v * 3 + 1], z = pk.pos[v * 3 + 2];
            final float ax = pk.nrm[v * 3], ay = pk.nrm[v * 3 + 1], az = pk.nrm[v * 3 + 2];
            for (int k = 0; k < 4; k++) {
                final float wgt = pk.w[v * 4 + k];
                final int nd = pk.node[v * 4 + k];
                if (wgt <= 0 || nd >= nn) continue;
                final float[] m = skin[nd];
                px += wgt * (x * m[0] + y * m[4] + z * m[8] + m[12]);
                py += wgt * (x * m[1] + y * m[5] + z * m[9] + m[13]);
                pz += wgt * (x * m[2] + y * m[6] + z * m[10] + m[14]);
                nx += wgt * (ax * m[0] + ay * m[4] + az * m[8]);
                ny += wgt * (ax * m[1] + ay * m[5] + az * m[9]);
                nz += wgt * (ax * m[2] + ay * m[6] + az * m[10]);
                tw += wgt;
            }
            if (tw <= 0) {
                final float[] m = root;
                px = x * m[0] + y * m[4] + z * m[8] + m[12];
                py = x * m[1] + y * m[5] + z * m[9] + m[13];
                pz = x * m[2] + y * m[6] + z * m[10] + m[14];
                tw = 1;
            }
            out[0] = (float) (px / tw);
            out[1] = (float) (py / tw);
            out[2] = (float) (pz / tw);
            double ln = Math.sqrt(nx * nx + ny * ny + nz * nz);
            if (ln == 0) ln = 1;
            out[3] = (float) (nx / ln);
            out[4] = (float) (ny / ln);
            out[5] = (float) (nz / ln);
        }, withFlames);
    }

    /** gpk2obj.py: the pack as it lies (its bind pose; Strikers space, facing -Y), normals as stored. */
    public static Mesh bind(Gpk pack) {
        return build(pack, (v, pk, out) -> {
            for (int c = 0; c < 3; c++) {
                out[c] = pk.pos[v * 3 + c];
                out[3 + c] = pk.nrm[v * 3 + c];
            }
        }, false);
    }

    /**
     * gpk2obj.py --anim (own-clip packs, GPK1: Charged): posed by its own clip at t (the key int(t (keys - 1) +
     * 0.5)), the root where it stands; a vertex with no weight lands on the origin, as there.
     */
    public static Mesh ownClip(Gpk pack, String anim, double t) {
        final Gpk.Anim a = pack.anim(anim);
        if (a == null) return null;
        final int nn = pack.nodes.length;
        final int k = Math.min((int) (t * (a.keys - 1) + 0.5), a.keys - 1);
        final double[][][] world = new double[nn][][];
        final float[][] skin = new float[nn][];
        for (int i = 0; i < nn; i++) {
            final Gpk.Node nd = pack.nodes[i];
            final int at = a.at + (k * nn + i) * 28;
            final double[] v = new double[7];
            for (int c = 0; c < 7; c++) v[c] = Bin.lef32(pack.b, at + 4 * c);
            final double[][] local = Py.quatRows(v[0], v[1], v[2], v[3]);
            local[3] = a.hasTrans[i] ? new double[] {v[4], v[5], v[6], 1.0} : new double[] {nd.trans[0], nd.trans[1], nd.trans[2], 1.0};
            final int par = nd.parent;
            world[i] = par < 0 || par >= i ? Py.ident() : Py.mul(local, world[par]);
            skin[i] = flat(nd.hasInv ? Py.mul(nd.inv, world[i]) : world[i]);
        }
        return build(pack, (v, pk, out) -> {
            double px = 0, py = 0, pz = 0, tw = 0;
            final float x = pk.pos[v * 3], y = pk.pos[v * 3 + 1], z = pk.pos[v * 3 + 2];
            for (int q = 0; q < 4; q++) {
                final float wgt = pk.w[v * 4 + q];
                final int nd = pk.node[v * 4 + q];
                if (wgt <= 0 || nd >= nn) continue;
                final float[] m = skin[nd];
                px += wgt * (x * m[0] + y * m[4] + z * m[8] + m[12]);
                py += wgt * (x * m[1] + y * m[5] + z * m[9] + m[13]);
                pz += wgt * (x * m[2] + y * m[6] + z * m[10] + m[14]);
                tw += wgt;
            }
            if (tw == 0) tw = 1;
            out[0] = (float) (px / tw);
            out[1] = (float) (py / tw);
            out[2] = (float) (pz / tw);
            for (int c = 0; c < 3; c++) out[3 + c] = pk.nrm[v * 3 + c];
        }, false);
    }

    interface VertexFn {
        void at(int v, Gpk.Packet pk, float[] out);
    }

    private static float[] flat(double[][] m) {
        final float[] f = new float[16];
        for (int i = 0; i < 16; i++) f[i] = (float) m[i / 4][i % 4];
        return f;
    }

    private static Mesh build(Gpk pack, VertexFn fn, boolean withFlames) {
        final Mesh me = new Mesh();
        int nv = 0, nt = 0;
        for (Gpk.Packet pk : pack.packets) {
            nv += pk.nv;
            nt += Math.max(0, pk.strip.length - 2);
        }
        me.p = new float[nv * 3];
        me.n = new float[nv * 3];
        me.uv = new float[nv * 2];
        final int[] tri = new int[nt * 3], triMat = new int[nt];
        final Map<Integer, Material> cache = new HashMap<>();
        final float[] o = new float[6];
        int base = 0, t = 0;
        for (Gpk.Packet pk : pack.packets) {
            if (pk.flameFrame() >= 0 && (!withFlames || pk.flameFrame() != 0)) continue;  // one frame of the flipbook
            for (int v = 0; v < pk.nv; v++) {
                fn.at(v, pk, o);
                final int d = base + v;
                for (int c = 0; c < 3; c++) {
                    me.p[d * 3 + c] = o[c];
                    me.n[d * 3 + c] = o[3 + c];
                }
                me.uv[d * 2] = pk.uv[v * 2];
                me.uv[d * 2 + 1] = pk.uv[v * 2 + 1];
            }
            final Material base0 = material(pack, pk.texHash, cache);
            Material mat = base0;
            if (pk.soft() || pk.flameFrame() >= 0) {  // its own entry: drawn blended
                mat = new Material();
                mat.rgba = base0.rgba;
                mat.w = base0.w;
                mat.h = base0.h;
                mat.alpha = true;
                mat.soft = true;
                mat.flameFrame = pk.flameFrame();
            }
            int mi = me.mats.indexOf(mat);
            if (mi < 0) {
                mi = me.mats.size();
                me.mats.add(mat);
            }
            final int[] s = pk.strip;
            for (int k = 0; k + 2 < s.length; k++) {
                int a = s[k], c = s[k + 1], d = s[k + 2];
                if (a == c || c == d || a == d) continue;
                if (a >= pk.nv || c >= pk.nv || d >= pk.nv) continue;
                if ((k & 1) == 1) {
                    final int x = a;
                    a = c;
                    c = x;
                }
                tri[t * 3] = base + a;
                tri[t * 3 + 1] = base + c;
                tri[t * 3 + 2] = base + d;
                triMat[t] = mi;
                t++;
            }
            base += pk.nv;
        }
        me.nv = base;
        me.nt = t;
        me.tri = tri;
        me.triMat = triMat;
        return me;
    }
}
