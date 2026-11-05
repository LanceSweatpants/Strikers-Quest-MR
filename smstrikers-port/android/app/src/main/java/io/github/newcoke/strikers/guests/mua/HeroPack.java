package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * A hero costume's match pack, as tools/outside/mua/mua2gpk.py made it and mua_cels.py added its celebrations
 * (GPK2, retarget mode): the hero's own skeleton whole (Bip01 bones the Strikers skeleton has follow it), its skin's
 * parts (segments dropped unless kept) welded, subdivided once, smooth normals, strips, its pictures as RGBA8, its
 * own idle / run as clips, then own_cel1-4 from its power moves.
 */
public final class HeroPack {
    private HeroPack() {}

    static final Set<String> STRIKERS = new HashSet<>();

    static {
        for (String b : new String[] {"bip01", "bip01 pelvis", "bip01 spine", "bip01 spine1", "bip01 neck", "bip01 head",
            "bip01 l clavicle", "bip01 r clavicle", "bip01 l upperarm", "bip01 r upperarm", "bip01 l forearm",
            "bip01 r forearm", "bip01 l hand", "bip01 r hand", "bip01 l thigh", "bip01 r thigh", "bip01 l calf",
            "bip01 r calf", "bip01 l foot", "bip01 r foot", "bip01 l toe0", "bip01 r toe0"}) STRIKERS.add(b);
        for (String side : new String[] {"l", "r"})
            for (String f : new String[] {"0", "01", "1", "11", "2", "21", "3", "31", "4", "41"})
                STRIKERS.add("bip01 " + side + " finger" + f);
    }

