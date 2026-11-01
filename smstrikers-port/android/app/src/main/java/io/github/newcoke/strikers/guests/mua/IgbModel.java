package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.GxTex;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The models in an IGB (igb.py layer 2, number for number): the skeleton (bind-pose world matrices by numpy's
 * inverse), the skins' and static models' parts (positions, normals, UVs, joints / weights, triangles from strips
 * with the stored normals' winding), and the GX pictures.
 */
public final class IgbModel {
    private IgbModel() {}

    // ------------------------------------------------------------------------------------------------ skeleton
    public static final class Skel {
        public String name;
        public String[] names;
        public int[] parents, bm, flags;
        public double[][] localT;
        public double[][][] invBind, world;
        public int jointCount;
        /** bind_local per bone (W[i] @ inv(W[p]) or W[i]): pose_world's, made once. */
        double[][][] bindLocal;

        public int index(String bone) {
            for (int i = 0; i < names.length; i++) if (names[i].equals(bone)) return i;
            return -1;
        }
    }

    public static Skel skeleton(Igb g) throws IOException {
        final List<Igb.Obj> sks = g.ofType("igSkeleton");
        if (sks.isEmpty()) return null;
        final Igb.Obj skel = sks.get(0);
        final List<Igb.Obj> infos = g.items(skel.obj(4));
        final int n = infos.size();
        final Skel s = new Skel();
        s.name = skel.s(2);
        s.names = new String[n];
        s.parents = new int[n];
        s.bm = new int[n];
        s.flags = new int[n];
        for (int i = 0; i < n; i++) {
            final Igb.Obj b = infos.get(i);
            final String nm = b.s(2);
            s.names[i] = nm != null ? nm : "";
            s.parents[i] = b.i(3, -1);
            s.bm[i] = b.i(4, -1);
            s.flags[i] = b.i(5, 0);
        }
        final Igb.Mem tm = skel.mem(3);
        s.localT = tm != null ? g.memFloats(tm, 3) : new double[n][3];
        final Igb.Mem im = skel.mem(5);
        final double[][] invFlat = im != null ? g.memFloats(im, 16) : new double[0][16];
        s.invBind = new double[invFlat.length][][];
        for (int k = 0; k < invFlat.length; k++) s.invBind[k] = to44(invFlat[k]);
        s.world = new double[n][][];
        for (int i = 0; i < n; i++) {
            if (s.bm[i] >= 0 && s.bm[i] < s.invBind.length) {
                s.world[i] = Np.inv(s.invBind[s.bm[i]]);
            } else {
                final double[][] p = s.parents[i] >= 0 && s.parents[i] < i ? s.world[s.parents[i]] : Np.eye4();
                double[] t = new double[3];
                if (i < s.localT.length) {
                    final double[] lt = s.localT[i];
                    if (Math.abs(lt[0]) < 1e6 && Math.abs(lt[1]) < 1e6 && Math.abs(lt[2]) < 1e6) t = lt.clone();
                }
                final double[][] w = Np.copy(p);
                final double[] r = Np.vm(t, p, 3);
                w[3][0] = r[0] + p[3][0];
                w[3][1] = r[1] + p[3][1];
                w[3][2] = r[2] + p[3][2];
                s.world[i] = w;
            }
        }
        s.jointCount = skel.i(6, 0);
        return s;
    }

    static double[][] to44(double[] f) {
        final double[][] m = new double[4][4];
        for (int r = 0; r < 4; r++) System.arraycopy(f, 4 * r, m[r], 0, 4);
        return m;
    }

    // ------------------------------------------------------------------------------------------------ parts
    public static final class Part {
        public String name, segment;
        public Integer nodeFlags;
        public double[][] pos, nrm, uv, col, uv2;
        public int[][] tris;
        public int prim, nverts;
        public int[][] joints;
        public double[][] weights;
        // inherited state
        public Igb.Obj texattr, envTexattr;
        public boolean textureOn = true, blend;
    }

    private static final Map<Integer, String> COMP = new HashMap<>();

    static {
        COMP.put(1, "position");
        COMP.put(2, "color");
        COMP.put(3, "normal");
        COMP.put(4, "texcoord");
        COMP.put(5, "weight");
        COMP.put(6, "index");
        COMP.put(7, "binormal");
        COMP.put(8, "tangent");
    }

    private static Map<String, double[][]> componentsVa2(Igb g, Igb.Obj va) throws IOException {
        final Map<String, double[][]> comps = new java.util.LinkedHashMap<>();
        final Igb.Obj lst0 = va.obj(3);
        if (lst0 == null) return comps;
        for (Igb.Obj vd : g.items(lst0)) {
            if (vd == null) continue;
            final int ct = vd.i(4, 0);
            final Igb.Obj lst = vd.obj(3);
            if (lst == null) continue;
            if (ct == 12 || ct == 17) throw new IOException("native GX/Wii vertex stream (componentType " + ct + ")");
            double[][] arr = g.listArray(lst);
            int k = vd.i(6, 0);
            if (k == 0) k = 1;
            if (arr.length > 0 && arr[0].length == 1 && k > 1) {
                final int n = arr.length / k;
                final double[][] r = new double[n][k];
                for (int i = 0; i < n; i++) for (int j = 0; j < k; j++) r[i][j] = arr[i * k + j][0];
                arr = r;
            } else if (arr.length == 0 && k > 1) {
                arr = new double[0][k];
            }
            String name = COMP.containsKey(ct) ? COMP.get(ct) : "c" + ct;
            if (comps.containsKey(name)) {
                int c = 0;
                for (String key : comps.keySet()) if (key.startsWith(name)) c++;
                name = name + (c + 1);
            }
            comps.put(name, arr);
        }
        return comps;
    }

    private static Map<String, double[][]> componentsVa11(Igb g, Igb.Obj va) {
        final Map<String, double[][]> comps = new java.util.LinkedHashMap<>();
        final int n = va.i(3, 0);
        final Igb.Mem tab = va.mem(2);
        if (tab == null || n <= 0) return comps;
        final int[] refs = g.memInts(tab);
        final double[][] p = va11(g, refs, 0, 3, n, false);
        if (p != null) comps.put("position", p);
        final double[][] nr = va11(g, refs, 1, 3, n, false);
        if (nr != null) comps.put("normal", nr);
        final double[][] c = va11(g, refs, 2, 4, n, true);
        if (c != null) {
            for (double[] row : c) for (int j = 0; j < 4; j++) row[j] = (float) ((float) row[j] / 255.0f);
            comps.put("color", c);
        }
        for (int t = 0; t < 8; t++) {
            final double[][] uv = va11(g, refs, 11 + t, 2, n, false);
            if (uv != null) comps.put(t == 0 ? "texcoord" : "texcoord" + (t + 1), uv);
        }
        return comps;
    }

    private static double[][] va11(Igb g, int[] refs, int i, int k, int n, boolean bytes) {
        if (i >= refs.length || refs[i] < 0 || !(g.objects[refs[i]] instanceof Igb.Mem)) return null;
        final Igb.Mem m = (Igb.Mem) g.objects[refs[i]];
        final int have = bytes ? m.len / k : m.len / (4 * k);
        if (have < n) return null;
        final double[][] out = new double[n][k];
        for (int r = 0; r < n; r++)
            for (int j = 0; j < k; j++)
                out[r][j] = bytes ? g.d[m.off + r * k + j] & 0xFF : g.lef32(m.off + 4 * (r * k + j));
        return out;
    }

    private static List<int[]> triangles(int prim, int[] lengths, int nverts, int offset, int[] idx) {
        final int[] seq;
        if (idx != null) {
            seq = idx;
        } else {
            seq = new int[Math.max(0, nverts)];
            for (int i = 0; i < seq.length; i++) seq[i] = offset + i;
        }
        final List<int[]> tris = new ArrayList<>();
        if (prim == 3) {
            for (int i = 0; i + 2 < seq.length; i += 3) tris.add(new int[] {seq[i], seq[i + 1], seq[i + 2]});
            return tris;
        }
        final int[] runs = lengths != null && lengths.length > 0 ? lengths : new int[] {seq.length};
        int p = 0;
        for (int n : runs) {
            final int s0 = p;
            p += n;
            for (int i = 0; i < n - 2; i++) {
                if (prim == 4) {
                    if (i % 2 == 0) tris.add(new int[] {seq[s0 + i], seq[s0 + i + 1], seq[s0 + i + 2]});
                    else tris.add(new int[] {seq[s0 + i + 1], seq[s0 + i], seq[s0 + i + 2]});
                } else if (prim == 5) {
                    tris.add(new int[] {seq[s0], seq[s0 + i + 1], seq[s0 + i + 2]});
                }
            }
        }
        return tris;
    }

    /** One igGeometryAttr2 / igGeometryAttr1_5. */
    static Part geometryPart(Igb g, Igb.Obj gattr, int[] palette, Map<Integer, Integer> bmToBone) throws IOException {
        final Igb.Obj va = gattr.obj(4);
        if (va == null) return null;
        final Map<String, double[][]> comps;
        if (va.type.equals("igVertexArray2")) comps = componentsVa2(g, va);
        else if (va.type.startsWith("igVertexArray1")) comps = componentsVa11(g, va);
        else throw new IOException("vertex array type " + va.type);
        final double[][] pos = comps.get("position");
        if (pos == null) return null;
        final int n = pos.length;
        final int prim, offset;
        final Igb.Obj pla;
        if (gattr.type.equals("igGeometryAttr2")) {
            prim = gattr.i(7, 0);
            pla = gattr.obj(6);
            offset = gattr.i(9, 0);
        } else {
            prim = gattr.i(6, 0);
            pla = gattr.obj(13);
            offset = gattr.i(8, 0);
        }
        int[] lengths = null;
        if (pla != null && pla.mem(2) != null) {
            int bits = pla.i(4, 0);
            if (bits == 0) bits = 32;
            final Igb.Mem m = pla.mem(2);
            final int[] all = bits == 32 ? g.memInts(m) : g.memU16(m);
            final int cnt = Math.min(all.length, Math.max(0, pla.i(3, 0)));
            lengths = java.util.Arrays.copyOf(all, cnt);
        }
        int[] idx = null;
        final Igb.Obj ia = gattr.obj(5);
        if (ia != null) {
            for (Object t : ia.refs()) {
                if (t instanceof Igb.Mem) {
                    idx = g.memU16((Igb.Mem) t);
                    break;
                }
            }
        }
        int off = offset;
        if (idx == null) {
            long sum = 0;
            if (lengths != null && lengths.length > 0 && prim != 3) for (int l : lengths) sum += l;
            if (off + sum > n) off = 0;
        }
        List<int[]> tl = triangles(prim, lengths, n - off, off, idx);
        // drop degenerate triangles, wind each like its stored normals
        final double[][] nrm = comps.get("normal");
        final List<int[]> kept = new ArrayList<>();
        for (int[] t : tl) {
            if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
            final double[] a = pos[t[0]], b = pos[t[1]], c = pos[t[2]];
            final double[] u = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            final double[] v = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            final double[] cr = Np.cross(u, v);
            final double area = Math.sqrt((cr[0] * cr[0] + cr[1] * cr[1]) + cr[2] * cr[2]);
            if (!(area > 1e-12)) continue;
            int[] tt = t;
            if (nrm != null) {
                final double vx = (nrm[t[0]][0] + nrm[t[1]][0]) + nrm[t[2]][0];
                final double vy = (nrm[t[0]][1] + nrm[t[1]][1]) + nrm[t[2]][1];
                final double vz = (nrm[t[0]][2] + nrm[t[1]][2]) + nrm[t[2]][2];
                final double agree = (cr[0] * vx + cr[1] * vy) + cr[2] * vz;
                if (agree < 0) tt = new int[] {t[0], t[2], t[1]};
            }
            kept.add(tt);
        }
        final Part part = new Part();
        part.pos = pos;
        part.tris = kept.toArray(new int[0][]);
        part.prim = prim;
        part.nverts = n;
        part.nrm = nrm;
        part.uv = comps.get("texcoord");
        part.col = comps.get("color");
        part.uv2 = comps.get("texcoord2");
        final double[][] pi = comps.get("index");
        if (pi != null) {
            final int k = pi.length > 0 ? pi[0].length : 0;
            part.joints = new int[pi.length][k];
            for (int r = 0; r < pi.length; r++) {
                for (int j = 0; j < k; j++) {
                    final int c = (int) pi[r][j];
                    final int bmi = c >= 0 && c < palette.length ? palette[c] : -1;
                    final Integer bone = bmToBone.get(bmi);
                    part.joints[r][j] = bone != null ? bone : -1;
                }
            }
            double[][] w = comps.get("weight");
            if (w == null) {
                w = new double[pi.length][k];
                for (double[] row : w) java.util.Arrays.fill(row, 1.0);
            }
            part.weights = w;
        }
        return part;
    }

    // ------------------------------------------------------------------------------------------------ graphs
    private static final class State {
        Igb.Obj texattr, envTexattr;
        boolean textureOn = true, blend;

        State copy() {
            final State s = new State();
            s.texattr = texattr;
            s.envTexattr = envTexattr;
            s.textureOn = textureOn;
            s.blend = blend;
            return s;
        }
    }

    /** Every igSkin's parts, in order. */
    public static List<Part> skins(Igb g, Skel sk) throws IOException {
        final Map<Integer, Integer> bmToBone = new HashMap<>();
        if (sk != null) for (int i = 0; i < sk.bm.length; i++) if (sk.bm[i] >= 0) bmToBone.put(sk.bm[i], i);
        final List<Part> out = new ArrayList<>();
        for (Igb.Obj s : g.ofType("igSkin")) out.addAll(walkGraph(g, s.obj(3), bmToBone));
        return out;
    }

    /** Static models (bolt-ons, props): igSceneInfo graphs, igTransforms applied. */
    public static List<Part> sceneMeshes(Igb g) throws IOException {
        final List<Part> out = new ArrayList<>();
        for (Igb.Obj si : g.ofType("igSceneInfo")) out.addAll(walkGraph(g, si.obj(5), new HashMap<>()));
        return out;
    }

    static List<Part> walkGraph(Igb g, Igb.Obj root, Map<Integer, Integer> bmToBone) throws IOException {
        final List<Part> parts = new ArrayList<>();
        walk(g, root, new State(), null, null, null, 0, bmToBone, parts);
        return parts;
    }

    private static int[] identityPalette() {
        final int[] p = new int[256];
        for (int i = 0; i < 256; i++) p[i] = i;
        return p;
    }

    private static void walk(Igb g, Igb.Obj node, State state, int[] palette, String segment, double[][] mat, int depth,
                             Map<Integer, Integer> bmToBone, List<Part> parts) throws IOException {
        if (node == null || depth > 200) return;
        final State st = state.copy();
        final String t = node.type;
        if (t.equals("igBlendMatrixSelect") && node.obj(10) != null) {
            final double[][] a = g.listArray(node.obj(10));
            int cnt = 0;
            for (double[] row : a) cnt += row.length;
            palette = new int[cnt];
            int k = 0;
            for (double[] row : a) for (double v : row) palette[k++] = (int) v;
        }
        if (t.equals("igSegment")) segment = node.s(2);
        if (t.equals("igEnvironmentMapShader2")) {
            final Igb.Obj tb = node.obj(9);
            if (tb != null && tb.type.equals("igTextureBindAttr")) st.envTexattr = tb.obj(4);
        }
        if (t.equals("igTransform")) {
            final double[] m16 = node.vec(8);
            if (m16 != null && m16.length == 16) {
                final double[][] local = to44(m16);
                mat = mat == null ? local : Np.mm(local, mat);
            }
        }
        final List<Igb.Obj> geo = new ArrayList<>();
        if ("igObjectRefMetaField".equals(node.fieldType(8))) {
            final List<Igb.Obj> attrs = g.items(node.obj(8));
            if (attrs != null) {
                for (Igb.Obj a : attrs) {
                    if (a == null) continue;
                    if (a.type.startsWith("igGeometryAttr")) geo.add(a);
                    else if (a.type.equals("igTextureBindAttr")) st.texattr = a.obj(4);
                    else if (a.type.equals("igTextureStateAttr")) st.textureOn = a.i(4, 0) != 0;
                    else if (a.type.equals("igBlendStateAttr")) st.blend = a.i(4, 0) != 0;
                }
            }
        }
        for (Igb.Obj ga : geo) {
            final Part part = geometryPart(g, ga, palette != null && palette.length > 0 ? palette : identityPalette(), bmToBone);
            if (part == null) continue;
            if (mat != null && !allclose(mat)) {
                final double[][] p = Np.mm(part.pos, Np.rot(mat));
                for (double[] row : p) {
                    row[0] = row[0] + mat[3][0];
                    row[1] = row[1] + mat[3][1];
                    row[2] = row[2] + mat[3][2];
                }
                part.pos = f32(p);
                if (part.nrm != null) {
                    final double[][] nn = Np.mm(part.nrm, Np.rot(mat));
                    for (double[] row : nn) {
                        final double ln = Math.max(Math.sqrt((row[0] * row[0] + row[1] * row[1]) + row[2] * row[2]), 1e-9);
                        row[0] /= ln;
                        row[1] /= ln;
                        row[2] /= ln;
                    }
                    part.nrm = f32(nn);
                }
                if (det3(mat) < 0) for (int[] tr : part.tris) {
                    final int x = tr[1];
                    tr[1] = tr[2];
                    tr[2] = x;
                }
            }
            part.name = node.s(2) != null ? node.s(2) : "";
            part.segment = segment;
            part.nodeFlags = node.i(5);
            part.texattr = st.texattr;
            part.envTexattr = st.envTexattr;
            part.textureOn = st.textureOn;
            part.blend = st.blend;
            parts.add(part);
        }
        if ("igObjectRefMetaField".equals(node.fieldType(7))) {
            final List<Igb.Obj> kids = g.items(node.obj(7));
            if (kids != null) for (Igb.Obj c : kids) walk(g, c, st, palette, segment, mat, depth + 1, bmToBone, parts);
        }
    }

    private static double[][] f32(double[][] a) {
        for (double[] row : a) for (int j = 0; j < row.length; j++) row[j] = (float) row[j];
        return a;
    }

    private static boolean allclose(double[][] m) {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                final double b = r == c ? 1.0 : 0.0;
                if (!(Math.abs(m[r][c] - b) <= 1e-8 + 1e-5 * Math.abs(b))) return false;
            }
        return true;
    }

    private static double det3(double[][] m) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    // ------------------------------------------------------------------------------------------------ pictures
    /** A picture: RGBA top row first. */
    public static final class Pic {
        public final int w, h;
        public final byte[] rgba;
        public final String name;

        Pic(int w, int h, byte[] rgba, String name) {
            this.w = w;
            this.h = h;
            this.rgba = rgba;
            this.name = name;
        }
    }

    /** igb.image_data + decode_image for an igTextureAttr. */
    public static Pic image(Igb g, Igb.Obj texattr) throws IOException {
        final Igb.Obj img = texattr.obj(12);
        Igb.Mem raw = null;
        final Igb.Obj lst = texattr.obj(19);
        if (lst != null && lst.mem(4) != null) raw = lst.mem(4);
        else if (img != null && img.mem(13) != null) raw = img.mem(13);
        if (img == null) throw new IOException("texture attr without an image");
        final int w = img.i(2, 0), h = img.i(3, 0), pf = img.i(6, 0);
        if (pf != 101) throw new IOException("igImage format " + pf + " not mapped (" + img.s(22) + ")");
        final int need = GxTex.sizeOf(GxTex.RGB5A3, w, h);
        if (raw == null || raw.len < need) throw new IOException("image data short");
        return new Pic(w, h, GxTex.decode(g.d, raw.off, GxTex.RGB5A3, w, h, null), img.s(22) != null ? img.s(22) : "");
    }

    /** igb.images: every igTextureAttr's picture, in order. */
    public static List<Pic> images(Igb g) throws IOException {
        final List<Pic> out = new ArrayList<>();
        for (Igb.Obj ta : g.ofType("igTextureAttr")) {
            if (ta.obj(12) == null) continue;
            out.add(image(g, ta));
        }
        return out;
    }
}
