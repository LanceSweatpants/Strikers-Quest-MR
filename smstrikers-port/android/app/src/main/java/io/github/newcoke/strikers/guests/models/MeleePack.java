package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * tools/outside/meleepack.py in Java: an HSD model (a Melee fighter costume PlXxNr.dat, or the dat inside a Colosseum
 * .pkx) -> a Strikers guest pack in retarget mode (GPK2). Same steps, same order, same numbers (see Py for the few
 * Python rules copied), so the pack comes out byte for byte as the PC tool's:
 * joints (SRT chain, or Colosseum's inverse binds) -> visible DObjs -> vertices with up to 4 joint weights ->
 * (Colosseum) the skin space that puts each part on its joints, gas cards to puffs -> sizes -> textures (texture
 * matrix, frame-0 texture moves, wrap modes baked, colour ops, alpha-mask layers) -> nodes with the bone map -> merge
 * by texture -> Y-up to Z-up (x, -z, y) and scale -> weld -> Loop subdivision -> smooth normals -> strips -> GPK2.
 */
final class MeleePack {
    /** meleepack's command line. */
    static final class Options {
        String name;
        int root = -1;                 // --root (data offset of the root joint), -1 = the *_joint root
        boolean modelSpaceEnv;         // --model-space-env (Colosseum)
        boolean showHidden;            // --show-hidden
        int subdivide;                 // --subdivide N
        int[] gasColor;                // --gas-color RRGGBB
        double gasOpacity = 1.0;       // --gas-opacity
        int matanim = -1;              // --matanim (data offset), -1 none
        double height = Double.NaN;    // --height
        double ground = Double.NaN;    // --ground
        Set<Integer> show;             // --visible: this fighter's "show" list (null: none)
        String upright;                // --upright x+90 | x-90 | z+90 | z-90 | yNN
        BoneMap map;                   // --map: this pack's entry (null: no map file)
        boolean texturesOnly;          // just the texture records (the shiny rare_ builds)
        List<Integer> retex;           // texture records "tex/<data>" made again from the picture as it is
    }

    /** One pack's bone-map entry (melee-bone-map.json / colo-bone-map.json, as shipped). */
    static final class BoneMap {
        final List<String> bones = new ArrayList<>();  // Strikers bone names, in the file's order
        final List<Integer> joints = new ArrayList<>(); // their joint indices
        String cls;                                     // _class
        int[] flapL = new int[0], flapR = new int[0];   // _flap_l / _flap_r
        String scaleHint;                               // _scale_hint (text), null none
    }

    /** The result: the pack, or (texturesOnly) the texture records in order. */
    static final class Result {
        byte[] gpk;
        final List<Integer> texHashes = new ArrayList<>();
        final List<byte[]> texRecords = new ArrayList<>();
    }

    private static final int VA_PNMTXIDX = 0, VA_POS = 9, VA_NRM = 10, VA_CLR0 = 11, VA_TEX0 = 13;
    private static final int TEX_COLORMAP_ALPHA_MASK = 1, GX_TG_TEX0 = 4;
    private static final byte RIGID = 0, RAW = 1, ENV = 2;

    private final Hsd d;
    private final Options o;
    private final String name;

    // joints (parents first), by index
    private final List<Integer> joints = new ArrayList<>();
    private final Map<Integer, Integer> index = new HashMap<>();
    private final List<Integer> parent = new ArrayList<>();      // parent index, -1 none
    private final List<double[][]> world = new ArrayList<>();
    private final List<double[][]> restWorld = new ArrayList<>();

    /** A vertex before the merge: position (Melee space), weights (joint index, weight), UV. */
    static final class Vx {
        double x, y, z;
        int[] wj;
        double[] ww;
        double u, v;

        Vx(double x, double y, double z, int[] wj, double[] ww, double u, double v) {
            this.x = x;
            this.y = y;
            this.z = z;
            this.wj = wj;
            this.ww = ww;
            this.u = u;
            this.v = v;
        }
    }

    /** A packet before the merge: one PObj's shape (or a baked eye layer: decalHash != 0 / isDecal). */
    static final class Pk {
        List<Vx> verts;
        int[] tris;            // triples
        int mobj;
        int[] vcol;            // null none
        boolean isDecal;
        int decalHash;
        double[][] uv1, uv2;   // TEX1 / TEX2 per vertex (null where none)
        byte[] kind;           // recipe per vertex
        double[] raw;          // stored position per vertex (3 each)
        int[] rj;              // recipe joint per vertex

        int ntris() {
            return tris.length / 3;
        }
    }

    private final List<Pk> packets = new ArrayList<>();
    private final Map<Integer, Integer> billboard = new LinkedHashMap<>();  // packet index -> joint index
    private final Map<Integer, Map<Integer, Map<Integer, Double>>> texFrame0 = new HashMap<>();
    private final Tex.Cache texd;

    private MeleePack(Hsd d, Options o) {
        this.d = d;
        this.o = o;
        this.name = o.name;
        this.texd = new Tex.Cache(d);
    }

    static Result run(byte[] dat, Options o) throws IOException {
        final Hsd d = new Hsd(dat);
        try {
            return new MeleePack(d, o).make();
        } catch (IndexOutOfBoundsException e) {
            throw new IOException("bad model file (" + e.getMessage() + ")");
        }
    }

    // ------------------------------------------------------------------------------------------------ joints
    private void walk(int j, int par, double[][] pw, double[][] pr) {
        while (j != 0) {
            final double[] s = d.f32(j + 0x20, 3), r = d.f32(j + 0x14, 3), t = d.f32(j + 0x2C, 3);
            double[][] w = M4.mul(pw, M4.srt(s, r, t));
            final double[][] rest = M4.mul(pr, M4.srt(s, r, t));
            final int ib = o.modelSpaceEnv ? d.u32(j + 0x38) : 0;
            if (ib != 0) w = M4.inv(ibRows(ib));
            final int k = joints.size();
            joints.add(j);
            index.put(j, k);
            parent.add(par);
            world.add(w);
            restWorld.add(rest);
            final int fl = d.u32(j + 0x04), ch = d.u32(j + 0x08);
            if (ch != 0 && (fl & 0x1000) == 0) walk(ch, k, w, rest);
            j = d.u32(j + 0x0C);
        }
    }

    private double[][] ibRows(int ib) {
        final double[] m = d.f32(ib, 12);
        return new double[][] {{m[0], m[1], m[2], m[3]}, {m[4], m[5], m[6], m[7]}, {m[8], m[9], m[10], m[11]},
            {0.0, 0.0, 0.0, 1.0}};
    }

    // ------------------------------------------------------------------------------------------------ texanim
    private Double fobjFirst(int fobj) {
        final int ln = d.u32(fobj + 0x04), frac = d.u8(fobj + 0x0D);
        int p = d.u32(fobj + 0x10);
        if (ln == 0 || p == 0) return null;
        final int op = d.u8(p) & 0x0F;
        while ((d.u8(p) & 0x80) != 0) p++;
        p++;
        if (op == 5 || op == 0) return null;
        final int kind = frac & 0xE0;
        final double scale = (double) (1L << (frac & 0x1F));
        if (kind == 0x00) return d.f32(p);
        if (kind == 0x20) return d.s16(p) / scale;
        if (kind == 0x40) return d.u16(p) / scale;
        if (kind == 0x60) return d.s8(p) / scale;
        return d.u8(p) / scale;
    }

    private void texanimWalk(int j, int m) {
        while (j != 0 && m != 0) {
            int dobj = d.u32(j + 0x10), ma = d.u32(m + 0x08);
            while (dobj != 0 && ma != 0) {
                int ta = d.u32(ma + 0x08);
                while (ta != 0) {
                    final int aobj = d.u32(ta + 0x08);
                    int f = aobj != 0 ? d.u32(aobj + 0x08) : 0;
                    while (f != 0) {
                        final Double v = fobjFirst(f);
                        if (v != null)
                            texFrame0.computeIfAbsent(d.u32(dobj + 0x08), k -> new HashMap<>())
                                    .computeIfAbsent(d.u32(ta + 0x04), k -> new HashMap<>()).put(d.u8(f + 0x0C), v);
                        f = d.u32(f);
                    }
                    ta = d.u32(ta);
                }
                dobj = d.u32(dobj + 0x04);
                ma = d.u32(ma);
            }
            if ((d.u32(j + 0x04) & 0x1000) == 0) texanimWalk(d.u32(j + 0x08), d.u32(m));
            j = d.u32(j + 0x0C);
            m = d.u32(m + 0x04);
        }
    }

    // ------------------------------------------------------------------------------------------------ shapes
    private static double[] readColour(byte[] b, int off, int ctype) {
        // only for the average vertex colour (ints 0-255)
        final int o = off;
        if (ctype == 0) {
            final int v = ((b[o] & 0xFF) << 8) | (b[o + 1] & 0xFF);
            return new double[] {(v >> 11) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
        }
        if (ctype == 1 || ctype == 2 || ctype == 5) return new double[] {b[o] & 0xFF, b[o + 1] & 0xFF, b[o + 2] & 0xFF};
        if (ctype == 3) return new double[] {((b[o] & 0xFF) >> 4) * 17, (b[o] & 15) * 17, ((b[o + 1] & 0xFF) >> 4) * 17};
        final int v = ((b[o] & 0xFF) << 16) | ((b[o + 1] & 0xFF) << 8) | (b[o + 2] & 0xFF);
        return new double[] {(v >> 18) * 255 / 63, ((v >> 12) & 63) * 255 / 63, ((v >> 6) & 63) * 255 / 63};
    }

    private static final int[] CLR_SIZE = {2, 3, 4, 2, 3, 4};
    private static final int[] COMP_SIZE = {1, 1, 2, 2, 4};

    private double[] readComp(int off, int ctype, int n, int frac) throws IOException {
        if (ctype < 0 || ctype > 4) throw new IOException("vertex component type " + ctype);
        final double[] v = new double[n];
        final double sc = (double) (1L << frac);
        for (int i = 0; i < n; i++) {
            switch (ctype) {
                case 0: v[i] = d.u8(off + i) / sc; break;
                case 1: v[i] = d.s8(off + i) / sc; break;
                case 2: v[i] = d.u16(off + 2 * i) / sc; break;
                case 3: v[i] = d.s16(off + 2 * i) / sc; break;
                default: v[i] = d.f32(off + 4 * i);
            }
        }
        return v;
    }

    /** One vertex as read: attribute -> values (only the ones used). */
    private static final class RawV {
        int pnmtx = -1;
        double[] pos, tex0, tex1, tex2;
        double[] clr;
    }

    private void readShapes() throws IOException {
        int dobjNo = -1;
        for (int ji = 0; ji < joints.size(); ji++) {
            final int j = joints.get(ji);
            final int fl = d.u32(j + 0x04);
            int dobj = d.u32(j + 0x10);
            if (o.show == null && (fl & (0x4000 | 0x20 | (o.showHidden ? 0 : 0x10))) != 0) {
                while (dobj != 0 && dobj + 8 <= d.dataLimit()) {
                    dobjNo++;
                    dobj = d.u32(dobj + 0x04);
                }
                continue;
            }
            while (dobj != 0) {
                dobjNo++;
                if (o.show != null && !o.show.contains(dobjNo)) {
                    dobj = d.u32(dobj + 0x04);
                    continue;
                }
                final int mobj = d.u32(dobj + 0x08);
                int pobj = d.u32(dobj + 0x0C);
                while (pobj != 0) {
                    readPobj(pobj, ji, fl, mobj);
                    pobj = d.u32(pobj + 0x04);
                }
                dobj = d.u32(dobj + 0x04);
            }
        }
    }

    private void readPobj(int pobj, int ji, int fl, int mobj) throws IOException {
        final int j = joints.get(ji);
        final List<int[]> attrs = new ArrayList<>();
        int a = d.u32(pobj + 0x08);
        while (true) {
            final int at = d.u32(a);
            if (at == 0xFF) break;
            attrs.add(new int[] {at, d.u32(a + 4), d.u32(a + 8), d.u32(a + 12), d.u8(a + 16), d.u16(a + 18), d.u32(a + 20)});
            a += 0x18;
        }
        final int pflags = d.u16(pobj + 0x0C);
        final int dlSize = d.u16(pobj + 0x0E) * 32;
        final int dl = d.u32(pobj + 0x10);
        final int kind = pflags & 0x3000;
        final int link = d.u32(pobj + 0x14);
        final List<int[]> envJ = new ArrayList<>();       // per envelope: joint offsets
        final List<double[]> envW = new ArrayList<>();
        if (kind == 0x2000 && link != 0) {
            for (int k = 0; ; k++) {
                final int e = d.u32(link + k * 4);
                if (e == 0) break;
                int q = 0;
                while (d.u32(e + q * 8) != 0) q++;
                final int[] jj = new int[q];
                final double[] ww = new double[q];
                for (int i = 0; i < q; i++) {
                    jj[i] = d.u32(e + i * 8);
                    ww[i] = d.f32(e + i * 8 + 4);
                }
                envJ.add(jj);
                envW.add(ww);
            }
        }
        final int bone = (kind == 0 && link != 0 && index.containsKey(link)) ? index.get(link) : ji;
        final List<RawV> verts = new ArrayList<>();
        final List<int[]> trisL = new ArrayList<>();
        int p = dl;
        final int end = dl + dlSize;
        while (p < end) {
            final int op = d.u8(p);
            if (op == 0) {
                p++;
                continue;
            }
            final int prim = op & 0xF8;
            if (prim != 0x80 && prim != 0x90 && prim != 0x98 && prim != 0xA0) break;
            final int n = d.u16(p + 1);
            p += 3;
            final int first = verts.size();
            for (int vi = 0; vi < n; vi++) {
                final RawV v = new RawV();
                for (int[] at : attrs) {
                    final int an = at[0], atype = at[1], cnt = at[2], ctype = at[3], frac = at[4], stride = at[5],
                            data = at[6];
                    if (atype == 1) {
                        if (an == VA_PNMTXIDX || (an >= 1 && an <= 8)) {
                            if (an == VA_PNMTXIDX) v.pnmtx = d.u8(p);
                            p += 1;
                        } else if (an == VA_CLR0 || an == VA_CLR0 + 1) {
                            if (ctype < 0 || ctype > 5) throw new IOException("colour type " + ctype);
                            if (an == VA_CLR0) v.clr = readColour(d.b, Hsd.D + p, ctype);
                            p += CLR_SIZE[ctype];
                        } else {
                            final int comps = an == VA_POS ? 2 + cnt : an == VA_NRM ? 3 : 1 + cnt;
                            final double[] c = readComp(p, ctype, comps, frac);
                            put(v, an, c);
                            p += COMP_SIZE[ctype] * comps;
                        }
                    } else if (atype == 2 || atype == 3) {
                        final int ii = atype == 2 ? d.u8(p) : d.u16(p);
                        p += atype == 2 ? 1 : 2;
                        if (an == VA_CLR0) {
                            if (ctype < 0 || ctype > 5) throw new IOException("colour type " + ctype);
                            v.clr = readColour(d.b, Hsd.D + data + ii * stride, ctype);
                        } else if (an == VA_POS || an == VA_NRM || an >= VA_TEX0) {
                            final int comps = an == VA_POS ? 2 + cnt : an == VA_NRM ? 3 : 1 + cnt;
                            put(v, an, readComp(data + ii * stride, ctype, comps, frac));
                        }
                    }
                }
                verts.add(v);
            }
            final int cnt = verts.size() - first;
            if (prim == 0x90) {
                for (int k = 0; k < cnt - 2; k += 3) trisL.add(new int[] {first + k, first + k + 1, first + k + 2});
            } else if (prim == 0x98) {
                for (int k = 0; k < cnt - 2; k++)
                    trisL.add(k % 2 == 0 ? new int[] {first + k, first + k + 1, first + k + 2}
                            : new int[] {first + k + 1, first + k, first + k + 2});
            } else if (prim == 0xA0) {
                for (int k = 1; k < cnt - 1; k++) trisL.add(new int[] {first, first + k, first + k + 1});
            } else {
                for (int k = 0; k < cnt - 3; k += 4) {
                    trisL.add(new int[] {first + k, first + k + 1, first + k + 2});
                    trisL.add(new int[] {first + k, first + k + 2, first + k + 3});
                }
            }
        }
        // recipes (placed below, realize)
        final int nv = verts.size();
        final Pk pk = new Pk();
        pk.verts = new ArrayList<>(nv);
        pk.kind = new byte[nv];
        pk.raw = new double[nv * 3];
        pk.rj = new int[nv];
        pk.uv1 = new double[nv][];
        pk.uv2 = new double[nv][];
        for (int i = 0; i < nv; i++) {
            final RawV v = verts.get(i);
            double px = 0.0, py = 0.0, pz = 0.0;
            if (v.pos != null) {
                px = v.pos[0];
                py = v.pos.length > 1 ? v.pos[1] : 0.0;
                pz = v.pos.length > 2 ? v.pos[2] : 0.0;
            }
            double u = 0.0, vv = 0.0;
            if (v.tex0 != null) {
                u = v.tex0[0];
                vv = v.tex0.length > 1 ? v.tex0[1] : 0.0;
            }
            int[] wj;
            double[] ww;
            byte k;
            int rj;
            if (!envJ.isEmpty() && v.pnmtx >= 0 && v.pnmtx / 3 < envJ.size()) {
                final int[] ej = envJ.get(v.pnmtx / 3);
                final double[] ew = envW.get(v.pnmtx / 3);
                if (ej.length == 1 && o.modelSpaceEnv && index.containsKey(ej[0]) && d.u32(ej[0] + 0x38) != 0) {
                    wj = new int[] {index.get(ej[0])};
                    ww = new double[] {1.0};
                    k = ENV;
                    rj = ji;
                } else if (ej.length == 1) {
                    final int jj = index.containsKey(ej[0]) ? index.get(ej[0]) : ji;
                    wj = new int[] {jj};
                    ww = new double[] {1.0};
                    k = RIGID;
                    rj = jj;
                } else {
                    int c = 0;
                    final int[] tj = new int[Math.min(4, ej.length)];
                    final double[] tw = new double[tj.length];
                    for (int q = 0; q < ej.length && c < 4; q++) {
                        if (index.containsKey(ej[q])) {
                            tj[c] = index.get(ej[q]);
                            tw[c] = ew[q];
                            c++;
                        }
                    }
                    if (c == 0) {
                        wj = new int[] {ji};
                        ww = new double[] {1.0};
                    } else {
                        wj = java.util.Arrays.copyOf(tj, c);
                        ww = java.util.Arrays.copyOf(tw, c);
                    }
                    k = o.modelSpaceEnv ? ENV : RAW;
                    rj = ji;
                }
            } else {
                wj = new int[] {bone};
                ww = new double[] {1.0};
                k = RIGID;
                rj = bone;
            }
            pk.verts.add(new Vx(px, py, pz, wj, ww, u, vv));
            pk.kind[i] = k;
            pk.raw[i * 3] = px;
            pk.raw[i * 3 + 1] = py;
            pk.raw[i * 3 + 2] = pz;
            pk.rj[i] = rj;
            pk.uv1[i] = v.tex1;
            pk.uv2[i] = v.tex2;
        }
        if (!trisL.isEmpty()) {
            int cn = 0;
            final long[] sum = new long[3];
            for (RawV v : verts) {
                if (v.clr != null) {
                    cn++;
                    for (int c = 0; c < 3; c++) sum[c] += (long) v.clr[c];
                }
            }
            if (cn > 0) pk.vcol = new int[] {(int) (sum[0] / cn), (int) (sum[1] / cn), (int) (sum[2] / cn)};
            if (o.modelSpaceEnv && (fl & (0x200 | 0x400 | 0x800 | 0x2000)) != 0) billboard.put(packets.size(), ji);
            pk.tris = new int[trisL.size() * 3];
            for (int t = 0; t < trisL.size(); t++) System.arraycopy(trisL.get(t), 0, pk.tris, t * 3, 3);
            pk.mobj = mobj;
            packets.add(pk);
        }
    }

    private static void put(RawV v, int an, double[] c) {
        if (an == VA_POS) v.pos = c;
        else if (an == VA_TEX0) v.tex0 = c;
        else if (an == VA_TEX0 + 1) v.tex1 = c;
        else if (an == VA_TEX0 + 2) v.tex2 = c;
    }

    // ------------------------------------------------------------------------------------------------ placing
    private static double[] tr(double[][] m) {
        return new double[] {m[0][3], m[1][3], m[2][3]};
    }

    private int[] srOf;

    private double[] envPos(double[] raw, int owner, String mode) {
        if (mode.equals("owner")) return M4.vec(restWorld.get(owner), raw);
        if (mode.equals("owner_t")) {
            final double[] t = tr(restWorld.get(owner));
            return new double[] {raw[0] + t[0], raw[1] + t[1], raw[2] + t[2]};
        }
        if (mode.equals("inv_sr")) return M4.vec(M4.inv(restWorld.get(srOf[owner])), raw);
        return raw;
    }

    private void realize(List<double[][]> wv, Map<Integer, String> modes) {
        for (Pk pk : packets) {
            for (int i = 0; i < pk.kind.length; i++) {
                final double[] raw = {pk.raw[i * 3], pk.raw[i * 3 + 1], pk.raw[i * 3 + 2]};
                double[] p;
                if (pk.kind[i] == RIGID) p = M4.vec(wv.get(pk.rj[i]), raw);
                else if (pk.kind[i] == ENV) p = envPos(raw, pk.rj[i], modes.getOrDefault(pk.rj[i], "raw"));
                else p = raw;
                final Vx v = pk.verts.get(i);
                v.x = p[0];
                v.y = p[1];
                v.z = p[2];
            }
        }
    }

    private static int lead(Vx v) {
        int best = 0;
        for (int q = 1; q < v.ww.length; q++) if (v.ww[q] > v.ww[best]) best = q;
        return v.wj[best];
    }

    private void place() {
        final double[][] ident = M4.ident();
        if (!(o.modelSpaceEnv && !packets.isEmpty())) {
            realize(world, new HashMap<>());
            return;
        }
        final int nj = joints.size();
        srOf = new int[nj];
        for (int j = 0; j < nj; j++) {
            int p = j;
            while (p >= 0 && !(((d.u32(joints.get(p) + 0x04) & 0x2) != 0) && !M4.near(restWorld.get(p), ident))) p = parent.get(p);
            srOf[j] = p;
        }
        final List<double[][]> worldB = new ArrayList<>(nj);
        for (int j = 0; j < nj; j++) {
            final int jo = joints.get(j);
            final int ibp = d.u32(jo + 0x38);
            if (ibp != 0 && srOf[j] >= 0) {
                worldB.add(M4.mul(restWorld.get(srOf[j]), M4.inv(ibRows(ibp))));
            } else if (ibp != 0) {
                worldB.add(world.get(j));
            } else {
                final int par = parent.get(j);
                final double[][] base = par >= 0 ? worldB.get(par) : ident;
                worldB.add(M4.mul(base, M4.srt(d.f32(jo + 0x20, 3), d.f32(jo + 0x14, 3), d.f32(jo + 0x2C, 3))));
            }
        }
        // per DObj owner: (stored position, its main joint)
        final Map<Integer, List<double[]>> byOwner = new LinkedHashMap<>();
        final Map<Integer, List<Integer>> byOwnerJ = new LinkedHashMap<>();
        for (Pk pk : packets) {
            for (int i = 0; i < pk.kind.length; i++) {
                if (pk.kind[i] != ENV) continue;
                byOwner.computeIfAbsent(pk.rj[i], k -> new ArrayList<>())
                        .add(new double[] {pk.raw[i * 3], pk.raw[i * 3 + 1], pk.raw[i * 3 + 2]});
                byOwnerJ.computeIfAbsent(pk.rj[i], k -> new ArrayList<>()).add(lead(pk.verts.get(i)));
            }
        }
        final List<List<double[][]>> variants = new ArrayList<>();
        variants.add(world);
        boolean differ = false;
        for (int j = 0; j < nj && !differ; j++) if (!M4.near(worldB.get(j), world.get(j))) differ = true;
        if (differ) variants.add(worldB);
        double bestMean = 0;
        List<double[][]> bestW = null;
        Map<Integer, String> bestModes = null;
        for (List<double[][]> wv : variants) {
            final Map<Integer, String> modes = new HashMap<>();
            double total = 0.0;
            int count = 0;
            for (Map.Entry<Integer, List<double[]>> e : byOwner.entrySet()) {
                final int ow = e.getKey();
                final List<double[]> vs = e.getValue();
                final List<Integer> djs = byOwnerJ.get(ow);
                final List<String> cands = new ArrayList<>();
                cands.add("raw");
                if (!M4.near(restWorld.get(ow), ident)) {
                    cands.add("owner");
                    cands.add("owner_t");
                }
                if (srOf[ow] >= 0) cands.add("inv_sr");
                final Map<String, Double> sc = new HashMap<>();
                for (String mname : cands) {
                    double tot = 0.0;
                    final double[] lo = {1e30, 1e30, 1e30}, hi = {-1e30, -1e30, -1e30};
                    for (int q = 0; q < vs.size(); q++) {
                        final double[] p = envPos(vs.get(q), ow, mname);
                        final double[] t = tr(wv.get(djs.get(q)));
                        final double dx = p[0] - t[0], dy = p[1] - t[1], dz = p[2] - t[2];
                        tot += Math.sqrt(dx * dx + dy * dy + dz * dz);
                        for (int c = 0; c < 3; c++) {
                            if (p[c] < lo[c]) lo[c] = p[c];
                            if (p[c] > hi[c]) hi[c] = p[c];
                        }
                    }
                    final double ex = hi[0] - lo[0], ey = hi[1] - lo[1], ez = hi[2] - lo[2];
                    double size = Math.sqrt(Py.sum3(ex * ex, ey * ey, ez * ez));
                    if (size == 0.0) size = 1.0;
                    sc.put(mname, tot / vs.size() / size);
                }
                String mbest = null;
                double mk = 0;
                for (String mname : cands) {
                    final double k = sc.get(mname) * (mname.equals("raw") ? 1.0 : 1.03);
                    if (mbest == null || k < mk) {
                        mbest = mname;
                        mk = k;
                    }
                }
                modes.put(ow, mbest);
                total += sc.get(mbest) * vs.size();
                count += vs.size();
            }
            final double mean = total / Math.max(count, 1);
            if (bestW == null || mean < bestMean * 0.95) {
                bestMean = mean;
                bestW = wv;
                bestModes = modes;
            }
        }
        if (bestW != world) {
            for (int j = 0; j < nj; j++) world.set(j, bestW.get(j));
        }
        realize(world, bestModes);
        // joints shrunk to nothing in the rest pose hide their part
        final boolean[] hidden = new boolean[nj];
        boolean any = false;
        for (int j = 0; j < nj; j++) {
            final double[][] m = restWorld.get(j);
            double mx = 0;
            for (int c = 0; c < 3; c++) {
                final double v = Math.sqrt(m[0][c] * m[0][c] + m[1][c] * m[1][c] + m[2][c] * m[2][c]);
                if (c == 0 || v > mx) mx = v;
            }
            if (mx < 0.05) {
                hidden[j] = true;
                any = true;
            }
        }
        if (any) {
            for (Pk pk : packets) {
                final int[] ld = new int[pk.verts.size()];
                for (int i = 0; i < ld.length; i++) ld[i] = lead(pk.verts.get(i));
                int n = 0;
                final int[] keep = new int[pk.tris.length];
                for (int t = 0; t < pk.ntris(); t++) {
                    final int a = pk.tris[t * 3], b = pk.tris[t * 3 + 1], c = pk.tris[t * 3 + 2];
                    if (hidden[ld[a]] || hidden[ld[b]] || hidden[ld[c]]) continue;
                    keep[n++] = a;
                    keep[n++] = b;
                    keep[n++] = c;
                }
                pk.tris = java.util.Arrays.copyOf(keep, n);
            }
        }
    }

    // ------------------------------------------------------------------------------------------------ gas
    private static final double[][] PUFFS = {
        {0.0, 0.0, 0.0, 0.62, 0.0}, {0.27, 0.04, 0.02, 0.5, 0.6}, {-0.27, -0.03, 0.05, 0.52, 1.3},
        {0.03, 0.05, 0.27, 0.48, 2.1}, {-0.04, 0.02, -0.26, 0.5, 2.8}, {0.05, 0.22, 0.0, 0.46, 3.6},
        {-0.02, -0.18, 0.03, 0.44, 4.4}};

    private final Set<Integer> softMobj = new HashSet<>();

    private void gasPuffs() {
        for (Map.Entry<Integer, Integer> e : new ArrayList<>(billboard.entrySet())) {
            final int k = e.getKey();
            final Pk pk = packets.get(k);
            softMobj.add(pk.mobj);
            final List<Vx> vs = pk.verts;
            final double[] c = new double[3];
            final double[] buf = new double[vs.size()];
            for (int i = 0; i < 3; i++) {
                for (int q = 0; q < vs.size(); q++) buf[q] = comp(vs.get(q), i);
                c[i] = Py.sum(buf, vs.size()) / vs.size();
            }
            double S = 0;
            for (int i = 0; i < 3; i++) {
                double mx = comp(vs.get(0), i), mn = mx;
                for (Vx v : vs) {
                    final double x = comp(v, i);
                    if (x > mx) mx = x;
                    if (x < mn) mn = x;
                }
                final double ext = mx - mn;
                if (i == 0 || ext > S) S = ext;
            }
            if (S == 0.0) S = 1.0;
            double u0 = vs.get(0).u, u1 = u0, v0 = vs.get(0).v, v1 = v0;
            for (Vx v : vs) {
                if (v.u < u0) u0 = v.u;
                if (v.u > u1) u1 = v.u;
                if (v.v < v0) v0 = v.v;
                if (v.v > v1) v1 = v.v;
            }
            final int[] wj0 = vs.get(0).wj;
            final double[] ww0 = vs.get(0).ww;
            final List<Vx> nv = new ArrayList<>();
            final List<Integer> nt = new ArrayList<>();
            for (double[] pf : PUFFS) {
                final double h = 0.5 * pf[3] * S;
                final double cx = c[0] + pf[0] * S, cy = c[1] + pf[1] * S, cz = c[2] + pf[2] * S;
                final double cr = Trig.cos(pf[4]), sr = Trig.sin(pf[4]);
                for (int plane = 0; plane < 3; plane++) {
                    final int base = nv.size();
                    for (int q = 0; q < 4; q++) {
                        final int a = (q == 0 || q == 3) ? -1 : 1, b = q < 2 ? -1 : 1;
                        double x, y, z;
                        if (plane == 0) {
                            x = a * h;
                            y = b * h;
                            z = 0.0;
                        } else if (plane == 1) {
                            x = 0.0;
                            y = b * h;
                            z = a * h;
                        } else {
                            x = a * h;
                            y = 0.0;
                            z = b * h;
                        }
                        final double x2 = x * cr - z * sr, z2 = x * sr + z * cr;
                        nv.add(new Vx(cx + x2, cy + y, cz + z2, wj0, ww0, u0 + (a + 1) * 0.5 * (u1 - u0),
                                v0 + (b + 1) * 0.5 * (v1 - v0)));
                    }
                    nt.add(base);
                    nt.add(base + 1);
                    nt.add(base + 2);
                    nt.add(base);
                    nt.add(base + 2);
                    nt.add(base + 3);
                }
            }
            pk.verts = nv;
            pk.tris = new int[nt.size()];
            for (int i = 0; i < pk.tris.length; i++) pk.tris[i] = nt.get(i);
        }
    }

    private static double comp(Vx v, int i) {
        return i == 0 ? v.x : i == 1 ? v.y : v.z;
    }

    // ------------------------------------------------------------------------------------------------ upright
    private void upright() {
        final String u = o.upright;
        double[][] R;
        switch (u) {
            case "x+90": R = new double[][] {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}; break;
            case "x-90": R = new double[][] {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}}; break;
            case "z+90": R = new double[][] {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}; break;
            case "z-90": R = new double[][] {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}; break;
            default: {
                if (!u.startsWith("y")) throw new IllegalArgumentException("upright " + u);
                final double a = Trig.radians(Py.num(u.substring(1)));
                final double ca = Trig.cos(a), sa = Trig.sin(a);
                R = new double[][] {{ca, 0, sa}, {0, 1, 0}, {-sa, 0, ca}};
            }
        }
        final double[][] R4 = {{R[0][0], R[0][1], R[0][2], 0}, {R[1][0], R[1][1], R[1][2], 0},
            {R[2][0], R[2][1], R[2][2], 0}, {0, 0, 0, 1}};
        for (int j = 0; j < world.size(); j++) world.set(j, M4.mul(R4, world.get(j)));
        for (Pk pk : packets) {
            for (Vx v : pk.verts) {
                final double[] p = {v.x, v.y, v.z};
                final double[] r = new double[3];
                for (int i = 0; i < 3; i++) r[i] = Py.sum3(R[i][0] * p[0], R[i][1] * p[1], R[i][2] * p[2]);
                v.x = r[0];
                v.y = r[1];
                v.z = r[2];
            }
        }
    }

    // ------------------------------------------------------------------------------------------------ textures
    private final LinkedHashMap<Integer, byte[]> textures = new LinkedHashMap<>();
    private final Map<Integer, List<double[]>> rawUvs = new HashMap<>();

    /** A material's UV function. */
    interface UvFn {
        double[] map(double u, double v);
    }

    private static final class MatInfo {
        final int hash;
        final UvFn fn;

        MatInfo(int hash, UvFn fn) {
            this.hash = hash;
            this.fn = fn;
        }
    }

    private static final class MatPix {
        byte[] rgba;
        int w, h, ws, wt;
        UvFn fn;
    }

    private final Map<Integer, MatInfo> matInfo = new HashMap<>();
    private final Map<Integer, Tex.Pic> plainPics = new HashMap<>();
    private final Map<Integer, MatPix> matPixels = new HashMap<>();

    private int swatch(int r, int g, int b, String key) {
        final int h = Py.lowerHash(name + "/colour/" + key);
        if (!textures.containsKey(h)) {
            final byte[] px = new byte[64];
            for (int i = 0; i < 16; i++) {
                px[i * 4] = (byte) r;
                px[i * 4 + 1] = (byte) g;
                px[i * 4 + 2] = (byte) b;
                px[i * 4 + 3] = (byte) 255;
            }
            textures.put(h, Tex.record(px, 4, 4));
        }
        return h;
    }

    private static String hex2(int v) {
        final String s = Integer.toHexString(v & 0xFF);
        return s.length() < 2 ? "0" + s : s;
    }

    private MatInfo material(int mobj, int[] vcol) throws IOException {
        if (mobj == 0) return new MatInfo(swatch(200, 200, 200, "none"), (u, v) -> new double[] {0.0, 0.0});
        final int matp = d.u32(mobj + 0x0C);
        final int[] dc = matp != 0 ? new int[] {d.u8(matp + 4), d.u8(matp + 5), d.u8(matp + 6), d.u8(matp + 7)}
                : new int[] {200, 200, 200, 255};
        final double[] diffuse = {dc[0] / 255.0, dc[1] / 255.0, dc[2] / 255.0};
        final List<Integer> imgs = new ArrayList<>();
        for (int t : Tex.tobjList(d, mobj)) if (d.u32(t + 0x4C) != 0) imgs.add(t);
        if (imgs.isEmpty()) {
            int[] c = dc;
            if (vcol != null) c = new int[] {dc[0] * vcol[0] / 255, dc[1] * vcol[1] / 255, dc[2] * vcol[2] / 255, 255};
            return new MatInfo(swatch(c[0], c[1], c[2], hex2(c[0]) + hex2(c[1]) + hex2(c[2])), (u, v) -> new double[] {0.5, 0.5});
        }
        final MatInfo cached = matInfo.get(mobj);
        if (cached != null) return cached;
        final int first = imgs.get(0);
        final List<Integer> uvt = new ArrayList<>();
        for (int t : imgs) if ((d.u32(t + 0x40) & 0xF) == Tex.TEX_COORD_UV) uvt.add(t);
        Integer base = null;
        for (int t : uvt) {
            final int f = d.u32(t + 0x40);
            if ((f & 0x50) != 0 || (f & 0x1F0) == 0) {
                base = t;
                break;
            }
        }
        if (base == null && !uvt.isEmpty()) base = uvt.get(0);
        if (base == null) {
            final Tex.Pic pic = texd.rgba(first);
            double[] a = Tex.avgColour(pic.rgba);
            final Tex.ColourOp op = Tex.colourOp((d.u32(first + 0x40) >> 16) & 0xF, d.f32(first + 0x44), diffuse);
            if (op != null) a = op.apply(a);
            final int[] c = new int[3];
            for (int i = 0; i < 3; i++) c[i] = Math.min(255, (int) (a[i] * 255));
            final MatInfo mi = new MatInfo(swatch(c[0], c[1], c[2], "env" + Integer.toHexString(mobj)), (u, v) -> new double[] {0.5, 0.5});
            matInfo.put(mobj, mi);
            return mi;
        }
        final int bt = base;
        final int fl = d.u32(bt + 0x40);
        final int rs = d.u8(bt + 0x3C) != 0 ? d.u8(bt + 0x3C) : 1, rt = d.u8(bt + 0x3D) != 0 ? d.u8(bt + 0x3D) : 1;
        double sx = d.f32(bt + 0x1C), sy = d.f32(bt + 0x20);
        double tx = d.f32(bt + 0x28), ty = d.f32(bt + 0x2C);
        final double rz = d.f32(bt + 0x18);
        final Map<Integer, Double> moves = texFrame0.getOrDefault(mobj, new HashMap<>())
                .getOrDefault(d.u32(bt + 0x08), new HashMap<>());
        final double[] own = {tx, ty, sx, sy};
        tx = moves.getOrDefault(2, tx);
        ty = moves.getOrDefault(3, ty);
        sx = moves.getOrDefault(4, sx);
        sy = moves.getOrDefault(5, sy);
        final int ws = d.u32(bt + 0x34), wt = d.u32(bt + 0x38);
        final double cr = Trig.cos(-rz), sr = Trig.sin(-rz);
        final List<double[]> raw = rawUvs.get(mobj);
        double[] set = setting(tx, ty, sx, sy, rs, rt, wt);
        if (!moves.isEmpty() && onPicture(set, cr, sr, ws, wt, raw) < 0.5) {
            final double[] alt = setting(own[0], own[1], own[2], own[3], rs, rt, wt);
            if (onPicture(alt, cr, sr, ws, wt, raw) >= 0.5) set = alt;
        }
        final double ftx = set[0], fty = set[1], fs = set[2], ft = set[3];
        final UvFn tm = (u, v) -> {
            double uu = u - ftx, vv = v - fty;
            final double u2 = cr * uu - sr * vv, v2 = sr * uu + cr * vv;
            return new double[] {u2 * fs, v2 * ft};
        };
        final Tex.Pic pic = texd.rgba(bt);
        if (!plainPics.containsKey(pic.data)) plainPics.put(pic.data, pic);
        byte[] rgba = pic.rgba;
        final int w_ = pic.w, h_ = pic.h, data = pic.data;
        final Tex.ColourOp op = Tex.colourOp((fl >> 16) & 0xF, d.f32(bt + 0x44), diffuse);
        if (op != null) rgba = Tex.applyColour(rgba, op);
        if (softMobj.contains(mobj)) rgba = gas(rgba, w_, h_);
        int s0 = 0, ns = 1, t0 = 0, nt = 1;
        final boolean hasUvs = raw != null && !raw.isEmpty();
        if (hasUvs) {
            final double e = 1e-3;
            double umin = 0, umax = 0, vmin = 0, vmax = 0;
            for (int i = 0; i < raw.size(); i++) {
                final double[] m = tm.map(raw.get(i)[0], raw.get(i)[1]);
                if (i == 0 || m[0] < umin) umin = m[0];
                if (i == 0 || m[0] > umax) umax = m[0];
                if (i == 0 || m[1] < vmin) vmin = m[1];
                if (i == 0 || m[1] > vmax) vmax = m[1];
            }
            s0 = Py.floor(umin + e);
            final int s1 = Math.max(Py.ceil(umax - e), s0 + 1);
            t0 = Py.floor(vmin + e);
            final int t1 = Math.max(Py.ceil(vmax - e), t0 + 1);
            ns = s1 - s0;
            nt = t1 - t0;
        }
        final List<Integer> overs = new ArrayList<>();
        for (int t : uvt)
            if (t != bt && ((d.u32(t + 0x40) >> 16) & 0xF) == TEX_COLORMAP_ALPHA_MASK && d.u32(t + 0x0C) == GX_TG_TEX0)
                overs.add(t);
        final int hsh;
        final UvFn fn;
        final MatPix pix = new MatPix();
        if (!overs.isEmpty() && hasUvs && fs != 0.0 && ft != 0.0) {
            final int fs0 = s0, fns = ns, ft0 = t0, fnt = nt;
            final int[] dims = new int[2];
            rgba = bakeLayers(rgba, w_, h_, new double[] {ftx, fty, fs, ft, cr, sr}, s0, ns, t0, nt, ws, wt, overs,
                    texFrame0.getOrDefault(mobj, new HashMap<>()), raw, dims);
            hsh = Py.lowerHash(name + "/tex/" + Integer.toHexString(mobj) + "/layered");
            textures.put(hsh, Tex.record(rgba, dims[0], dims[1]));
            fn = (u, v) -> {
                final double[] m = tm.map(u, v);
                return new double[] {(m[0] - fs0) / fns, (m[1] - ft0) / fnt};
            };
            pix.rgba = rgba;
            pix.w = dims[0];
            pix.h = dims[1];
            pix.ws = Tex.GX_CLAMP;
            pix.wt = Tex.GX_CLAMP;
        } else if (!(s0 == 0 && ns == 1 && t0 == 0 && nt == 1) && (long) w_ * ns <= 2048 && (long) h_ * nt <= 2048) {
            rgba = Tex.bakeWrap(rgba, w_, h_, s0, ns, t0, nt, ws, wt);
            int bw = w_ * ns, bh = h_ * nt;
            while (bw > 1024 || bh > 1024) {
                final int nw = Math.max(bw / 2, 1), nh = Math.max(bh / 2, 1);
                final byte[] half = new byte[nw * nh * 4];
                for (int y = 0; y < nh; y++)
                    for (int x = 0; x < nw; x++) System.arraycopy(rgba, ((y * 2) * bw + x * 2) * 4, half, (y * nw + x) * 4, 4);
                rgba = half;
                bw = nw;
                bh = nh;
            }
            hsh = Py.lowerHash(name + "/tex/" + Integer.toHexString(mobj) + "/baked");
            textures.put(hsh, Tex.record(rgba, bw, bh));
            final int fs0 = s0, fns = ns, ft0 = t0, fnt = nt;
            fn = (u, v) -> {
                final double[] m = tm.map(u, v);
                return new double[] {(m[0] - fs0) / fns, (m[1] - ft0) / fnt};
            };
            pix.rgba = rgba;
            pix.w = bw;
            pix.h = bh;
            pix.ws = Tex.GX_CLAMP;
            pix.wt = Tex.GX_CLAMP;
        } else {
            hsh = op != null ? Py.lowerHash(name + "/tex/" + Integer.toHexString(data) + "/" + Integer.toHexString(mobj))
                    : Py.lowerHash(name + "/tex/" + Integer.toHexString(data));
            if (!textures.containsKey(hsh)) textures.put(hsh, Tex.record(rgba, w_, h_));
            fn = tm;
            pix.rgba = rgba;
            pix.w = w_;
            pix.h = h_;
            pix.ws = ws;
            pix.wt = wt;
        }
        pix.fn = fn;
        matPixels.put(mobj, pix);
        final MatInfo mi = new MatInfo(hsh, fn);
        matInfo.put(mobj, mi);
        return mi;
    }

    /** setting(): (tx, ty, fs, ft) */
    private static double[] setting(double tx, double ty, double sx, double sy, int rs, int rt, int wt) {
        final double fs = Math.abs(sx) > 1e-6 ? rs / sx : 0.0;
        final double ft = Math.abs(sy) > 1e-6 ? rt / sy : 0.0;
        if (wt == Tex.GX_MIRROR && ft != 0.0) ty += 1.0 / ft;
        return new double[] {tx, ty, fs, ft};
    }

    /** on_picture(): the share of the coordinate range on the picture along each clamped axis. */
    private static double onPicture(double[] st, double cr, double sr, int ws, int wt, List<double[]> raw) {
        double share = 1.0;
        for (int axis = 0; axis < 2; axis++) {
            final int wrap = axis == 0 ? ws : wt;
            if (wrap != Tex.GX_CLAMP || raw == null || raw.isEmpty()) continue;
            double lo = 0, hi = 0;
            for (int i = 0; i < raw.size(); i++) {
                final double uu = raw.get(i)[0] - st[0], vv = raw.get(i)[1] - st[1];
                final double u2 = cr * uu - sr * vv, v2 = sr * uu + cr * vv;
                final double val = axis == 0 ? u2 * st[2] : v2 * st[3];
                if (i == 0 || val < lo) lo = val;
                if (i == 0 || val > hi) hi = val;
            }
            if (hi - lo > 1e-6) share = pyMin(share, pyMax(0.0, pyMin(hi, 1.0) - pyMax(lo, 0.0)) / (hi - lo));
        }
        return share;
    }

    static double pyMin(double a, double b) {
        return b < a ? b : a;
    }

    static double pyMax(double a, double b) {
        return b > a ? b : a;
    }

    /** r69 gas: greyscale picture -> its colour, brightness as see-through, faded round. */
    private byte[] gas(byte[] rgba, int w, int h) {
        final int[] gc = o.gasColor != null ? o.gasColor : new int[] {255, 255, 255};
        final byte[] px = new byte[rgba.length];
        for (int q = 0; q < rgba.length; q += 4) {
            final int xx = (q / 4) % w, yy = (q / 4) / w;
            final double rr = Py.hypot((xx + 0.5) / w - 0.5, (yy + 0.5) / h - 0.5) / 0.5;
            double fade = pyMax(0.0, pyMin(1.0, (1.0 - rr) / 0.45));
            fade = fade * fade * (3 - 2 * fade);
            final int lum = Math.min(255, (int) ((rgba[q] & 0xFF) * o.gasOpacity * fade));
            px[q] = (byte) gc[0];
            px[q + 1] = (byte) gc[1];
            px[q + 2] = (byte) gc[2];
            px[q + 3] = (byte) lum;
        }
        return px;
    }

    /** tobj_map(): a layer's picture and coordinate function. */
    private static final class Layer {
        UvFn f;
        byte[] rgba;
        int w, h, ws, wt;
        double fs, ft;
        int si;
    }

    private Layer tobjMap(int t, Map<Integer, Map<Integer, Double>> movesOf, List<double[]> raw) throws IOException {
        final int rs = d.u8(t + 0x3C) != 0 ? d.u8(t + 0x3C) : 1, rt = d.u8(t + 0x3D) != 0 ? d.u8(t + 0x3D) : 1;
        final double lsx = d.f32(t + 0x1C), lsy = d.f32(t + 0x20);
        final double ltx = d.f32(t + 0x28), lty = d.f32(t + 0x2C);
        final double lrz = d.f32(t + 0x18);
        final int lws = d.u32(t + 0x34), lwt = d.u32(t + 0x38);
        final double lcr = Trig.cos(-lrz), lsr = Trig.sin(-lrz);
        final Map<Integer, Double> mv = movesOf.getOrDefault(d.u32(t + 0x08), new HashMap<>());
        double[] m = make(mv.getOrDefault(2, ltx), mv.getOrDefault(3, lty), mv.getOrDefault(4, lsx), mv.getOrDefault(5, lsy), rs, rt, lwt);
        if (!mv.isEmpty() && share(m, lcr, lsr, lws, lwt, raw) < 0.5) {
            final double[] g = make(ltx, lty, lsx, lsy, rs, rt, lwt);
            if (share(g, lcr, lsr, lws, lwt, raw) >= 0.5) m = g;
        }
        final double tx = m[0], ty = m[1], fs = m[2], ft = m[3];
        final Layer L = new Layer();
        L.f = (u, v) -> {
            final double uu = u - tx, vv = v - ty;
            final double u2 = lcr * uu - lsr * vv, v2 = lsr * uu + lcr * vv;
            return new double[] {u2 * fs, v2 * ft};
        };
        final Tex.Pic pic = texd.rgba(t);
        L.rgba = pic.rgba;
        L.w = pic.w;
        L.h = pic.h;
        L.ws = lws;
        L.wt = lwt;
        L.fs = Math.abs(fs);
        L.ft = Math.abs(ft);
        return L;
    }

    private static double[] make(double tx, double ty, double sx, double sy, int rs, int rt, int lwt) {
        final double fs = Math.abs(sx) > 1e-6 ? rs / sx : 0.0;
        final double ft = Math.abs(sy) > 1e-6 ? rt / sy : 0.0;
        if (lwt == Tex.GX_MIRROR && ft != 0.0) ty += 1.0 / ft;
        return new double[] {tx, ty, fs, ft};
    }

    private static double share(double[] m, double cr, double sr, int ws, int wt, List<double[]> raw) {
        double best = 1.0;
        for (int axis = 0; axis < 2; axis++) {
            final int wrap = axis == 0 ? ws : wt;
            if (wrap != Tex.GX_CLAMP || raw == null || raw.isEmpty()) continue;
            double lo = 0, hi = 0;
            for (int i = 0; i < raw.size(); i++) {
                final double uu = raw.get(i)[0] - m[0], vv = raw.get(i)[1] - m[1];
                final double val = axis == 0 ? (cr * uu - sr * vv) * m[2] : (sr * uu + cr * vv) * m[3];
                if (i == 0 || val < lo) lo = val;
                if (i == 0 || val > hi) hi = val;
            }
            if (hi - lo > 1e-6) best = pyMin(best, pyMax(0.0, pyMin(hi, 1.0) - pyMax(lo, 0.0)) / (hi - lo));
        }
        return best;
    }

    private byte[] bakeLayers(byte[] rgba, int w, int h, double[] baseMap, int s0, int ns, int t0, int nt, int ws,
                              int wt, List<Integer> overs, Map<Integer, Map<Integer, Double>> movesOf,
                              List<double[]> raw, int[] dims) throws IOException {
        final double tx = baseMap[0], ty = baseMap[1], fs = baseMap[2], ft = baseMap[3], cr = baseMap[4], sr = baseMap[5];
        final List<Layer> layers = new ArrayList<>();
        for (int t : overs) layers.add(tobjMap(t, movesOf, raw));
        int k = 1;
        for (Layer L : layers) {
            k = Math.max(k, Math.max(Py.ceil(L.w * L.fs / (w * Math.abs(fs)) - 1e-6), Py.ceil(L.h * L.ft / (h * Math.abs(ft)) - 1e-6)));
        }
        k = Math.max(1, Math.min(k, Math.min(1024 / Math.max(1, w * ns), 1024 / Math.max(1, h * nt))));
        final int ow = w * ns * k, oh = h * nt * k;
        final byte[] out = new byte[ow * oh * 4];
        for (int y = 0; y < oh; y++) {
            final double V = t0 + (y + 0.5) / (h * k);
            for (int x = 0; x < ow; x++) {
                final double U = s0 + (x + 0.5) / (w * k);
                final double[] c = Tex.sampleBilinear(rgba, w, h, U, V, ws, wt);
                final double a = U / fs, b = V / ft;
                final double u = cr * a + sr * b + tx;
                final double v = -sr * a + cr * b + ty;
                for (Layer L : layers) {
                    final double[] st = L.f.map(u, v);
                    final double[] lc = Tex.sampleBilinear(L.rgba, L.w, L.h, st[0], st[1], L.ws, L.wt);
                    final double al = lc[3] / 255.0;
                    if (al > 0.0) for (int ch = 0; ch < 3; ch++) c[ch] = c[ch] * (1.0 - al) + lc[ch] * al;
                }
                final int o2 = (y * ow + x) * 4;
                for (int q = 0; q < 4; q++) out[o2 + q] = (byte) Math.max(0, Math.min(255, (int) (c[q] + 0.5)));
            }
        }
        dims[0] = ow;
        dims[1] = oh;
        return out;
    }

    // ------------------------------------------------------------------------------------------------ decals
    private void decals() throws IOException {
        final Map<Long, List<double[]>> rawSets = new HashMap<>();
        for (Pk pk : packets) {
            if (pk.mobj == 0 || pk.isDecal) continue;
            for (int si = 0; si < 2; si++) {
                final List<double[]> l = rawSets.computeIfAbsent(((long) pk.mobj << 1) | si, k -> new ArrayList<>());
                final double[][] set = si == 0 ? pk.uv1 : pk.uv2;
                for (double[] c : set) if (c != null) l.add(c);
            }
        }
        final List<Pk> decals = new ArrayList<>();
        final int np = packets.size();
        for (int k = 0; k < np; k++) {
            final Pk pk = packets.get(k);
            if (pk.mobj == 0 || pk.tris.length == 0) continue;
            final List<Integer> lay = new ArrayList<>();
            for (int t : Tex.tobjList(d, pk.mobj)) {
                final int f = d.u32(t + 0x40), src = d.u32(t + 0x0C);
                if (d.u32(t + 0x4C) != 0 && (f & 0xF) == Tex.TEX_COORD_UV && ((f >> 16) & 0xF) == TEX_COLORMAP_ALPHA_MASK
                        && (src == GX_TG_TEX0 + 1 || src == GX_TG_TEX0 + 2)) lay.add(t);
            }
            if (lay.isEmpty()) continue;
            material(pk.mobj, pk.vcol);
            final MatPix mp = matPixels.get(pk.mobj);
            if (mp == null) continue;
            final List<Layer> L = new ArrayList<>();
            for (int t : lay) {
                final int si = d.u32(t + 0x0C) - GX_TG_TEX0 - 1;
                final Layer l = tobjMap(t, texFrame0.getOrDefault(pk.mobj, new HashMap<>()),
                        rawSets.getOrDefault(((long) pk.mobj << 1) | si, new ArrayList<>()));
                l.si = si;
                L.add(l);
            }
            final int nv = pk.verts.size();
            final double[][][] stOf = new double[L.size()][nv][];
            for (int li = 0; li < L.size(); li++) {
                final Layer l = L.get(li);
                final double[][] set = l.si == 0 ? pk.uv1 : pk.uv2;
                for (int vi = 0; vi < nv; vi++) stOf[li][vi] = set[vi] == null ? null : l.f.map(set[vi][0], set[vi][1]);
            }
            final List<Integer> aTris = new ArrayList<>();
            final boolean[] isA = new boolean[pk.ntris()];
            for (int t = 0; t < pk.ntris(); t++) {
                final int a = pk.tris[t * 3], b = pk.tris[t * 3 + 1], c = pk.tris[t * 3 + 2];
                if (stOf[0][a] == null || stOf[0][b] == null || stOf[0][c] == null) continue;
                boolean any = false;
                for (int li = 0; li < L.size() && !any; li++) any = shows(new int[] {a, b, c}, stOf[li], L.get(li));
                if (any) {
                    aTris.add(t);
                    isA[t] = true;
                }
            }
            if (aTris.isEmpty()) continue;
            // a_set / b_tris: triangles as values (a repeated triangle counts as in a_set too)
            final Set<Long> aSet = new HashSet<>();
            for (int t : aTris) aSet.add(triKey(pk.tris, t));
            final java.util.TreeSet<Integer> usedSet = new java.util.TreeSet<>();
            for (int t : aTris) for (int q = 0; q < 3; q++) usedSet.add(pk.tris[t * 3 + q]);
            final int[] used = new int[usedSet.size()];
            int ui = 0;
            for (int v : usedSet) used[ui++] = v;
            double umin = 0, umax = 0, vmin = 0, vmax = 0;
            for (int i = 0; i < used.length; i++) {
                final double[] p = stOf[0][used[i]];
                if (i == 0 || p[0] < umin) umin = p[0];
                if (i == 0 || p[0] > umax) umax = p[0];
                if (i == 0 || p[1] < vmin) vmin = p[1];
                if (i == 0 || p[1] > vmax) vmax = p[1];
            }
            if (umax - umin < 1e-6 || vmax - vmin < 1e-6) continue;
            double kk = 2.0;
            for (int li = 1; li < L.size(); li++) {
                double smin = 0, smax = 0, tmin = 0, tmax = 0;
                boolean got = false;
                for (int vi : used) {
                    final double[] p = stOf[li][vi];
                    if (p == null) continue;
                    if (!got || p[0] > smax) smax = p[0];
                    if (!got || p[0] < smin) smin = p[0];
                    if (!got || p[1] > tmax) tmax = p[1];
                    if (!got || p[1] < tmin) tmin = p[1];
                    got = true;
                }
                if (got) {
                    final double ds = (smax - smin) * L.get(li).w / ((umax - umin) * L.get(0).w);
                    final double dt = (tmax - tmin) * L.get(li).h / ((vmax - vmin) * L.get(0).h);
                    kk = pyMax(pyMax(kk, ds), dt);
                }
            }
            double Wd = (umax - umin) * L.get(0).w * kk;
            double Hd = (vmax - vmin) * L.get(0).h * kk;
            final double fit = pyMin(1.0, 512.0 / pyMax(Wd, Hd));
            final int W = Math.max(8, Py.ceil(Wd * fit)), H = Math.max(8, Py.ceil(Hd * fit));
            final double[][] img = new double[W * H][];
            for (int t : aTris) {
                final int[] tv = {pk.tris[t * 3], pk.tris[t * 3 + 1], pk.tris[t * 3 + 2]};
                final double[][] P = new double[3][];
                for (int q = 0; q < 3; q++) {
                    final double[] s = stOf[0][tv[q]];
                    P[q] = new double[] {(s[0] - umin) / (umax - umin) * W, (s[1] - vmin) / (vmax - vmin) * H};
                }
                final double den = (P[1][1] - P[2][1]) * (P[0][0] - P[2][0]) + (P[2][0] - P[1][0]) * (P[0][1] - P[2][1]);
                if (Math.abs(den) < 1e-9) continue;
                final double pminx = pyMin(pyMin(P[0][0], P[1][0]), P[2][0]), pmaxx = pyMax(pyMax(P[0][0], P[1][0]), P[2][0]);
                final double pminy = pyMin(pyMin(P[0][1], P[1][1]), P[2][1]), pmaxy = pyMax(pyMax(P[0][1], P[1][1]), P[2][1]);
                final int xa = Math.max(0, Py.floor(pminx)), xb = Math.min(W - 1, Py.ceil(pmaxx));
                final int ya = Math.max(0, Py.floor(pminy)), yb = Math.min(H - 1, Py.ceil(pmaxy));
                for (int y = ya; y <= yb; y++) {
                    for (int x = xa; x <= xb; x++) {
                        final double px = x + 0.5, py = y + 0.5;
                        final double l0 = ((P[1][1] - P[2][1]) * (px - P[2][0]) + (P[2][0] - P[1][0]) * (py - P[2][1])) / den;
                        final double l1 = ((P[2][1] - P[0][1]) * (px - P[2][0]) + (P[0][0] - P[2][0]) * (py - P[2][1])) / den;
                        final double l2 = 1.0 - l0 - l1;
                        if (pyMin(pyMin(l0, l1), l2) < -1e-4) continue;
                        final double[] lam = {l0, l1, l2};
                        final double u0 = Py.sum3(lam[0] * pk.verts.get(tv[0]).u, lam[1] * pk.verts.get(tv[1]).u, lam[2] * pk.verts.get(tv[2]).u);
                        final double v0 = Py.sum3(lam[0] * pk.verts.get(tv[0]).v, lam[1] * pk.verts.get(tv[1]).v, lam[2] * pk.verts.get(tv[2]).v);
                        final double[] bst = mp.fn.map(u0, v0);
                        final double[] c = Tex.sampleBilinear(mp.rgba, mp.w, mp.h, bst[0], bst[1], mp.ws, mp.wt);
                        for (Layer l : L) {
                            final double[][] set = l.si == 0 ? pk.uv1 : pk.uv2;
                            final double[] c0 = set[tv[0]], c1 = set[tv[1]], c2 = set[tv[2]];
                            if (c0 == null || c1 == null || c2 == null) continue;
                            final double[] st = l.f.map(Py.sum3(lam[0] * c0[0], lam[1] * c1[0], lam[2] * c2[0]),
                                    Py.sum3(lam[0] * c0[1], lam[1] * c1[1], lam[2] * c2[1]));
                            final double[] lc = Tex.sampleBilinear(l.rgba, l.w, l.h, st[0], st[1], l.ws, l.wt);
                            final double al = lc[3] / 255.0;
                            if (al > 0.0) for (int ch = 0; ch < 3; ch++) c[ch] = c[ch] * (1.0 - al) + lc[ch] * al;
                        }
                        img[y * W + x] = c;
                    }
                }
            }
            for (int it = 0; it < 4; it++) {
                final Map<Integer, double[]> fill = new LinkedHashMap<>();
                final double[] nb = new double[4];
                for (int y = 0; y < H; y++) {
                    for (int x = 0; x < W; x++) {
                        if (img[y * W + x] != null) continue;
                        final double[][] ns = new double[4][];
                        int n = 0;
                        final int[][] dxy = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                        for (int[] q : dxy) {
                            if (q[0] >= 0 && q[0] < W && q[1] >= 0 && q[1] < H && img[q[1] * W + q[0]] != null) ns[n++] = img[q[1] * W + q[0]];
                        }
                        if (n > 0) {
                            final double[] c = new double[4];
                            for (int ch = 0; ch < 4; ch++) {
                                for (int i = 0; i < n; i++) nb[i] = ns[i][ch];
                                c[ch] = Py.sum(nb, n) / n;
                            }
                            fill.put(y * W + x, c);
                        }
                    }
                }
                for (Map.Entry<Integer, double[]> e : fill.entrySet()) img[e.getKey()] = e.getValue();
            }
            final double[] avg = Tex.sampleBilinear(mp.rgba, mp.w, mp.h, 0.5, 0.5, mp.ws, mp.wt);
            final byte[] rgbaD = new byte[W * H * 4];
            for (int i = 0; i < W * H; i++) {
                final double[] c = img[i] != null ? img[i] : avg;
                for (int ch = 0; ch < 4; ch++) rgbaD[i * 4 + ch] = (byte) Math.max(0, Math.min(255, (int) (c[ch] + 0.5)));
            }
            final int hsh = Py.lowerHash(name + "/tex/" + Integer.toHexString(pk.mobj) + "/decal" + k);
            textures.put(hsh, Tex.record(rgbaD, W, H));
            final Map<Integer, Integer> remap = new HashMap<>();
            for (int q = 0; q < used.length; q++) remap.put(used[q], q);
            final Pk dk = new Pk();
            dk.isDecal = true;
            dk.decalHash = hsh;
            dk.vcol = pk.vcol;
            dk.verts = new ArrayList<>();
            for (int vi : used) {
                final Vx v = pk.verts.get(vi);
                final double[] s = stOf[0][vi];
                dk.verts.add(new Vx(v.x, v.y, v.z, v.wj, v.ww, (s[0] - umin) / (umax - umin), (s[1] - vmin) / (vmax - vmin)));
            }
            dk.tris = new int[aTris.size() * 3];
            for (int i = 0; i < aTris.size(); i++)
                for (int q = 0; q < 3; q++) dk.tris[i * 3 + q] = remap.get(pk.tris[aTris.get(i) * 3 + q]);
            decals.add(dk);
            // b_tris: the triangles not in a_set (by value)
            int n = 0;
            final int[] bt = new int[pk.tris.length];
            for (int t = 0; t < pk.ntris(); t++) {
                if (aSet.contains(triKey(pk.tris, t))) continue;
                for (int q = 0; q < 3; q++) bt[n++] = pk.tris[t * 3 + q];
            }
            pk.tris = java.util.Arrays.copyOf(bt, n);
        }
        packets.addAll(decals);
    }

    private static long triKey(int[] tris, int t) {
        return ((long) tris[t * 3] << 42) ^ ((long) tris[t * 3 + 1] << 21) ^ tris[t * 3 + 2];
    }

    private static boolean shows(int[] tri, double[][] st, Layer L) {
        final double[][] pts = new double[3][];
        for (int q = 0; q < 3; q++) {
            pts[q] = st[tri[q]];
            if (pts[q] == null) return false;
        }
        double sLo = pyMin(pyMin(pts[0][0], pts[1][0]), pts[2][0]), sHi = pyMax(pyMax(pts[0][0], pts[1][0]), pts[2][0]);
        double tLo = pyMin(pyMin(pts[0][1], pts[1][1]), pts[2][1]), tHi = pyMax(pyMax(pts[0][1], pts[1][1]), pts[2][1]);
        if (L.ws == Tex.GX_CLAMP) {
            sLo = pyMax(sLo, 0.0);
            sHi = pyMin(sHi, 1.0);
        }
        if (L.wt == Tex.GX_CLAMP) {
            tLo = pyMax(tLo, 0.0);
            tHi = pyMin(tHi, 1.0);
        }
        if (sLo > sHi || tLo > tHi) return false;
        final int x0 = Py.floor(sLo * L.w), x1 = Py.floor(sHi * L.w);
        final int y0 = Py.floor(tLo * L.h), y1 = Py.floor(tHi * L.h);
        final int step = Math.max(1, (int) Math.sqrt((double) ((long) (x1 - x0 + 1) * (y1 - y0 + 1)) / 4096.0));
        for (int y = y0; y <= y1; y += step) {
            final int row = Tex.wrapIndex(y, L.h, L.wt) * L.w;
            for (int x = x0; x <= x1; x += step) if ((L.rgba[(row + Tex.wrapIndex(x, L.w, L.ws)) * 4 + 3] & 0xFF) > 8) return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------------------------------ the pack
    private Result make() throws IOException {
        int root = o.root;
        if (root < 0) {
            root = d.rootEndingWith("_joint");
            if (root < 0) throw new IOException("no joint root");
        }
        walk(root, -1, M4.ident(), M4.ident());
        if (o.matanim >= 0) texanimWalk(root, o.matanim);
        readShapes();
        place();
        gasPuffs();
        if (o.upright != null) upright();
        // size
        final List<Pk> sized = new ArrayList<>();
        for (int k = 0; k < packets.size(); k++) if (!billboard.containsKey(k)) sized.add(packets.get(k));
        if (sized.isEmpty()) sized.addAll(packets);
        double ymin = 0, ymax = 0;
        boolean any = false;
        for (Pk pk : sized) for (Vx v : pk.verts) {
            if (!any || v.y > ymax) ymax = v.y;
            if (!any || v.y < ymin) ymin = v.y;
            any = true;
        }
        final double meleeH = any ? ymax - ymin : 1.0;
        double s = !Double.isNaN(o.height) && o.height != 0.0 ? o.height / meleeH : 0.075;
        if (!Double.isNaN(o.ground) && !packets.isEmpty()) {
            double xmin = 0, xmax = 0, zmin = 0, zmax = 0;
            boolean got = false;
            for (Pk pk : sized) for (Vx v : pk.verts) {
                if (!got || v.x < xmin) xmin = v.x;
                if (!got || v.x > xmax) xmax = v.x;
                if (!got || v.z < zmin) zmin = v.z;
                if (!got || v.z > zmax) zmax = v.z;
                got = true;
            }
            double floorY = 0;
            boolean gy = false;
            for (Pk pk : packets) for (Vx v : pk.verts) {
                if (!gy || v.y < floorY) floorY = v.y;
                gy = true;
            }
            double[] shift = {(xmin + xmax) / 2.0, floorY - o.ground / s, (zmin + zmax) / 2.0};
            if (o.modelSpaceEnv) shift = new double[] {0.0, shift[1], 0.0};
            for (int j = 0; j < world.size(); j++) {
                final double[][] w = M4.copy(world.get(j));
                for (int r = 0; r < 3; r++) w[r][3] -= shift[r];
                world.set(j, w);
            }
            for (Pk pk : packets) for (Vx v : pk.verts) {
                v.x = v.x - shift[0];
                v.y = v.y - shift[1];
                v.z = v.z - shift[2];
            }
        }
        if ((Double.isNaN(o.height) || o.height == 0.0) && o.map != null && o.map.scaleHint != null) {
            s = 1.0 / Py.num(o.map.scaleHint);
        }
        // textures (as the merge needs them) need every material's UVs
        for (Pk pk : packets) {
            final List<double[]> l = rawUvs.computeIfAbsent(pk.mobj, k -> new ArrayList<>());
            for (Vx v : pk.verts) l.add(new double[] {v.u, v.v});
        }
        // bone map
        final Map<Integer, Integer> mapping = new HashMap<>();
        int mode = 1;
        if (o.map != null) {
            final Map<String, String> trot = new HashMap<>();
            if ("quad".equals(o.map.cls)) {
                final String[][] parts = {{"upperarm", "thigh"}, {"forearm", "calf"}, {"hand", "foot"}};
                for (String[] pa : parts) {
                    trot.put("bip01 l " + pa[0], "bip01 r " + pa[1]);
                    trot.put("bip01 r " + pa[0], "bip01 l " + pa[1]);
                }
            }
            for (int i = 0; i < o.map.bones.size(); i++) {
                final String bip = o.map.bones.get(i);
                final int jidx = o.map.joints.get(i);
                if (jidx >= 0 && jidx < joints.size()) mapping.put(jidx, Py.lowerHash(trot.getOrDefault(bip, bip)));
            }
            for (int side = 0; side < 2; side++) {
                for (int jidx : side == 0 ? o.map.flapL : o.map.flapR) {
                    if (jidx >= 0 && jidx < joints.size() && !mapping.containsKey(jidx))
                        mapping.put(jidx, Py.lowerHash("guest flap " + (side == 0 ? "l" : "r")));
                }
            }
            if ("fly".equals(o.map.cls)) mode |= 2;
            else if ("float".equals(o.map.cls)) mode |= 4;
        }
        for (int jb : billboard.values()) if (!mapping.containsKey(jb)) mapping.put(jb, Py.lowerHash("guest swirl"));
        // nodes
        final Out nodes = new Out();
        if (!o.texturesOnly) {
            final List<double[][]> bindRows = new ArrayList<>();
            for (double[][] w : world) bindRows.add(toStrikers(w, s));
            for (int k = 0; k < joints.size(); k++) {
                final int par = parent.get(k);
                final double[][] bw = bindRows.get(k);
                double[] trans;
                if (par >= 0) {
                    final double[][] local = M4.mul(bw, M4.inv(bindRows.get(par)));
                    trans = new double[] {local[3][0], local[3][1], local[3][2]};
                } else {
                    trans = new double[] {bw[3][0], bw[3][1], bw[3][2]};
                }
                final double[][] ib = M4.inv(bw);
                nodes.le32(Py.lowerHash("melee/" + name + "/" + k)).le32(par >= 0 ? par : -1);
                for (double t : trans) nodes.lef32((float) t);
                nodes.le32(1);
                for (double[] r : ib) for (double v : r) nodes.lef32((float) v);
                nodes.le32(mapping.getOrDefault(k, 0));
                for (double[] r : bw) for (double v : r) nodes.lef32((float) v);
            }
        }
        decals();
        // merge shapes that share a texture
        final List<Integer> orderTh = new ArrayList<>();
        final List<List<Vx>> mergedV = new ArrayList<>();
        final List<List<int[]>> mergedT = new ArrayList<>();
        final Map<Integer, Integer> current = new HashMap<>();
        final Set<Integer> softTh = new HashSet<>();
        for (Pk pk : packets) {
            int th;
            UvFn uvf;
            if (pk.isDecal) {
                th = pk.decalHash;
                uvf = (u, v) -> new double[] {u, v};
            } else {
                final MatInfo mi = material(pk.mobj, pk.vcol);
                th = mi.hash;
                uvf = mi.fn;
                if (softMobj.contains(pk.mobj)) softTh.add(th);
            }
            Integer key = current.get(th);
            if (key == null || mergedV.get(key).size() + pk.verts.size() > 60000 || mergedT.get(key).size() + pk.ntris() > 20000) {
                key = orderTh.size();
                orderTh.add(th);
                mergedV.add(new ArrayList<>());
                mergedT.add(new ArrayList<>());
                current.put(th, key);
            }
            final List<Vx> mv = mergedV.get(key);
            final List<int[]> mt = mergedT.get(key);
            final int baseI = mv.size();
            for (Vx v : pk.verts) {
                final double[] uv = uvf.map(v.u, v.v);
                mv.add(new Vx(v.x, v.y, v.z, v.wj, v.ww, uv[0], uv[1]));
            }
            for (int t = 0; t < pk.ntris(); t++)
                mt.add(new int[] {pk.tris[t * 3] + baseI, pk.tris[t * 3 + 1] + baseI, pk.tris[t * 3 + 2] + baseI});
        }
        // colo_resize_finish.py's hand fix (Sceptile): a shared picture's record as the picture is (a gas card's
        // recolour had registered it first)
        if (o.retex != null) {
            for (int data : o.retex) {
                final int h = Py.lowerHash(name + "/tex/" + Integer.toHexString(data));
                final Tex.Pic p = plainPics.get(data);
                if (textures.containsKey(h) && p != null) textures.put(h, Tex.record(p.rgba, p.w, p.h));
            }
        }
        final Result res = new Result();
        if (o.texturesOnly) {
            for (Map.Entry<Integer, byte[]> e : textures.entrySet()) {
                res.texHashes.add(e.getKey());
                res.texRecords.add(e.getValue());
            }
            return res;
        }
        // positions to Strikers space, weld
        final List<Geo.Packet> welded = new ArrayList<>(), soft = new ArrayList<>();
        for (int k = 0; k < orderTh.size(); k++) {
            final List<Vx> mv = mergedV.get(k);
            final double[][] P = new double[mv.size()][];
            for (int i = 0; i < P.length; i++) {
                final Vx v = mv.get(i);
                P[i] = new double[] {v.x * s, -v.z * s, v.y * s};
            }
            final Geo.Packet w = Geo.weld(orderTh.get(k), P, mv, mergedT.get(k));
            if (w.tris.length > 0) (softTh.contains(w.th) ? soft : welded).add(w);
        }
        List<Geo.Packet> solid = welded;
        for (int i = 0; i < o.subdivide; i++) solid = Geo.subdivide(solid, 0.5);
        Geo.smoothNormals(solid, 0.5);
        Geo.smoothNormals(soft, 0.5);
        final Out pkb = new Out();
        int npk = 0;
        final List<Geo.Packet> all = new ArrayList<>(solid);
        all.addAll(soft);
        for (Geo.Packet p : all) npk += Geo.emit(pkb, p, softTh.contains(p.th));
        // the file
        final Out f = new Out();
        f.bytes("GPK2".getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));
        f.le32(2).le32(joints.size()).le32(npk).le32(textures.size()).le32(0).lef32((float) (meleeH * s));
        final byte[] nm = name.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        final byte[] n32 = new byte[32];
        System.arraycopy(nm, 0, n32, 0, Math.min(31, nm.length));
        f.bytes(n32).le32(mode);
        f.bytes(nodes.toBytes());
        for (Map.Entry<Integer, byte[]> e : textures.entrySet()) {
            f.le32(e.getKey()).le32(e.getValue().length).bytes(e.getValue()).align(4);
        }
        f.bytes(pkb.toBytes());
        res.gpk = f.toBytes();
        for (Map.Entry<Integer, byte[]> e : textures.entrySet()) {
            res.texHashes.add(e.getKey());
            res.texRecords.add(e.getValue());
        }
        return res;
    }

    /** to_strikers: column-vector Melee matrix -> row-vector Strikers matrix (x, -z, y), translations x s. */
    private static double[][] toStrikers(double[][] worldCol, double s) {
        final double[][] C = {{1, 0, 0, 0}, {0, 0, -1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}};
        final double[][] w = M4.mul(C, worldCol);
        final double[][] row = new double[4][4];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) row[i][j] = w[j][i];
        row[3] = new double[] {row[3][0] * s, row[3][1] * s, row[3][2] * s, 1.0};
        return row;
    }
}