    /** MUA faces +X, Strikers guests -Y: p' = (y, -x, z). */
    static final double[][] R3 = {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    static final Pattern MAIN_SET = Pattern.compile("actors/\\d+_[a-z_]+\\.igb");

    /** A costume's package opened: its skeleton, the skin IGB, its animation sets. */
    static final class Costume {
        Wad.Fb fb;
        Igb skin;
        IgbModel.Skel sk;
        Igb main, combat;
    }

    static Costume open(Wad.Fb fb, String cid) throws IOException {
        final Costume c = new Costume();
        c.fb = fb;
        String skin = "actors/" + cid + ".igb";
        if (!fb.has(skin)) {
            int best = -1;
            String bn = null;
            for (String n : fb.names()) {
                if (!"actorskin".equals(fb.type(n))) continue;
                final int len = fb.size(n);
                if (len > best || (len == best && n.compareTo(bn) > 0)) {
                    best = len;
                    bn = n;
                }
            }
            if (bn == null) throw new IOException("no actor skin");
            skin = bn;
        }
        c.skin = new Igb(fb.data(skin));
        c.sk = IgbModel.skeleton(c.skin);
        byte[] main = null, combat = null;
        for (String n : fb.names()) {
            if (!"actoranimdb".equals(fb.type(n))) continue;
            if (MAIN_SET.matcher(n).matches()) main = fb.data(n);
            if (combat == null && n.endsWith("_4_combat.igb")) combat = fb.data(n);
        }
        c.main = main != null ? new Igb(main) : null;
        c.combat = combat != null ? new Igb(combat) : null;
        return c;
    }

    // ------------------------------------------------------------------------------------------------ mua2gpk
    /** The pack mua2gpk.py makes (own_idle / own_run included), before the celebrations. */
    static byte[] base(Hero h, int costume, Costume c, Igb shared) throws IOException {
        final String name = h.pack(costume);
        final IgbModel.Skel sk = c.sk;
        final Igb g = c.skin;
        final List<IgbModel.Part> all = IgbModel.skins(g, sk);
        final List<IgbModel.Part> parts = new ArrayList<>();
        for (IgbModel.Part p : all) {
            if (p.segment == null || p.segment.isEmpty()) {
                parts.add(p);
                continue;
            }
            for (String k : h.keep) {
                if (p.segment.contains(k)) {
                    parts.add(p);
                    break;
                }
            }
        }
        boolean none = true;
        for (IgbModel.Part p : parts) if (p.texattr != null) none = false;
        if (none) for (IgbModel.Part p : parts) if (p.envTexattr != null) p.texattr = p.envTexattr;

        // size: the bind mesh's height -> h.height, feet on 0
        double zmin = Double.NaN, zmax = Double.NaN;
        for (IgbModel.Part p : parts) {
            for (double[] v : p.pos) {
                if (Double.isNaN(zmin) || v[2] < zmin) zmin = v[2];
                if (Double.isNaN(zmax) || v[2] > zmax) zmax = v[2];
            }
        }
        final double s = h.height / (zmax - zmin);
        final double fz = zmin;

        // r135 (the NPC guests, mua2gpk.py --bolton / --rest-bind / --unmap / --reparent; see Npc): a bolt-on's nodes
        // after the skin's (Nightcrawler's tail), the binds at a rest pose with the modelled ones as the skin's inverse
        // binds, Strikers bones left unfollowed, a bone hung on another parent. No hero has any of them.
        final int n0 = sk.names.length;
        final Npc.Bolt bolt = Npc.bolt(h, c);
        final int nb = n0 + (bolt != null ? bolt.names.length : 0);
        final String[] bnames = new String[nb];
        final int[] parents = new int[nb];
        final double[][][] worldsM = new double[nb][][], restsM = new double[nb][][];
        final boolean restBind = h.more.containsKey("rest");
        final double[][][] rest0 = restBind ? Npc.rest(h, c) : sk.world;
        for (int i = 0; i < n0; i++) {
            bnames[i] = sk.names[i];
            parents[i] = sk.parents[i];
            worldsM[i] = sk.world[i];
            restsM[i] = rest0[i];
        }
        final String reparent = h.more.get("reparent");
        if (reparent != null) {
            for (String pair : reparent.split(";")) {
                final String[] cp = pair.split(":");
                int ci = -1, ni = -1;
                for (int i = 0; i < n0; i++) {
                    final String lo = sk.names[i].toLowerCase(Locale.ROOT);
                    if (ci < 0 && lo.equals(cp[0].toLowerCase(Locale.ROOT))) ci = i;
                    if (ni < 0 && lo.equals(cp[1].toLowerCase(Locale.ROOT))) ni = i;
                }
                if (ci < 0 || ni < 0) throw new IOException(h.pack + ": reparent " + pair);
                parents[ci] = ni;
            }
        }
        final java.util.Set<IgbModel.Part> boltParts = java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>());
        if (bolt != null) {
            for (int j = 0; j < bolt.names.length; j++) {
                bnames[n0 + j] = bolt.names[j];
                parents[n0 + j] = bolt.parents[j];
                worldsM[n0 + j] = bolt.worlds[j];
                restsM[n0 + j] = bolt.rests[j];
            }
            parts.addAll(bolt.parts);
            boltParts.addAll(bolt.parts);
        }
        final double[][][] world = new double[nb][][], skinW = new double[nb][][];
        for (int i = 0; i < nb; i++) {
            skinW[i] = toStrikers(worldsM[i], fz, s);
            world[i] = i >= n0 || restBind ? toStrikers(restsM[i], fz, s) : skinW[i];
        }
        final java.util.Set<String> unmap = new java.util.HashSet<>();
        if (h.more.containsKey("unmap")) for (String u : h.more.get("unmap").toLowerCase(Locale.ROOT).split(",")) unmap.add(u);
        final Map<Integer, String> mapping = new LinkedHashMap<>();
        for (int i = 0; i < nb; i++) {
            final String lo = bnames[i].toLowerCase(Locale.ROOT);
            if (STRIKERS.contains(lo) && !unmap.contains(lo)) mapping.put(i, lo);
        }

        // textures
        final Map<String, byte[]> textures = new LinkedHashMap<>();
        final List<String> texOfPart = new ArrayList<>();
        for (IgbModel.Part p : parts) {
            final Igb.Obj ta = p.textureOn ? p.texattr : null;
            if (ta == null) {
                texOfPart.add(null);
                continue;
            }
            final Igb src = boltParts.contains(p) ? bolt.model : g;  // (r135: a bolt-on's pictures are in its own IGB)
            final String key = (src == g ? "t" : "b") + ta.index;
            if (!textures.containsKey(key)) {
                final IgbModel.Pic pic = IgbModel.image(src, ta);
                textures.put(key, Tex.record(pic.rgba, pic.w, pic.h));
            }
            texOfPart.add(key);
        }

        // packets per texture: triangles turned GX-way (pointing in)
        final Map<String, List<Object>> byTex = new LinkedHashMap<>();
        for (int pi = 0; pi < parts.size(); pi++) {
            final IgbModel.Part p = parts.get(pi);
            final String key = texOfPart.get(pi);
            if (key == null) continue;
            final int n = p.pos.length;
            final double[][] P = new double[n][];
            for (int k = 0; k < n; k++) P[k] = toS(p.pos[k], fz, s);
            double[][] nsrc = null;
            if (p.nrm != null) {
                nsrc = new double[p.nrm.length][];
                for (int k = 0; k < p.nrm.length; k++) nsrc[k] = Np.vm(p.nrm[k], R3);
            }
            final int[][] wj = new int[n][];
            final double[][] ww = new double[n][];
            final double[] u = new double[n], v = new double[n];
            for (int k = 0; k < n; k++) {
                final List<int[]> js = new ArrayList<>();
                final List<Double> ws = new ArrayList<>();
                if (p.joints != null) {
                    for (int q = 0; q < p.joints[k].length; q++) {
                        final int j = p.joints[k][q];
                        final double w = p.weights[k][q];
                        if (j >= 0 && w > 0) {
                            js.add(new int[] {j});
                            ws.add(w);
                        }
                    }
                }
                wj[k] = new int[js.size()];
                ww[k] = new double[js.size()];
                for (int q = 0; q < js.size(); q++) {
                    wj[k][q] = js.get(q)[0];
                    ww[k][q] = ws.get(q);
                }
                if (p.uv != null) {
                    u[k] = p.uv[k][0];
                    v[k] = p.uv[k][1];
                }
            }
            final List<int[]> tris = new ArrayList<>();
            for (int[] t : p.tris) {
                int a = t[0], b = t[1], cc = t[2];
                if (a == b || b == cc || a == cc) continue;
                final double[] e1 = {P[b][0] - P[a][0], P[b][1] - P[a][1], P[b][2] - P[a][2]};
                final double[] e2 = {P[cc][0] - P[a][0], P[cc][1] - P[a][1], P[cc][2] - P[a][2]};
                final double[] fn = Np.cross(e1, e2);
                if (nsrc != null) {
                    final double[] ns = new double[3];
                    for (int x = 0; x < 3; x++) ns[x] = (nsrc[a][x] + nsrc[b][x]) + nsrc[cc][x];
                    if (Np.dot(fn, ns) > 0) {
                        final int y = b;
                        b = cc;
                        cc = y;
                    }
                }
                tris.add(new int[] {a, b, cc});
            }
            final List<Object> acc = byTex.computeIfAbsent(key, x -> {
                final List<Object> l = new ArrayList<>();
                l.add(new ArrayList<double[]>());
                l.add(new ArrayList<int[]>());
                l.add(new ArrayList<double[]>());
                l.add(new ArrayList<double[]>());
                l.add(new ArrayList<int[]>());
                return l;
            });
            @SuppressWarnings("unchecked") final List<double[]> aP = (List<double[]>) acc.get(0);
            @SuppressWarnings("unchecked") final List<int[]> aJ = (List<int[]>) acc.get(1);
            @SuppressWarnings("unchecked") final List<double[]> aW = (List<double[]>) acc.get(2);
            @SuppressWarnings("unchecked") final List<double[]> aUv = (List<double[]>) acc.get(3);
            @SuppressWarnings("unchecked") final List<int[]> aT = (List<int[]>) acc.get(4);
            final int base = aP.size();
            for (int k = 0; k < n; k++) {
                aP.add(P[k]);
                aJ.add(wj[k]);
                aW.add(ww[k]);
                aUv.add(new double[] {u[k], v[k]});
            }
            for (int[] t : tris) aT.add(new int[] {t[0] + base, t[1] + base, t[2] + base});
        }
        List<MGeo.Packet> welded = new ArrayList<>();
        for (Map.Entry<String, List<Object>> e : byTex.entrySet()) {
            @SuppressWarnings("unchecked") final List<double[]> aP = (List<double[]>) e.getValue().get(0);
            @SuppressWarnings("unchecked") final List<int[]> aJ = (List<int[]>) e.getValue().get(1);
            @SuppressWarnings("unchecked") final List<double[]> aW = (List<double[]>) e.getValue().get(2);
            @SuppressWarnings("unchecked") final List<double[]> aUv = (List<double[]>) e.getValue().get(3);
            @SuppressWarnings("unchecked") final List<int[]> aT = (List<int[]>) e.getValue().get(4);
            final int n = aP.size();
            final double[] u = new double[n], v = new double[n];
            for (int k = 0; k < n; k++) {
                u[k] = aUv.get(k)[0];
                v[k] = aUv.get(k)[1];
            }
            welded.add(MGeo.weld(e.getKey(), aP.toArray(new double[0][]), aJ.toArray(new int[0][]),
                    aW.toArray(new double[0][]), u, v, aT));
        }
        welded = MGeo.subdivide(welded, 0.5);
        MGeo.smoothNormals(welded, 0.5);
        final Out pk = new Out();
        int npk = 0;
        for (MGeo.Packet p : welded) npk += emit(pk, name, p);

        // own idle / run
        final List<Object[]> clips = new ArrayList<>();
        final List<String> want = h.runs(costume);
        final List<Igb> sets = new ArrayList<>();
        if (c.main != null) sets.add(c.main);
        if (c.combat != null) sets.add(c.combat);
        if (shared != null) sets.add(shared);
        final Set<String> found = new HashSet<>();
        for (Igb ag : sets) {
            for (Igb.Obj an : ag.ofType("igAnimation")) {
                final String an2 = an.s(2);
                if (!"idle".equals(an2) && !"run".equals(an2)) continue;
                if (found.contains(an2)) continue;
                if (an2.equals("run") && h.sharedRun && ag != shared) continue;
                // (a pack made by an older tool has fewer own clips: Iceman's no run, before MUA's shared set)
                if (want != null && !want.contains(an2)) continue;
                found.add(an2);
                final IgbAnim.Clip cl = IgbAnim.clip(ag, an2);
                final double dur = cl.dur;
                if (dur <= 0) continue;
                final int nk = Math.max(2, (int) Math.rint(dur * 30) + 1);
                final byte[] keys = new byte[nk * nb * 28];
                int o = 0;
                for (int k = 0; k < nk; k++) {
                    final double t = k < nk - 1 ? dur * k / (nk - 1) : 0.0;
                    final double[][][] wp = IgbAnim.poseWorld(sk, cl.tracks, t);
                    for (int i = 0; i < n0; i++) {
                        final int par = parents[i];  // (r135: the pack's parents, --reparent)
                        double[] q;
                        double[] tr;
                        if (par < 0) {
                            q = rotToQuatPow(Np.rot(Np.eye4()));
                            tr = new double[] {0.0, 0.0, 0.0};
                        } else {
                            final double[][] L = Np.mm(wp[i], Np.inv(wp[par]));
                            if (parents[par] < 0) L[3][2] -= fz;
                            q = rotToQuatPow(Np.rot(L));
                            tr = new double[] {L[3][0] * s, L[3][1] * s, L[3][2] * s};
                        }
                        for (int j = 0; j < 4; j++) o = putF(keys, o, q[j]);
                        for (int j = 0; j < 3; j++) o = putF(keys, o, tr[j]);
                    }
                    if (bolt != null) {
                        // r135 (mua2gpk.bolton_keys): the bolt-on's nodes from its own set's clip of this name, at the
                        // same fraction of its length, else its bind
                        final IgbAnim.Clip bc = IgbAnim.clip(bolt.anims, an2);
                        final double[][][] bw = bc != null ? IgbAnim.poseWorld(bolt.sk, bc.tracks, t / dur * bc.dur) : bolt.sk.world;
                        for (int j = 0; j < bolt.sk.names.length; j++) {
                            final int par = bolt.sk.parents[j];
                            final double[][] L = par < 0 ? bw[j] : Np.mm(bw[j], Np.inv(bw[par]));
                            final double[] q = rotToQuatPow(Np.rot(L));
                            for (int x = 0; x < 4; x++) o = putF(keys, o, q[x]);
                            for (int x = 0; x < 3; x++) o = putF(keys, o, L[3][x] * s);
                        }
                    }
                }
                clips.add(new Object[] {an2.equals("idle") ? "own_idle" : "own_run", nk, keys});
            }
        }

        // nodes
        final Out nodes = new Out();
        for (int k = 0; k < nb; k++) {
            final double[][] bw = world[k];
            final int par = parents[k];
            final double[] trans;
            if (par >= 0) {
                final double[][] local = Np.mm(bw, Np.inv(world[par]));
                trans = new double[] {local[3][0], local[3][1], local[3][2]};
            } else {
                trans = new double[] {bw[3][0], bw[3][1], bw[3][2]};
            }
            final double[][] ib = Np.inv(skinW[k]);
            nodes.le32(Tex.lowerHash("mua/" + name + "/" + bnames[k])).le32(par);
            for (double x : trans) nodes.lef32((float) x);
            nodes.le32(1);
            for (double[] r : ib) for (double x : r) nodes.lef32((float) x);
            final String target = mapping.get(k);
            nodes.le32(target != null ? Tex.lowerHash(target) : 0);
            for (double[] r : bw) for (double x : r) nodes.lef32((float) x);
        }
        final Out texs = new Out();
        for (Map.Entry<String, byte[]> e : textures.entrySet()) {
            texs.le32(Tex.lowerHash("mua/" + name + "/tex/" + e.getKey())).le32(e.getValue().length).bytes(e.getValue()).align(4);
        }
        final Out anims = new Out();
        for (Object[] cl : clips) {
            anims.fixed((String) cl[0], 48).le32((Integer) cl[1]).bytes((byte[]) cl[2]);
            for (int i = 0; i < nb; i++) anims.u8(1);
            anims.align(4);
        }
        final Out f = new Out();
        f.bytes("GPK2".getBytes(StandardCharsets.ISO_8859_1)).le32(2).le32(nb).le32(npk).le32(textures.size())
                .le32(clips.size()).lef32((float) h.height);
        f.fixed(truncate(name, 31), 32).le32(h.more.containsKey("float") ? 5 : 1);  // (r135 --float: Professor X)
        f.bytes(nodes.toBytes()).bytes(texs.toBytes()).bytes(pk.toBytes()).bytes(anims.toBytes());
        return f.toBytes();
    }

