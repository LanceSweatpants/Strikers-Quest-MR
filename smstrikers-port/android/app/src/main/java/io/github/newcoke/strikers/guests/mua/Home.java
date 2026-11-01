package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * tools/outside/mua/build_home.py: a hero's home-corner figure. home/<pack>.gpk = its match pack (no clips) + bolt-on
 * parts on extra nodes (Wolverine's claws, Spider-Man's web strands, MUA's own flame on Ghost Rider / the Human Torch,
 * the Silver Surfer's board); home/<pack>.gev = a GEV1 loop (20 frames a second) of its own MUA moves in groups the
 * headset picks from (SEG1: idle, greeting, big move, goodbye, cheer, upset), each clip crossfaded into the next.
 */
final class Home {
    private Home() {}

    static final double FPS = 20.0, FADE = 0.2;

    static final class Result {
        byte[] gpk, gev;
    }

    private static final class Frame {
        double[][][] R;
        double[][] T;
        String clip;

        Frame(double[][][] r, double[][] t, String clip) {
            R = r;
            T = t;
            this.clip = clip;
        }
    }

    static Result build(Hero h, int costume, HeroPack.Costume c, byte[] matchPack, Igb shared, byte[] fire4) throws IOException {
        final String packName = h.pack(costume);
        final IgbModel.Skel sk = c.sk;
        final String[] names = sk.names;
        final int nb = names.length;
        final HeroCels.PackRead P = new HeroCels.PackRead(matchPack);
        if (P.nn != nb) throw new IOException(packName + ": " + P.nn + " pack nodes, " + nb + " bones");
        final double[] zm = new double[nb], zs = new double[nb];
        for (int i = 0; i < nb; i++) {
            zm[i] = sk.world[i][3][2];
            zs[i] = P.binds[i][3][2];
        }
        int a = 0, b = 0;
        for (int i = 1; i < nb; i++) {
            if (zm[i] > zm[a]) a = i;
            if (zm[i] < zm[b]) b = i;
        }
        final double s = (zs[a] - zs[b]) / (zm[a] - zm[b]);
        final double zmin = zm[a] - zs[a] / s;
        Igb db = null, combat = null;
        for (String n : c.fb.names()) {
            if (!"actoranimdb".equals(c.fb.type(n))) continue;
            if (HeroPack.MAIN_SET.matcher(n).matches()) db = new Igb(c.fb.data(n));
            if (n.endsWith("_4_combat.igb")) combat = new Igb(c.fb.data(n));
        }
        final String extra = h.extra;
        final List<Object[]> groups = h.home;

        // its moves' frames, one after another; seg table (first frame, frames, kind)
        final List<Frame> frames = new ArrayList<>();
        final List<int[]> seg = new ArrayList<>();
        for (int gi = 0; gi < groups.size(); gi++) {
            final int kind = (Integer) groups.get(gi)[0];
            @SuppressWarnings("unchecked") final List<Object[]> clips = (List<Object[]>) groups.get(gi)[1];
            final int segStart = frames.size();
            Frame prev = null;
            for (Object[] item : clips) {
                final String clip = (String) item[0];
                final double t0 = (Double) item[1];
                final Object t1 = item[2];
                IgbAnim.Clip cl = db != null ? IgbAnim.clip(db, clip) : null;
                if (cl == null && combat != null) cl = IgbAnim.clip(combat, clip);
                if (cl == null && shared != null) cl = IgbAnim.clip(shared, clip);
                if (cl == null) continue;
                final double dur = Math.max(cl.dur, 1e-3);
                double hold = 0.0;
                double tEnd;
                double hangAt = Double.NaN;
                if ("hang".equals(t1)) {
                    final int head = sk.index("Bip01 Head"), foot = sk.index("Bip01 L Foot");
                    double bestV = Double.NEGATIVE_INFINITY, bestT = 0;
                    for (int k = 0; k <= 40; k++) {
                        final double t = dur * k / 40;
                        final double[][][] W = IgbAnim.poseWorld(sk, cl.tracks, t);
                        final double v = W[foot][3][2] - W[head][3][2];
                        if (v > bestV || (v == bestV && t > bestT)) {
                            bestV = v;
                            bestT = t;
                        }
                    }
                    tEnd = dur;
                    hold = 3.0;
                    hangAt = bestT;
                } else {
                    tEnd = t1 == null ? dur : Math.min(dur, (Double) t1);
                }
                int n = Math.max(2, (int) Math.rint((tEnd - t0) * FPS));
                if (tEnd - t0 < 0.3) n = (int) (3.0 * FPS);
                int kHold = (int) Math.rint(hold * FPS);
                final List<Double> seq = new ArrayList<>();
                for (int k = 0; k < n; k++) {
                    final double t = t0 + (tEnd - t0) * k / n;
                    seq.add(t);
                    if (!Double.isNaN(hangAt) && kHold != 0 && t >= hangAt) {
                        for (int x = 0; x < kHold; x++) seq.add(hangAt);
                        kHold = 0;
                    }
                }
                final double fade = tEnd - t0 < 0.3 ? 0.6 : FADE;
                for (int j = 0; j < seq.size(); j++) {
                    final Object[] rt = localsAt(sk, cl, seq.get(j), s, zmin);
                    double[][][] R = (double[][][]) rt[0];
                    double[][] T = (double[][]) rt[1];
                    if (prev != null && j < fade * FPS) {
                        final double f = (j + 1) / (fade * FPS + 1);
                        final double[][][] R2 = new double[nb][][];
                        final double[][] T2 = new double[nb][3];
                        for (int i = 0; i < nb; i++) {
                            R2[i] = slerpRot(prev.R[i], R[i], f);
                            for (int x = 0; x < 3; x++) T2[i][x] = prev.T[i][x] * (1 - f) + T[i][x] * f;
                        }
                        R = R2;
                        T = T2;
                    }
                    frames.add(new Frame(R, T, clip));
                }
                prev = frames.get(frames.size() - 1);
            }
            if (frames.size() > segStart) seg.add(new int[] {segStart, frames.size() - segStart, kind});
        }
        // the fights on the spot (turned to face as the idle does; the hips' slow drift taken out)
        final List<Integer> pelvis = new ArrayList<>();
        for (int i = 0; i < nb; i++) if (sk.parents[i] >= 0 && sk.parents[sk.parents[i]] < 0) pelvis.add(i);
        final int[] frameKind = new int[frames.size()];
        for (int[] sg : seg) {
            final int st = sg[0], nf = sg[1], kind = sg[2];
            for (int j = st; j < st + nf; j++) frameKind[j] = kind;
            if (kind != 2 && kind != 5 && kind != 6) continue;
            if (!pelvis.isEmpty()) {
                final int p0 = pelvis.get(0);
                final double[][] ref = frames.get(0).R[p0];
                final double[] sins = new double[nf], coss = new double[nf];
                for (int j = st; j < st + nf; j++) {
                    final double[][] rj = frames.get(j).R[p0];
                    int best = 0;
                    double bn = -1;
                    for (int q = 0; q < 3; q++) {
                        final double nq = Math.sqrt(Np.dot(ref[q][0], ref[q][1], ref[q][0], ref[q][1]));
                        if (nq > bn) {
                            bn = nq;
                            best = q;
                        }
                    }
                    final double d00 = ref[best][0], d01 = ref[best][1], d10 = rj[best][0], d11 = rj[best][1];
                    final double y = Libm.atan2(d00 * d11 - d01 * d10, Np.dot(d00, d01, d10, d11));
                    sins[j - st] = Libm.sin(y);
                    coss[j - st] = Libm.cos(y);
                }
                final double ym = Libm.atan2(Np.pySum(sins, nf), Np.pySum(coss, nf));
                final double cc = Libm.cos(-ym), ss = Libm.sin(-ym);
                final double[][] rz = {{cc, ss, 0.0}, {-ss, cc, 0.0}, {0.0, 0.0, 1.0}};
                for (int p2 : pelvis) {
                    for (int j = st; j < st + nf; j++) {
                        final Frame fr = frames.get(j);
                        fr.R[p2] = Np.mm(fr.R[p2], rz);
                        fr.T[p2] = Np.vm(fr.T[p2], rz);
                    }
                }
            }
            for (int p : pelvis) {
                final double[][] path = new double[nf][];
                for (int j = 0; j < nf; j++) path[j] = new double[] {frames.get(st + j).T[p][0], frames.get(st + j).T[p][1]};
                final int half = (int) (FPS / 2);
                final double[][] sm = new double[nf][2];
                for (int k = 0; k < nf; k++) {
                    final int lo = Math.max(0, k - half), hi = Math.min(nf, k + half + 1);
                    for (int x = 0; x < 2; x++) {
                        double acc = 0.0;
                        for (int r = lo; r < hi; r++) acc = r == lo ? path[r][x] : acc + path[r][x];
                        sm[k][x] = acc / (hi - lo) - path[0][x];
                    }
                }
                for (int k = 0; k < nf; k++) {
                    final double[] t = frames.get(st + k).T[p];
                    t[0] -= sm[k][0];
                    t[1] -= sm[k][1];
                }
            }
        }
        final List<double[][][]> wAll = new ArrayList<>();
        for (Frame fr : frames) wAll.add(worlds(sk, fr.R, fr.T));
        // the body never under the floor
        final boolean[] skip = new boolean[nb];
        if (!h.liftAll && !"claws".equals(extra))
            for (int i = 0; i < nb; i++) skip[i] = names[i].toLowerCase(java.util.Locale.ROOT).startsWith("fx");
        final double[] lift = new double[wAll.size()];
        for (int j = 0; j < wAll.size(); j++) {
            double lowest = Double.NaN;
            final double[][][] W = wAll.get(j);
            for (int i = 1; i < nb; i++) {
                if (skip[i]) continue;
                if (Double.isNaN(lowest) || W[i][3][2] < lowest) lowest = W[i][3][2];
            }
            final double l = 0.04 - lowest;
            lift[j] = 0.0 >= l ? 0.0 : l;
        }

        // extras
        final List<byte[]> nodes = new ArrayList<>();
        int p = 64;
        for (int i = 0; i < nb; i++) {
            nodes.add(java.util.Arrays.copyOfRange(matchPack, p, p + 156));
            p += 156;
        }
        final List<double[][]> binds = new ArrayList<>();
        for (double[][] bm : P.binds) binds.add(bm);
        final Out pks = new Out();
        pks.bytes(matchPack, P.texEnd, P.pkEnd - P.texEnd);
        int npk = P.npk;
        final Out texs = new Out();
        texs.bytes(matchPack, P.texOff, P.texEnd - P.texOff);
        int nt = P.nt;
        final List<List<double[][]>> extraFrames = new ArrayList<>();
        if ("claws".equals(extra)) {
            final int th = Tex.lowerHash("mua/" + packName + "/home/claw");
            final byte[] rec = texture(205, 212, 222);
            texs.le32(th).le32(rec.length).bytes(rec).align(4);
            nt++;
            final TreeMap<String, byte[]> claws = new TreeMap<>();
            for (String n : c.fb.names()) {
                if (n.contains("bolton/claw_left")) claws.put("Bip01 L Hand", c.fb.data(n));
                if (n.contains("bolton/claw_right")) claws.put("Bip01 R Hand", c.fb.data(n));
            }
            for (java.util.Map.Entry<String, byte[]> e : claws.entrySet()) {
                final int fi = sk.index(e.getKey());
                final List<IgbModel.Part> parts = IgbModel.sceneMeshes(new Igb(e.getValue()));
                final List<double[]> pos = new ArrayList<>();
                final List<int[]> tris = new ArrayList<>();
                for (IgbModel.Part pp : parts) {
                    final int base = pos.size();
                    for (double[] q : pp.pos) pos.add(q.clone());
                    for (int[] t : pp.tris) tris.add(new int[] {base + t[0], base + t[1], base + t[2]});
                }
                final double[][] local = new double[pos.size()][];
                double baseX = Double.NaN;
                for (int i = 0; i < local.length; i++) {
                    final double[] q = pos.get(i);
                    local[i] = new double[] {q[0] * s, q[1] * s, q[2] * s};
                    if (Double.isNaN(baseX) || local[i][0] < baseX) baseX = local[i][0];
                }
                final double[][] B = binds.get(fi);
                final double[][] model = place(local, B);
                final int node = nodes.size();
                nodes.add(nodeRec(Tex.lowerHash("mua/" + packName + "/home/claw_" + e.getKey()), fi, B));
                binds.add(B);
                final Object[] ts = twoSided(model, tris);
                pks.bytes(packet(th, (double[][]) ts[0], (double[][]) ts[1], castTris(ts[2]), node));
                npk++;
                final List<double[][]> seqM = new ArrayList<>();
                for (int j = 0; j < wAll.size(); j++) {
                    final double t = j / FPS;
                    final double ph = pyMod(t, 2.4);
                    double k = 0.12 <= ph && ph < 1.42 ? 1.0 : (ph < 0.12 ? ph / 0.12 : pyMax(0.0, 1.0 - (ph - 1.42) / 0.12));
                    k = pyMax(0.03, pyMin(1.0, k));
                    if (frameKind[j] == 2) k = 1.0;
                    final double[][] M = Np.eye4();
                    M[0][0] = k;
                    M[3][0] = baseX * (1 - k);
                    seqM.add(Np.mm(M, wAll.get(j)[fi]));
                }
                extraFrames.add(seqM);
            }
        }
        if ("webs".equals(extra)) {
            final int th = Tex.lowerHash("mua/" + packName + "/home/web");
            final byte[] rec = texture(245, 245, 250);
            texs.le32(th).le32(rec.length).bytes(rec).align(4);
            nt++;
            final Object[] tube = tube(0.007);
            final int[] hands = {sk.index("Bip01 R Hand"), sk.index("Bip01 L Hand")};
            final int[] feet = {sk.index("Bip01 L Foot"), sk.index("Bip01 R Foot")};
            final int head = sk.index("Bip01 Head");
            int firstLoop = -1;
            for (int i = 0; i < frames.size(); i++) if (frames.get(i).clip.equals("power_1_loop")) {
                firstLoop = i;
                break;
            }
            for (int w = 0; w < 3; w++) {
                final int node = nodes.size();
                nodes.add(nodeRec(Tex.lowerHash("mua/" + packName + "/home/web" + w), 0, Np.eye4()));
                binds.add(Np.eye4());
                pks.bytes(packet(th, (double[][]) tube[0], (double[][]) tube[1], castTris(tube[2]), node));
                npk++;
                final List<double[][]> seqM = new ArrayList<>();
                for (int j = 0; j < wAll.size(); j++) {
                    final double[][][] W = wAll.get(j);
                    final double[][] M = Np.eye4();
                    for (double[] r : M) for (int x = 0; x < 4; x++) r[x] = r[x] * 1.0;
                    double length = 1e-4;
                    double[] at = {W[hands[0]][3][0], W[hands[0]][3][1], W[hands[0]][3][2]};
                    double[] d = {1.0, 0.0, 0.0};
                    if (w < 2 && frames.get(j).clip.equals("power_1_loop")) {
                        final double tl = (j - firstLoop) / FPS;
                        final int cyc = (int) (tl / 0.35);
                        final double ph = tl - cyc * 0.35;
                        if (cyc % 2 == w) length = ph < 0.28 ? 2.6 * pyMin(1.0, ph / 0.1) : 1e-4;
                        at = new double[] {W[hands[w]][3][0], W[hands[w]][3][1], W[hands[w]][3][2]};
                        d = new double[] {1.0, 0.0, 0.18};
                    }
                    if (w == 2) {
                        final double f0 = W[feet[0]][3][2], f1 = W[feet[1]][3][2];
                        final double top = f1 > f0 ? f1 : f0;
                        if (top - W[head][3][2] > 0.4) {
                            at = new double[] {0.5 * (W[feet[0]][3][0] + W[feet[1]][3][0]), 0.5 * (W[feet[0]][3][1] + W[feet[1]][3][1]),
                                0.5 * (W[feet[0]][3][2] + W[feet[1]][3][2])};
                            d = new double[] {0.0, 0.0, 1.0};
                            length = 1.6;
                        }
                    }
                    final double dn = Np.norm(d);
                    d = new double[] {d[0] / dn, d[1] / dn, d[2] / dn};
                    double[] u = Np.cross(d, Math.abs(d[1]) < 0.9 ? new double[] {0.0, 1.0, 0.0} : new double[] {1.0, 0.0, 0.0});
                    final double un = Np.norm(u);
                    u = new double[] {u[0] / un, u[1] / un, u[2] / un};
                    final double[] v = Np.cross(d, u);
                    M[0][0] = d[0] * length;
                    M[0][1] = d[1] * length;
                    M[0][2] = d[2] * length;
                    System.arraycopy(v, 0, M[1], 0, 3);
                    System.arraycopy(u, 0, M[2], 0, 3);
                    System.arraycopy(at, 0, M[3], 0, 3);
                    seqM.add(M);
                }
                extraFrames.add(seqM);
            }
        }
        if ("fire".equals(extra) || "torch".equals(extra)) {
            final byte[][] fr = fireFrames(fire4);
            final int[] hashes = new int[3];
            final int[] wh = fireSize(fire4);
            for (int f = 0; f < 3; f++) {
                hashes[f] = Tex.lowerHash("mua/" + packName + "/home/fire" + f);
                final byte[] rec = Tex.record(fr[f], wh[0], wh[1]);
                texs.le32(hashes[f]).le32(rec.length).bytes(rec).align(4);
                nt++;
            }
            if ("fire".equals(extra)) {
                final int head = sk.index("Bip01 Head");
                final double[][] bh = binds.get(head);
                final double[] centre = {bh[3][0] + 0.0, bh[3][1] + 0.0, bh[3][2] + 0.1};
                pks.bytes(flamePackets(hashes, centre, 0.5, head));
                npk += 9;
            } else {
                final Object[][] spots = {{"Bip01 Head", 0.1, 0.55}, {"Bip01 Spine1", 0.0, 0.7}, {"Bip01 L Hand", 0.0, 0.32},
                    {"Bip01 R Hand", 0.0, 0.32}, {"Bip01 L Calf", 0.0, 0.4}, {"Bip01 R Calf", 0.0, 0.4}};
                for (Object[] sp : spots) {
                    final int node = sk.index((String) sp[0]);
                    if (node < 0) continue;
                    final double[][] bm = binds.get(node);
                    final double[] centre = {bm[3][0] + 0.0, bm[3][1] + 0.0, bm[3][2] + (Double) sp[1]};
                    pks.bytes(flamePackets(hashes, centre, (Double) sp[2], node));
                    npk += 9;
                }
            }
        }
        if ("board".equals(extra)) {
            byte[] data = null;
            for (String n : c.fb.names()) if (n.endsWith("silver_surfer_surfboard.igb")) {
                data = c.fb.data(n);
                break;
            }
            if (data != null) {
                final Igb bg = new Igb(data);
                final List<IgbModel.Part> parts = IgbModel.sceneMeshes(bg);
                final int fi = sk.index("Bip01 R Toe0");
                final double[][] B = binds.get(fi);
                final double[] on = new double[wAll.size()];
                for (int j = 0; j < on.length; j++) on[j] = frameKind[j] == 0 || frameKind[j] == 1 || frameKind[j] == 4 ? 1.0 : 0.0;
                for (int pi = 0; pi < parts.size(); pi++) {
                    final IgbModel.Part pp = parts.get(pi);
                    final Igb.Obj ta = pp.texattr != null ? pp.texattr : pp.envTexattr;
                    if (ta == null) continue;
                    final IgbModel.Pic pic = IgbModel.image(bg, ta);
                    final int th = Tex.lowerHash("mua/" + packName + "/home/board" + pi);
                    final byte[] rec = Tex.record(pic.rgba, pic.w, pic.h);
                    texs.le32(th).le32(rec.length).bytes(rec).align(4);
                    nt++;
                    final double[][] local = new double[pp.pos.length][];
                    for (int i = 0; i < local.length; i++) local[i] = new double[] {pp.pos[i][0] * s, pp.pos[i][1] * s, pp.pos[i][2] * s};
                    final double[][] model = place(local, B);
                    final List<int[]> tris = new ArrayList<>();
                    for (int[] t : pp.tris) tris.add(t.clone());
                    final int node = nodes.size();
                    nodes.add(nodeRec(Tex.lowerHash("mua/" + packName + "/home/board" + pi), fi, B));
                    binds.add(B);
                    final Object[] ts = twoSided(model, tris);
                    final double[][] p2 = (double[][]) ts[0];
                    final byte[] body = packet(th, p2, (double[][]) ts[1], castTris(ts[2]), node);
                    for (int vi = 0; vi < p2.length; vi++) {
                        final double u = pp.uv != null ? pp.uv[vi % model.length][0] : 0.5;
                        final double v = pp.uv != null ? pp.uv[vi % model.length][1] : 0.5;
                        putF(body, 12 + vi * 52 + 24, u);
                        putF(body, 12 + vi * 52 + 28, v);
                    }
                    pks.bytes(body);
                    npk++;
                    final int ramp = (int) (0.25 * FPS);
                    final List<double[][]> seqM = new ArrayList<>();
                    for (int j = 0; j < wAll.size(); j++) {
                        final int lo = Math.max(0, j - ramp), hi = Math.min(on.length, j + ramp + 1);
                        double sum = 0.0;
                        final double[] part = java.util.Arrays.copyOfRange(on, lo, hi);
                        sum = Np.pySum(part, part.length);
                        final double k = pyMax(0.001, sum / (double) (hi - lo));
                        final double[][] M = Np.eye4();
                        M[0][0] = k;
                        M[1][1] = k;
                        M[2][2] = k;
                        seqM.add(Np.mm(M, wAll.get(j)[fi]));
                    }
                    extraFrames.add(seqM);
                }
            }
        }

        // the home pack (no clips: its loop moves it)
        final int nn = nodes.size();
        final Out pack = new Out();
        pack.bytes("GPK2".getBytes(StandardCharsets.ISO_8859_1)).le32(2).le32(nn).le32(npk).le32(nt).le32(0).lef32(P.height);
        pack.bytes(P.name32).le32(P.mode);
        for (byte[] nr : nodes) pack.bytes(nr);
        pack.bytes(texs.toBytes()).bytes(pks.toBytes());
        // its loop
        final Out gev = new Out();
        gev.bytes("GEV1".getBytes(StandardCharsets.ISO_8859_1)).le32(nn).le32(wAll.size()).lef32((float) FPS);
        for (int j = 0; j < wAll.size(); j++) {
            final double[][][] W = wAll.get(j);
            for (int i = 0; i < nb + extraFrames.size(); i++) {
                final double[][] m = i < nb ? W[i] : extraFrames.get(i - nb).get(j);
                for (int r = 0; r < 4; r++)
                    for (int x = 0; x < 3; x++) gev.lef32((float) (r == 3 && x == 2 ? m[3][2] + lift[j] : m[r][x]));
            }
        }
        gev.bytes("SEG1".getBytes(StandardCharsets.ISO_8859_1)).le32(seg.size()).le32(sk.index("Bip01 Head"));
        for (int[] sg : seg) gev.le32(sg[0]).le32(sg[1]).le32(sg[2]);
        final Result res = new Result();
        res.gpk = pack.toBytes();
        res.gev = gev.toBytes();
        return res;
    }

    // ------------------------------------------------------------------------------------------------ posing
    /** locals_at: each bone local to its parent, the root's children in the root frame with the feet on 0. */
    static Object[] localsAt(IgbModel.Skel sk, IgbAnim.Clip cl, double t, double s, double zmin) {
        final double[][][] wp = IgbAnim.poseWorld(sk, cl.tracks, t);
        final int n = sk.names.length;
        final double[][][] R = new double[n][][];
        final double[][] T = new double[n][3];
        for (int i = 0; i < n; i++) {
            final int par = sk.parents[i];
            if (par < 0) {
                R[i] = Np.eye3();
                continue;
            }
            final double[][] L = Np.mm(wp[i], Np.inv(wp[par]));
            if (sk.parents[par] < 0) L[3][2] -= zmin;
            R[i] = Np.rot(L);
            T[i] = new double[] {L[3][0] * s, L[3][1] * s, L[3][2] * s};
        }
        return new Object[] {R, T};
    }

    /** slerp_rot: a -> b by f through quaternions (q_of unnormalised, numpy's dot / norm). */
    static double[][] slerpRot(double[][] a, double[][] b, double f) {
        final double[] qa = qOf(a);
        double[] qb = qOf(b);
        if (Np.dot(qa, qb) < 0) qb = new double[] {-qb[0], -qb[1], -qb[2], -qb[3]};
        final double[] q = new double[4];
        for (int i = 0; i < 4; i++) q[i] = qa[i] * (1 - f) + qb[i] * f;
        final double n = Np.norm(q);
        final double x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
        return new double[][] {
            {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)},
            {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)},
            {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)}};
    }

    static double[] qOf(double[][] m) {
        final double tr = m[0][0] + m[1][1] + m[2][2];
        if (tr > 0) {
            final double S = Math.sqrt(tr + 1.0) * 2;
            return new double[] {(m[1][2] - m[2][1]) / S, (m[2][0] - m[0][2]) / S, (m[0][1] - m[1][0]) / S, 0.25 * S};
        }
        if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            final double S = Math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
            return new double[] {0.25 * S, (m[0][1] + m[1][0]) / S, (m[2][0] + m[0][2]) / S, (m[1][2] - m[2][1]) / S};
        }
        if (m[1][1] > m[2][2]) {
            final double S = Math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
            return new double[] {(m[0][1] + m[1][0]) / S, 0.25 * S, (m[1][2] + m[2][1]) / S, (m[2][0] - m[0][2]) / S};
        }
        final double S = Math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
        return new double[] {(m[2][0] + m[0][2]) / S, (m[1][2] + m[2][1]) / S, 0.25 * S, (m[0][1] - m[1][0]) / S};
    }

    static double[][][] worlds(IgbModel.Skel sk, double[][][] R, double[][] T) {
        final int n = sk.names.length;
        final double[][][] W = new double[n][][];
        for (int i = 0; i < n; i++) {
            final int par = sk.parents[i];
            final double[][] L = Np.eye4();
            for (int a = 0; a < 3; a++) System.arraycopy(R[i][a], 0, L[a], 0, 3);
            System.arraycopy(T[i], 0, L[3], 0, 3);
            W[i] = par < 0 ? L : Np.mm(L, W[par]);
        }
        return W;
    }

    // ------------------------------------------------------------------------------------------------ geometry
    /** local @ B[:3, :3] + B[3, :3] */
    static double[][] place(double[][] local, double[][] B) {
        final double[][] m = Np.mm(local, Np.rot(B));
        for (double[] r : m) {
            r[0] = r[0] + B[3][0];
            r[1] = r[1] + B[3][1];
            r[2] = r[2] + B[3][2];
        }
        return m;
    }

    /** node_rec: a node with no Strikers bone; its bind (Strikers space) as given. */
    static byte[] nodeRec(int hash, int parent, double[][] bind) {
        final double[][] ib = Np.inv(bind);
        final Out o = new Out();
        o.le32(hash).le32(parent);
        for (int x = 0; x < 3; x++) o.lef32((float) bind[3][x]);
        o.le32(1);
        for (double[] r : ib) for (double v : r) o.lef32((float) v);
        o.le32(0);
        for (double[] r : bind) for (double v : r) o.lef32((float) v);
        return o.toBytes();
    }

    /** packet(): every vertex on one node, normals stored pointing in, triangles reversed for the strip. */
    static byte[] packet(int texHash, double[][] pos, double[][] nrm, int[][] tris, int node) throws IOException {
        final int[] rev = new int[tris.length * 3];
        for (int t = 0; t < tris.length; t++) {
            rev[t * 3] = tris[t][0];
            rev[t * 3 + 1] = tris[t][2];
            rev[t * 3 + 2] = tris[t][1];
        }
        final int[] strip = MGeo.stripify(rev);
        if (!MGeo.checkStrip(strip, rev)) throw new IOException("strip check failed");
        final Out o = new Out();
        o.le32(texHash).le32(pos.length).le32(strip.length);
        for (int i = 0; i < pos.length; i++) {
            o.lef32((float) pos[i][0]).lef32((float) pos[i][1]).lef32((float) pos[i][2]);
            o.lef32((float) -nrm[i][0]).lef32((float) -nrm[i][1]).lef32((float) -nrm[i][2]);
            o.lef32(0.5f).lef32(0.5f);
            o.u8(node).u8(0xFF).u8(0xFF).u8(0xFF);
            o.lef32(1.0f).lef32(0f).lef32(0f).lef32(0f);
        }
        for (int x : strip) o.le16(x);
        o.align(4);
        return o.toBytes();
    }

    /** normals(): area-weighted vertex normals (np.cross, numpy's row norms), unit. */
    static double[][] normals(double[][] pos, List<int[]> tris) {
        final double[][] n = new double[pos.length][3];
        for (int[] t : tris) {
            final double[] pa = pos[t[0]], pb = pos[t[1]], pc = pos[t[2]];
            final double[] fn = Np.cross(new double[] {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]},
                    new double[] {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]});
            for (int q : t) for (int x = 0; x < 3; x++) n[q][x] += fn[x];
        }
        for (double[] v : n) {
            double ln = Math.sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
            if (ln == 0) ln = 1;
            v[0] /= ln;
            v[1] /= ln;
            v[2] /= ln;
        }
        return n;
    }

    /** two_sided(): its triangles and a copy turned round with its own normals. */
    static Object[] twoSided(double[][] pos, List<int[]> tris) {
        final double[][] n1 = normals(pos, tris);
        final int k = pos.length;
        final double[][] pos2 = new double[k * 2][];
        final double[][] nrm = new double[k * 2][];
        for (int i = 0; i < k; i++) {
            pos2[i] = pos[i];
            pos2[k + i] = pos[i];
            nrm[i] = n1[i];
            nrm[k + i] = new double[] {-n1[i][0], -n1[i][1], -n1[i][2]};
        }
        final List<int[]> tris2 = new ArrayList<>(tris);
        for (int[] t : tris) tris2.add(new int[] {t[0] + k, t[2] + k, t[1] + k});
        return new Object[] {pos2, nrm, tris2};
    }

    @SuppressWarnings("unchecked")
    static int[][] castTris(Object o) {
        return ((List<int[]>) o).toArray(new int[0][]);
    }

    /** tube(): a square tube along +X from 0 to 1 (a web strand), both sides. */
    static Object[] tube(double r) {
        final List<double[]> pos = new ArrayList<>();
        for (double x : new double[] {0.0, 1.0})
            for (double[] yz : new double[][] {{-r, -r}, {r, -r}, {r, r}, {-r, r}}) pos.add(new double[] {x, yz[0], yz[1]});
        final List<int[]> tris = new ArrayList<>();
        for (int k = 0; k < 4; k++) {
            final int a = k, b = (k + 1) % 4;
            tris.add(new int[] {a, b, b + 4});
            tris.add(new int[] {a, b + 4, a + 4});
        }
        tris.add(new int[] {0, 2, 1});
        tris.add(new int[] {0, 3, 2});
        tris.add(new int[] {4, 5, 6});
        tris.add(new int[] {4, 6, 7});
        return twoSided(pos.toArray(new double[0][]), tris);
    }

    /** texture(rgb): an 8 x 8 picture a touch darker at the bottom. */
    static byte[] texture(int r, int g, int b) {
        final int w = 8, h = 8;
        final byte[] px = new byte[w * h * 4];
        int o = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                final double f = 0.85 + 0.15 * (1 - y / (double) (h - 1));
                px[o++] = (byte) Math.min(255, (int) (r * f));
                px[o++] = (byte) Math.min(255, (int) (g * f));
                px[o++] = (byte) Math.min(255, (int) (b * f));
                px[o++] = (byte) 255;
            }
        }
        return Tex.record(px, w, h);
    }

    // ------------------------------------------------------------------------------------------------ fire
    static int[] fireSize(byte[] fire4) throws IOException {
        final IgbModel.Pic im = IgbModel.images(new Igb(fire4)).get(0);
        return new int[] {im.w, im.h};
    }

    /** fire_frames: MUA's fire4 flame as 3 flipbook frames (as is, mirrored, drawn up); brightness = see-through. */
    static byte[][] fireFrames(byte[] fire4) throws IOException {
        final IgbModel.Pic im = IgbModel.images(new Igb(fire4)).get(0);
        final int w = im.w, h = im.h;
        final byte[] src = im.rgba;
        final byte[][] out = new byte[3][w * h * 4];
        for (int f = 0; f < 3; f++) {
            int o = 0;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int sx = x, sy = y;
                    if (f == 1) sx = w - 1 - x;
                    if (f == 2) sy = Math.min(h - 1, y + 5);
                    final int so = (sy * w + sx) * 4;
                    final int r = src[so] & 0xFF, g = src[so + 1] & 0xFF, bb = src[so + 2] & 0xFF;
                    final int a = Math.max(r, Math.max(g, bb));
                    final double k = a > 0 ? 255.0 / a : 0.0;
                    out[f][o++] = (byte) Math.min(255, (int) (r * k));
                    out[f][o++] = (byte) Math.min(255, (int) (g * k));
                    out[f][o++] = (byte) Math.min(255, (int) (bb * k));
                    out[f][o++] = (byte) a;
                }
            }
        }
        return out;
    }

    /** flame_packets: 3 frames x 3 puffs, each 3 upright squares 60 degrees apart, on `node` (soft flame packets). */
    static byte[] flamePackets(int[] hashes, double[] centre, double H, int node) {
        final Out out = new Out();
        final double W = 0.6 * H;
        final double[][] layers = {{0.0, 1.0}, {0.38, 0.72}, {0.68, 0.46}};
        for (int f = 0; f < 3; f++) {
            for (int k = 0; k < 3; k++) {
                final double rise = layers[k][0], sz = layers[k][1];
                final Out verts = new Out();
                final List<Integer> strip = new ArrayList<>();
                final double side = W * sz;
                for (int q = 0; q < 3; q++) {
                    final double a = Libm.radians(60 * q + 20 * f + 35 * k);
                    final double dx = Libm.cos(a), dy = Libm.sin(a);
                    final int base = verts.size() / 52;
                    final int[][] st = {{-1, 0}, {1, 0}, {-1, 1}, {1, 1}};
                    for (int[] p : st) {
                        final int s = p[0], t = p[1];
                        verts.lef32((float) (centre[0] + s * 0.5 * side * dx)).lef32((float) (centre[1] + s * 0.5 * side * dy))
                                .lef32((float) (centre[2] - 0.25 * H + rise * H + t * side));
                        verts.lef32((float) -dy).lef32((float) dx).lef32(0f).lef32((float) ((s + 1) * 0.5)).lef32((float) (1.0 - t));
                        verts.u8(node).u8(0xFF).u8(0xFF).u8(0xFF).lef32(1f).lef32(0f).lef32(0f).lef32(0f);
                    }
                    if (!strip.isEmpty()) {
                        strip.add(strip.get(strip.size() - 1));
                        strip.add(base);
                    }
                    strip.add(base);
                    strip.add(base + 1);
                    strip.add(base + 2);
                    strip.add(base + 3);
                }
                final int flags = 0x40000000 | (f << 28);
                out.le32(hashes[(f + k) % 3]).le32((verts.size() / 52) | flags).le32(strip.size());
                out.bytes(verts.toBytes());
                for (int x : strip) out.le16(x);
                out.align(4);
            }
        }
        return out.toBytes();
    }

    // ------------------------------------------------------------------------------------------------ Python
    static double pyMod(double x, double m) {
        double r = x % m;
        if (r != 0.0) {
            if ((m < 0) != (r < 0)) r += m;
        } else {
            r = Math.copySign(0.0, m);
        }
        return r;
    }

    /** Python's max(a, b): a unless b > a. */
    static double pyMax(double a, double b) {
        return b > a ? b : a;
    }

    static double pyMin(double a, double b) {
        return b < a ? b : a;
    }

    static void putF(byte[] b, int o, double v) {
        HeroPack.putF(b, o, v);
    }
}
