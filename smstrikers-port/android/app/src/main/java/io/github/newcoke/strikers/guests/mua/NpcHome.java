package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * tools/outside/mua/npc_home.py: an NPC guest's home-corner figure (Home's way, no hero-select moves): every move named
 * in npcs.txt "home" (kind 0 idle, 1 greet, 2 big, 3 react, 4 goodbye, 5 cheer, 6 upset), looked up as NpcCels does;
 * the big / react / cheer / upset moves on the spot and facing as the idle does; tails never lift the figure.
 * Returns {home/&lt;pack&gt;.gpk, home/&lt;pack&gt;.gev}.
 */
final class NpcHome {
    private NpcHome() {}

    static final double FPS = Home.FPS, FADE = Home.FADE;

    static byte[][] build(Hero h, HeroPack.Costume c, byte[] matchPack, Igb shared) throws IOException {
        final IgbModel.Skel sk = c.sk;
        final String[] names = sk.names;
        final int n0 = names.length;
        final Npc.Bolt bolt = Npc.bolt(h, c);
        final double[][][] rest = Npc.rest(h, c);
        final HeroCels.PackRead P = new HeroCels.PackRead(matchPack);
        final int nb = bolt != null ? bolt.names.length : 0;
        final int nn = P.nn;
        if (nn != n0 + nb) throw new IOException(h.pack + ": " + nn + " pack nodes, " + n0 + " bones + " + nb + " bolt-on");
        final int[] parents = P.parents;
        final double[] zm = new double[n0], zs = new double[n0];
        for (int i = 0; i < n0; i++) {
            zm[i] = rest[i][3][2];
            zs[i] = P.binds[i][3][2];
        }
        int a = 0, b = 0;
        for (int i = 1; i < n0; i++) {
            if (zm[i] > zm[a]) a = i;
            if (zm[i] < zm[b]) b = i;
        }
        final double s = (zs[a] - zs[b]) / (zm[a] - zm[b]);
        final double zmin = zm[a] - zs[a] / s;
        final List<Igb> sets = Npc.sets(h, c, shared);
        final IgbAnim.Clip idle = Npc.find(sets, "idle");
        if (idle == null) throw new IOException(h.pack + ": no idle");
        final double idur = Math.max(idle.dur, 1e-3);
        final IgbAnim.Clip bIdle = bolt != null ? IgbAnim.clip(bolt.anims, "idle") : null;

        final List<double[][][]> fR = new ArrayList<>();
        final List<double[][]> fT = new ArrayList<>();
        final List<int[]> seg = new ArrayList<>();
        for (Object[] g : h.home) {
            final int kind = (Integer) g[0];
            @SuppressWarnings("unchecked") final List<Object[]> clips = (List<Object[]>) g[1];
            final int segStart = fR.size();
            boolean havePrev = false;
            double[][][] pR = null;
            double[][] pT = null;
            for (Object[] item : clips) {
                final String clip = (String) item[0];
                final double t0 = (Double) item[1];
                final Object t1 = item[2];
                final IgbAnim.Clip cl = Npc.move(sets, sk, clip);
                if (cl == null) continue;
                final double dur = Math.max(cl.dur, 1e-3);
                final double tEnd = t1 == null ? dur : Math.min(dur, (Double) t1);
                int n = Math.max(2, (int) Math.rint((tEnd - t0) * FPS));
                if (tEnd - t0 < 0.3) n = (int) (3.0 * FPS);
                final double fade = tEnd - t0 < 0.3 ? 0.6 : FADE;
                for (int j = 0; j < n; j++) {
                    final double t = t0 + (tEnd - t0) * j / n;
                    final double tf = fR.size() / FPS;
                    final double[][][] wp = Npc.poseFill(sk, cl.tracks, t, idle.tracks, Npc.pyMod(tf, idur));
                    double[][][] R = new double[nn][][];
                    double[][] T = new double[nn][3];
                    for (int i = 0; i < n0; i++) {
                        final int par = parents[i];
                        if (par < 0) {
                            R[i] = Np.eye3();
                            continue;
                        }
                        final double[][] L = Np.mm(wp[i], Np.inv(wp[par]));
                        if (parents[par] < 0) L[3][2] -= zmin;
                        R[i] = Np.rot(L);
                        T[i] = new double[] {L[3][0] * s, L[3][1] * s, L[3][2] * s};
                    }
                    if (bolt != null) Npc.boltLocals(bolt, bIdle, tf, s, R, T, n0);
                    if (havePrev && j < fade * FPS) {
                        final double f = (j + 1) / (fade * FPS + 1);
                        final double[][][] R2 = new double[nn][][];
                        final double[][] T2 = new double[nn][3];
                        for (int i = 0; i < nn; i++) {
                            R2[i] = Home.slerpRot(pR[i], R[i], f);
                            for (int x = 0; x < 3; x++) T2[i][x] = pT[i][x] * (1 - f) + T[i][x] * f;
                        }
                        R = R2;
                        T = T2;
                    }
                    fR.add(R);
                    fT.add(T);
                }
                pR = fR.get(fR.size() - 1);
                pT = fT.get(fT.size() - 1);
                havePrev = true;
            }
            if (fR.size() > segStart) seg.add(new int[] {segStart, fR.size() - segStart, kind});
        }
        // the moves on the spot, facing as its idle does (the body's own root child: Jean Grey's root has Motion first)
        final List<Integer> rootKids = new ArrayList<>();
        for (int i = 0; i < n0; i++) if (parents[i] >= 0 && parents[parents[i]] < 0) rootKids.add(i);
        final int[] desc = new int[nn];
        for (int i = 0; i < nn; i++) {
            int k = i;
            while (parents[k] >= 0) {
                k = parents[k];
                desc[k]++;
            }
        }
        int body = -1;
        for (int i : rootKids) if (body < 0 || desc[i] > desc[body]) body = i;
        for (int[] sg : seg) {
            final int st = sg[0], nf = sg[1], kind = sg[2];
            if (kind != 2 && kind != 3 && kind != 5 && kind != 6) continue;
            {
                final double[][] ref = fR.get(0)[body];
                final double[] sins = new double[nf], coss = new double[nf];
                for (int j = st; j < st + nf; j++) {
                    final double[][] rj = fR.get(j)[body];
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
                for (int p2 : rootKids) {
                    for (int j = st; j < st + nf; j++) {
                        fR.get(j)[p2] = Np.mm(fR.get(j)[p2], rz);
                        fT.get(j)[p2] = Np.vm(fT.get(j)[p2], rz);
                    }
                }
            }
            for (int p : rootKids) {
                final double[][] path = new double[nf][];
                for (int j = 0; j < nf; j++) path[j] = new double[] {fT.get(st + j)[p][0], fT.get(st + j)[p][1]};
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
                    final double[] t = fT.get(st + k)[p];
                    t[0] -= sm[k][0];
                    t[1] -= sm[k][1];
                }
            }
        }
        final List<double[][][]> wAll = new ArrayList<>();
        for (int j = 0; j < fR.size(); j++) {
            final double[][][] R = fR.get(j);
            final double[][] T = fT.get(j);
            final double[][][] W = new double[nn][][];
            for (int i = 0; i < nn; i++) {
                final double[][] L = Np.eye4();
                for (int x = 0; x < 3; x++) System.arraycopy(R[i][x], 0, L[x], 0, 3);
                System.arraycopy(T[i], 0, L[3], 0, 3);
                W[i] = parents[i] < 0 ? L : Np.mm(L, W[parents[i]]);
            }
            wAll.add(W);
        }
        // the body over the floor: fx / footstep helpers and tails don't lift it
        final boolean[] skip = new boolean[nn];
        for (int i = 0; i < nn; i++) {
            if (i >= n0) {
                skip[i] = true;
                continue;
            }
            final String nm = names[i].toLowerCase(Locale.ROOT);
            skip[i] = nm.startsWith("fx") || nm.equals("bip01 footsteps") || nm.contains("tail");
        }
        final double[] lift = new double[wAll.size()];
        for (int j = 0; j < wAll.size(); j++) {
            double lowest = Double.NaN;
            final double[][][] W = wAll.get(j);
            for (int i = 1; i < nn; i++) {
                if (skip[i]) continue;
                if (Double.isNaN(lowest) || W[i][3][2] < lowest) lowest = W[i][3][2];
            }
            final double l = 0.04 - lowest;
            lift[j] = 0.0 >= l ? 0.0 : l;
        }
        final Out pack = new Out();
        pack.bytes("GPK2".getBytes(StandardCharsets.ISO_8859_1)).le32(2).le32(nn).le32(P.npk).le32(P.nt).le32(0).lef32(P.height);
        pack.bytes(P.name32).le32(P.mode);
        pack.bytes(matchPack, P.nodesOff, P.texOff - P.nodesOff);
        pack.bytes(matchPack, P.texOff, P.pkEnd - P.texOff);
        final Out gev = new Out();
        gev.bytes("GEV1".getBytes(StandardCharsets.ISO_8859_1)).le32(nn).le32(wAll.size()).lef32((float) FPS);
        for (int j = 0; j < wAll.size(); j++) {
            final double[][][] W = wAll.get(j);
            for (int i = 0; i < nn; i++) {
                final double[][] m = W[i];
                for (int r = 0; r < 4; r++)
                    for (int x = 0; x < 3; x++) gev.lef32((float) (r == 3 && x == 2 ? m[3][2] + lift[j] : m[r][x]));
            }
        }
        gev.bytes("SEG1".getBytes(StandardCharsets.ISO_8859_1)).le32(seg.size()).le32(sk.index("Bip01 Head"));
        for (int[] sg : seg) gev.le32(sg[0]).le32(sg[1]).le32(sg[2]);
        return new byte[][] {pack.toBytes(), gev.toBytes()};
    }
}