    private static String truncate(String s, int n) {
        final byte[] b = s.getBytes(StandardCharsets.UTF_8);
        return b.length <= n ? s : new String(b, 0, n, StandardCharsets.ISO_8859_1);
    }

    /** A MUA model-space bone matrix in the pack's space: its turn @ R3, its place toS (mua2gpk's bones). */
    static double[][] toStrikers(double[][] W, double zmin, double s) {
        final double[][] rot = Np.mm(Np.rot(W), R3);
        final double[] t = toS(W[3], zmin, s);
        final double[][] wn = Np.eye4();
        for (int a = 0; a < 3; a++) System.arraycopy(rot[a], 0, wn[a], 0, 3);
        System.arraycopy(t, 0, wn[3], 0, 3);
        return wn;
    }

    /** MUA model point -> Strikers model point: ((p - (0, 0, zmin)) @ R3) * s. */
    static double[] toS(double[] p, double zmin, double s) {
        final double[] q = {p[0] - 0.0, p[1] - 0.0, p[2] - zmin};
        final double[] r = Np.vm(q, R3);
        return new double[] {r[0] * s, r[1] * s, r[2] * s};
    }

    static int putF(byte[] b, int o, double v) {
        final int x = Float.floatToRawIntBits((float) v);
        b[o] = (byte) x;
        b[o + 1] = (byte) (x >> 8);
        b[o + 2] = (byte) (x >> 16);
        b[o + 3] = (byte) (x >> 24);
        return o + 4;
    }

