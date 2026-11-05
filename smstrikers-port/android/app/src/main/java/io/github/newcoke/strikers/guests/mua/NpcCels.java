package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * tools/outside/mua/npc_cels.py: an NPC guest's 4 goal celebrations (HeroCels' way): its moves looked up in its own set,
 * then the .fb's shared sets (the villain fight style, MUA's common combat moves), then MUA's shared set; bones a move
 * has no track for take its own idle's; a bolt-on (Nightcrawler's tail) plays its idle; the pack's own parents
 * (Lizard's tail on his pelvis) and its rest binds (Professor X, mua2gpk --rest-bind) are used.
 */
final class NpcCels {
    private NpcCels() {}

    static final double FPS = HeroCels.FPS;

    static byte[] add(Hero h, HeroPack.Costume c, byte[] pack, Igb shared) throws IOException {
        final IgbModel.Skel sk = c.sk;
        final int n0 = sk.names.length;
        final Npc.Bolt bolt = Npc.bolt(h, c);
        final double[][][] rest = Npc.rest(h, c);
        final HeroCels.PackRead pr = new HeroCels.PackRead(pack);
        final int nb = bolt != null ? bolt.names.length : 0;
        final int nn = pr.nn;
        if (nn != n0 + nb) throw new IOException(h.pack + ": " + nn + " pack nodes, " + n0 + " bones + " + nb + " bolt-on");
        final int[] parents = pr.parents;
        final double[] zm = new double[n0], zs = new double[n0];
        for (int i = 0; i < n0; i++) {
            zm[i] = rest[i][3][2];
            zs[i] = pr.binds[i][3][2];
        }
        int a = 0, b = 0;
        for (int i = 1; i < n0; i++) {
            if (zm[i] > zm[a]) a = i;
            if (zm[i] < zm[b]) b = i;
        }
        final double s = (zs[a] - zs[b]) / (zm[a] - zm[b]);
        final double zmin = zm[a] - zs[a] / s;
        final List<Igb> sets = Npc.sets(h, c, shared);
        int root = -1;
        for (int i = 0; i < n0; i++) if (parents[i] < 0) {
            root = i;
            break;
        }
        final int[] desc = new int[n0];
        for (int i = 0; i < n0; i++) {
            int k = i;
            while (parents[k] >= 0) {
                k = parents[k];
                desc[k]++;
            }
        }
        int pelvis = -1;
        for (int i = 0; i < n0; i++) if (parents[i] == root && (pelvis < 0 || desc[i] > desc[pelvis])) pelvis = i;
        final IgbAnim.Clip idle = Npc.find(sets, "idle");
        if (idle == null) throw new IOException(h.pack + ": no idle");
        final double idur = Math.max(idle.dur, 1e-3);
        final double[][][] w0 = IgbAnim.poseWorld(sk, idle.tracks, 0.0);
        final double[][] ref = Np.rot(Np.mm(w0[pelvis], Np.inv(w0[root])));
        int hip = -1;
        for (int i = 0; i < n0; i++) if (parents[i] == pelvis && (hip < 0 || desc[i] > desc[hip])) hip = i;
        if (hip < 0) hip = pelvis;
        final double[][] l0 = Np.mm(w0[hip], Np.inv(w0[root]));
        final double[] idleHips = {l0[3][0] * s, l0[3][1] * s, (l0[3][2] - zmin) * s};
        final IgbAnim.Clip bIdle = bolt != null ? IgbAnim.clip(bolt.anims, "idle") : null;

        final List<byte[]> recs = new ArrayList<>();
        for (int ci = 0; ci < h.cels.size(); ci++) {
            final List<double[][][]> kR = new ArrayList<>();
            final List<double[][]> kT = new ArrayList<>();
            final List<Integer> starts = new ArrayList<>();
            for (Object[] piece : h.cels.get(ci)) {
                final String cname = (String) piece[0];
                final int rep = (Integer) piece[1];
                final IgbAnim.Clip cl = Npc.move(sets, sk, cname);
                if (cl == null) continue;
                final double dur = Math.max(cl.dur, 1e-3);
                for (int r = 0; r < rep; r++) {
                    starts.add(kR.size());
                    final int nk = Math.max(2, (int) Math.rint(dur * FPS));
                    for (int k = 0; k < nk; k++) {
                        final double now = kR.size() / FPS;
                        final double[][][] wp = Npc.poseFill(sk, cl.tracks, dur * k / nk, idle.tracks, Npc.pyMod(now, idur));
                        final double[][][] R = new double[nn][][];
                        final double[][] T = new double[nn][3];
                        for (int i = 0; i < n0; i++) {
                            final int par = parents[i];
                            if (par < 0) {
                                R[i] = Np.eye3();
                                continue;
                            }
                            final double[][] L = Np.mm(wp[i], Np.inv(wp[par]));
                            if (par == root) L[3][2] -= zmin;
                            R[i] = Np.rot(L);
                            T[i] = new double[] {L[3][0] * s, L[3][1] * s, L[3][2] * s};
                        }
                        if (bolt != null) Npc.boltLocals(bolt, bIdle, now, s, R, T, n0);
                        kR.add(R);
                        kT.add(T);
                    }
                }
            }
            if (kR.isEmpty()) continue;
            HeroCels.smoothJoins(kR, kT, starts, 8, nn);
            final double top = idleHips[2] + 1.2;
            int cut = 0;
            while (cut < kR.size() - 15 && HeroCels.hipPos(kR.get(cut), kT.get(cut), hip, pelvis)[2] > top) cut++;
            final List<double[][][]> R = kR.subList(cut, kR.size());
            final List<double[][]> T = kT.subList(cut, kT.size());
            final int nk = R.size();
            final double[][] path = new double[nk][];
            for (int k = 0; k < nk; k++) {
                final double[] hp = HeroCels.hipPos(R.get(k), T.get(k), hip, pelvis);
                path[k] = new double[] {hp[0], hp[1]};
            }
            final int half = (int) (FPS / 2);
            final double[][] sm = new double[nk][2];
            for (int k = 0; k < nk; k++) {
                final int lo = Math.max(0, k - half), hi = Math.min(nk, k + half + 1);
                for (int x = 0; x < 2; x++) {
                    double acc = 0.0;
                    for (int r = lo; r < hi; r++) acc = r == lo ? path[r][x] : acc + path[r][x];
                    sm[k][x] = acc / (hi - lo) - idleHips[x];
                }
            }
            int best = 0;
            double bestN = -1;
            for (int q = 0; q < 3; q++) {
                final double nq = Math.sqrt(Np.dot(ref[q][0], ref[q][1], ref[q][0], ref[q][1]));
                if (nq > bestN) {
                    bestN = nq;
                    best = q;
                }
            }
            final double[] sins = new double[nk], coss = new double[nk];
            for (int k = 0; k < nk; k++) {
                final double[] d0 = {ref[best][0], ref[best][1]};
                final double[] d1 = {R.get(k)[pelvis][best][0], R.get(k)[pelvis][best][1]};
                final double y = Libm.atan2(d0[0] * d1[1] - d0[1] * d1[0], Np.dot(d0[0], d0[1], d1[0], d1[1]));
                sins[k] = Libm.sin(y);
                coss[k] = Libm.cos(y);
            }
            final double ym = Libm.atan2(Np.pySum(sins, nk), Np.pySum(coss, nk));
            final double cz = Libm.cos(-ym), sz = Libm.sin(-ym);
            final double[][] rz = {{cz, sz, 0.0}, {-sz, cz, 0.0}, {0.0, 0.0, 1.0}};
            final Out body = new Out();
            for (int k = 0; k < nk; k++) {
                for (int i = 0; i < nn; i++) {
                    double[][] r = R.get(k)[i];
                    double[] t = T.get(k)[i].clone();
                    if (parents[i] == root) {
                        t[0] -= sm[k][0];
                        t[1] -= sm[k][1];
                        r = Np.mm(r, rz);
                        t = Np.vm(t, rz);
                    }
                    if (parents[i] >= 0) {
                        final double[] q = HeroCels.rotToQuat(r);
                        for (double x : q) body.lef32((float) x);
                        for (double x : t) body.lef32((float) x);
                    } else {
                        body.lef32(0f).lef32(0f).lef32(0f).lef32(1f).lef32(0f).lef32(0f).lef32(0f);
                    }
                }
            }
            final Out rec = new Out();
            rec.fixed("own_cel" + (ci + 1), 48).le32(nk).bytes(body.toBytes());
            for (int i = 0; i < nn; i++) rec.u8(1);
            rec.align(4);
            recs.add(rec.toBytes());
        }
        final Out out = new Out();
        out.bytes(pack, 0, pr.anim0);
        int kept = 0;
        for (int i = 0; i < pr.clipNames.size(); i++) {
            if (pr.clipNames.get(i).startsWith("own_cel")) continue;
            out.bytes(pack, pr.clipOff.get(i), pr.clipLen.get(i));
            kept++;
        }
        for (byte[] r : recs) out.bytes(r);
        final byte[] data = out.toBytes();
        final int cnt = kept + recs.size();
        data[20] = (byte) cnt;
        data[21] = (byte) (cnt >> 8);
        data[22] = (byte) (cnt >> 16);
        data[23] = (byte) (cnt >> 24);
        return data;
    }
}