    /** One packet record (two halves when the strip is too long); returns how many. */
    static int emit(Out o, String name, MGeo.Packet p) throws IOException {
        final int[] strip = MGeo.stripify(p.tris);
        if (strip.length > 65535 && p.ntris() > 1) {
            final int hh = p.ntris() / 2;
            int n = 0;
            for (int part = 0; part < 2; part++) {
                final int t0 = part == 0 ? 0 : hh, t1 = part == 0 ? hh : p.ntris();
                final java.util.TreeSet<Integer> usedS = new java.util.TreeSet<>();
                for (int t = t0; t < t1; t++) for (int q = 0; q < 3; q++) usedS.add(p.tris[t * 3 + q]);
                final int[] used = new int[usedS.size()];
                int i = 0;
                for (int x : usedS) used[i++] = x;
                final Map<Integer, Integer> re = new java.util.HashMap<>();
                for (int k = 0; k < used.length; k++) re.put(used[k], k);
                final MGeo.Packet q = new MGeo.Packet();
                q.key = p.key;
                q.P = new double[used.length][];
                q.wj = new int[used.length][];
                q.ww = new double[used.length][];
                q.u = new double[used.length];
                q.v = new double[used.length];
                q.N = new double[used.length][];
                for (int k = 0; k < used.length; k++) {
                    q.P[k] = p.P[used[k]];
                    q.wj[k] = p.wj[used[k]];
                    q.ww[k] = p.ww[used[k]];
                    q.u[k] = p.u[used[k]];
                    q.v[k] = p.v[used[k]];
                    q.N[k] = p.N[used[k]];
                }
                q.tris = new int[(t1 - t0) * 3];
                for (int t = t0; t < t1; t++) for (int c = 0; c < 3; c++) q.tris[(t - t0) * 3 + c] = re.get(p.tris[t * 3 + c]);
                n += emit(o, name, q);
            }
            return n;
        }
        if (!MGeo.checkStrip(strip, p.tris)) throw new IOException("strip check failed");
        o.le32(Tex.lowerHash("mua/" + name + "/tex/" + p.key)).le32(p.n()).le32(strip.length);
        for (int vi = 0; vi < p.n(); vi++) {
            final double[] P = p.P[vi], N = p.N[vi];
            // sorted by weight (stable), the 4 biggest, renormalised
            final int m = p.wj[vi].length;
            final Integer[] ord = new Integer[m];
            for (int i = 0; i < m; i++) ord[i] = i;
            for (int i = 1; i < m; i++) {
                final int x = ord[i];
                int k = i - 1;
                while (k >= 0 && -p.ww[vi][ord[k]] > -p.ww[vi][x]) {
                    ord[k + 1] = ord[k];
                    k--;
                }
                ord[k + 1] = x;
            }
            final int top = Math.min(4, m);
            final double[] tw = new double[top];
            for (int i = 0; i < top; i++) tw[i] = p.ww[vi][ord[i]];
            double tot = Np.pySum(tw, top);
            if (tot == 0.0) tot = 1.0;
            o.lef32((float) P[0]).lef32((float) P[1]).lef32((float) P[2]);
            o.lef32((float) N[0]).lef32((float) N[1]).lef32((float) N[2]);
            o.lef32((float) p.u[vi]).lef32((float) p.v[vi]);
            for (int q = 0; q < 4; q++) {
                if (q < top) {
                    final int j = p.wj[vi][ord[q]];
                    if (j > 255) throw new IOException("joint index " + j + " past 255");
                    o.u8(j);
                } else {
                    o.u8(0xFF);
                }
            }
            for (int q = 0; q < 4; q++) o.lef32(q < top ? (float) (tw[q] / tot) : 0.0f);
        }
        for (int x : strip) o.le16(x);
        o.align(4);
        return 1;
    }

    /** mua2gpk.rot_to_quat (row-vector 3 x 3 -> x y z w; its ** 0.5 as sqrt, its sum over numpy values plain). */
    static double[] rotToQuatPow(double[][] m) {
        final double r00 = m[0][0], r11 = m[1][1], r22 = m[2][2];
        final double r21 = m[1][2], r12 = m[2][1], r02 = m[2][0], r20 = m[0][2], r10 = m[0][1], r01 = m[1][0];
        final double tr = r00 + r11 + r22;
        double[] q;
        if (tr > 0) {
            final double s = Math.sqrt(tr + 1.0) * 2;
            q = new double[] {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25 * s};
        } else if (r00 > r11 && r00 > r22) {
            final double s = Math.sqrt(1.0 + r00 - r11 - r22) * 2;
            q = new double[] {0.25 * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
        } else if (r11 > r22) {
            final double s = Math.sqrt(1.0 + r11 - r00 - r22) * 2;
            q = new double[] {(r01 + r10) / s, 0.25 * s, (r12 + r21) / s, (r02 - r20) / s};
        } else {
            final double s = Math.sqrt(1.0 + r22 - r00 - r11) * 2;
            q = new double[] {(r02 + r20) / s, (r12 + r21) / s, 0.25 * s, (r10 - r01) / s};
        }
        final double n = Math.sqrt(Np.npSum(new double[] {q[0] * q[0], q[1] * q[1], q[2] * q[2], q[3] * q[3]}, 4));
        return new double[] {q[0] / n, q[1] / n, q[2] / n, q[3] / n};
    }
}
