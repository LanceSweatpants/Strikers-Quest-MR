package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * tools/outside/mua/mua_cels.py: a hero's 4 goal celebrations (its power moves, a start / loop / end joined smoothly)
 * as clips own_cel1-4 in its match pack, on the spot (the hips' slow drift taken out), turned to face as its idle does
 * on average. Run on the pack mua2gpk made (its binds read back as the floats in the pack).
 */
final class HeroCels {
    private HeroCels() {}

    static final double FPS = 30.0;

    /** The pack with its celebrations (any older own_cel clips replaced). */
    static byte[] add(Hero h, HeroPack.Costume c, byte[] pack) throws IOException {
        final IgbModel.Skel sk = c.sk;
        final int nn = sk.names.length;
        final int[] parents = sk.parents;
        final PackRead pr = new PackRead(pack);
        if (pr.nn != nn) throw new IOException(h.pack + ": " + pr.nn + " pack nodes, " + nn + " bones");
        // the pack's scale and feet: Strikers z = (MUA z - zmin) * s
        final double[] zm = new double[nn], zs = new double[nn];
        for (int i = 0; i < nn; i++) {
            zm[i] = sk.world[i][3][2];
            zs[i] = pr.binds[i][3][2];
        }
        int a = 0, b = 0;
        for (int i = 1; i < nn; i++) {
            if (zm[i] > zm[a]) a = i;
            if (zm[i] < zm[b]) b = i;
        }
        final double s = (zs[a] - zs[b]) / (zm[a] - zm[b]);
        final double zmin = zm[a] - zs[a] / s;
        Igb main = null, combat = null;
        for (String n : c.fb.names()) {
            if (!"actoranimdb".equals(c.fb.type(n))) continue;
            if (HeroPack.MAIN_SET.matcher(n).matches()) main = new Igb(c.fb.data(n));
            if (n.endsWith("_4_combat.igb")) combat = new Igb(c.fb.data(n));
        }
        int root = -1;
        for (int i = 0; i < nn; i++) if (parents[i] < 0) {
            root = i;
            break;
        }
        final int[] desc = new int[nn];
        for (int i = 0; i < nn; i++) {
            int k = i;
            while (parents[k] >= 0) {
                k = parents[k];
                desc[k]++;
            }
        }
        int pelvis = -1;
        for (int i = 0; i < nn; i++) if (parents[i] == root && (pelvis < 0 || desc[i] > desc[pelvis])) pelvis = i;
        IgbAnim.Clip idle = IgbAnim.clip(main, "idle");
        if (idle == null) idle = IgbAnim.clip(combat, "idle");
        if (idle == null) throw new IOException(h.pack + ": no idle");
        final double[][][] w0 = IgbAnim.poseWorld(sk, idle.tracks, 0.0);
        final double[][] ref = Np.rot(Np.mm(w0[pelvis], Np.inv(w0[root])));
        int hip = -1;
        for (int i = 0; i < nn; i++) if (parents[i] == pelvis && (hip < 0 || desc[i] > desc[hip])) hip = i;
        if (hip < 0) hip = pelvis;
        final double[][] l0 = Np.mm(w0[hip], Np.inv(w0[root]));
        final double[] idleHips = {l0[3][0] * s, l0[3][1] * s, (l0[3][2] - zmin) * s};

        final List<byte[]> recs = new ArrayList<>();
        for (int ci = 0; ci < h.cels.size(); ci++) {
            final List<double[][][]> kR = new ArrayList<>();
            final List<double[][]> kT = new ArrayList<>();
            final List<Integer> starts = new ArrayList<>();
            for (Object[] piece : h.cels.get(ci)) {
                final String cname = (String) piece[0];
                final int rep = (Integer) piece[1];
                IgbAnim.Clip cl = IgbAnim.clip(combat, cname);
                if (cl == null) cl = IgbAnim.clip(main, cname);
                if (cl == null) continue;
                final double dur = Math.max(cl.dur, 1e-3);
                for (int r = 0; r < rep; r++) {
                    starts.add(kR.size());
                    final int nk = Math.max(2, (int) Math.rint(dur * FPS));
                    for (int k = 0; k < nk; k++) {
                        final double[][][] wp = IgbAnim.poseWorld(sk, cl.tracks, dur * k / nk);
                        final double[][][] R = new double[nn][][];
                        final double[][] T = new double[nn][3];
                        for (int i = 0; i < nn; i++) {
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
                        kR.add(R);
                        kT.add(T);
                    }
                }
            }
            if (kR.isEmpty()) continue;
            smoothJoins(kR, kT, starts, 8, nn);
            // a move starting far up in the air starts where its hips come within 1.2 over the idle's
            final double top = idleHips[2] + 1.2;
            int cut = 0;
            while (cut < kR.size() - 15 && hipPos(kR.get(cut), kT.get(cut), hip, pelvis)[2] > top) cut++;
            final List<double[][][]> R = kR.subList(cut, kR.size());
            final List<double[][]> T = kT.subList(cut, kT.size());
            final int nk = R.size();
            // on the spot: the hips' path smoothed over a second taken away
            final double[][] path = new double[nk][];
            for (int k = 0; k < nk; k++) {
                final double[] hp = hipPos(R.get(k), T.get(k), hip, pelvis);
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
            // facing: the hips' turn from the idle's, averaged; the root's children turned back by it
            int best = 0;
            double bestN = -1;
            for (int q = 0; q < 3; q++) {
                final double nq = Math.sqrt(Np.dot(ref[q][0], ref[q][1], ref[q][0], ref[q][1]));
                if (nq > bestN) {
                    bestN = nq;
                    best = q;
                }
            }
            final double[] yaws = new double[nk], sins = new double[nk], coss = new double[nk];
            for (int k = 0; k < nk; k++) {
                final double[] d0 = {ref[best][0], ref[best][1]};
                final double[] d1 = {R.get(k)[pelvis][best][0], R.get(k)[pelvis][best][1]};
                yaws[k] = Libm.atan2(d0[0] * d1[1] - d0[1] * d1[0], Np.dot(d0[0], d0[1], d1[0], d1[1]));
                sins[k] = Libm.sin(yaws[k]);
                coss[k] = Libm.cos(yaws[k]);
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
                        final double[] q = rotToQuat(r);
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
        // the pack's clips: its own, less any older celebrations, then these
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

    static double[] hipPos(double[][][] R, double[][] T, int hip, int pelvis) {
        if (hip == pelvis) return T[pelvis].clone();
        final double[] v = Np.vm(T[hip], R[pelvis]);
        return new double[] {v[0] + T[pelvis][0], v[1] + T[pelvis][1], v[2] + T[pelvis][2]};
    }

    /** mua_cels.smooth_joins: each join's difference carried on the next piece and let go over n keys. */
    static void smoothJoins(List<double[][][]> kR, List<double[][]> kT, List<Integer> starts, int n, int nn) {
        for (int b : starts) {
            if (b <= 0 || b >= kR.size()) continue;
            final double[][][] Rp = kR.get(b - 1), R0 = kR.get(b);
            final double[][] Tp = kT.get(b - 1), T0 = kT.get(b);
            final double[][] d = new double[nn][];
            for (int i = 0; i < nn; i++) {
                final double[] qp = rotToQuat(Rp[i]), q0 = rotToQuat(R0[i]);
                double[] dq = qmul(qp, new double[] {-q0[0], -q0[1], -q0[2], q0[3]});
                if (dq[3] < 0) dq = new double[] {-dq[0], -dq[1], -dq[2], -dq[3]};
                d[i] = dq;
            }
            final double[][] dt = new double[nn][3];
            for (int i = 0; i < nn; i++) for (int x = 0; x < 3; x++) dt[i][x] = Tp[i][x] - T0[i][x];
            for (int j = 0; j < n; j++) {
                final int k = b + j;
                if (k >= kR.size()) break;
                final double keep = 1.0 - (j + 1) / (n + 1.0);
                final double[][][] R = kR.get(k);
                final double[][] T = kT.get(k);
                final double[][][] R2 = new double[nn][][];
                for (int i = 0; i < nn; i++) {
                    double[] q = {d[i][0] * keep, d[i][1] * keep, d[i][2] * keep, d[i][3] * keep + (1.0 - keep)};
                    double ql = Math.sqrt(Np.npSum(new double[] {q[0] * q[0], q[1] * q[1], q[2] * q[2], q[3] * q[3]}, 4));
                    if (ql == 0.0) ql = 1.0;
                    q = new double[] {q[0] / ql, q[1] / ql, q[2] / ql, q[3] / ql};
                    R2[i] = qToRows(qmul(q, rotToQuat(R[i])));
                }
                final double[][] T2 = new double[nn][3];
                for (int i = 0; i < nn; i++) for (int x = 0; x < 3; x++) T2[i][x] = T[i][x] + dt[i][x] * keep;
                kR.set(k, R2);
                kT.set(k, T2);
            }
        }
    }

    /** mua_cels.rot_to_quat (math.sqrt; the length's sum over numpy values plain). */
    static double[] rotToQuat(double[][] m) {
        final double tr = m[0][0] + m[1][1] + m[2][2];
        double[] q;
        if (tr > 0) {
            final double S = Math.sqrt(tr + 1.0) * 2;
            q = new double[] {(m[1][2] - m[2][1]) / S, (m[2][0] - m[0][2]) / S, (m[0][1] - m[1][0]) / S, 0.25 * S};
        } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            final double S = Math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
            q = new double[] {0.25 * S, (m[0][1] + m[1][0]) / S, (m[2][0] + m[0][2]) / S, (m[1][2] - m[2][1]) / S};
        } else if (m[1][1] > m[2][2]) {
            final double S = Math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
            q = new double[] {(m[0][1] + m[1][0]) / S, 0.25 * S, (m[1][2] + m[2][1]) / S, (m[2][0] - m[0][2]) / S};
        } else {
            final double S = Math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
            q = new double[] {(m[2][0] + m[0][2]) / S, (m[1][2] + m[2][1]) / S, 0.25 * S, (m[0][1] - m[1][0]) / S};
        }
        double n = Math.sqrt(Np.npSum(new double[] {q[0] * q[0], q[1] * q[1], q[2] * q[2], q[3] * q[3]}, 4));
        if (n == 0.0) n = 1.0;
        return new double[] {q[0] / n, q[1] / n, q[2] / n, q[3] / n};
    }

    /** mua_cels.q_to_rows: the row-form 3 x 3 (the transpose of the usual column form). */
    static double[][] qToRows(double[] q) {
        final double x = q[0], y = q[1], z = q[2], w = q[3];
        final double[][] m = {
            {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
            {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
            {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
        return new double[][] {{m[0][0], m[1][0], m[2][0]}, {m[0][1], m[1][1], m[2][1]}, {m[0][2], m[1][2], m[2][2]}};
    }

    static double[] qmul(double[] a, double[] b) {
        final double ax = a[0], ay = a[1], az = a[2], aw = a[3];
        final double bx = b[0], by = b[1], bz = b[2], bw = b[3];
        return new double[] {aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz};
    }

    // ------------------------------------------------------------------------------------------------ reading
    /** A GPK2 pack's layout (mua_cels.read_pack / build_home.read_pack). */
    static final class PackRead {
        final int nn, npk, nt, na;
        final float height;
        final byte[] name32 = new byte[32];
        final int mode;
        final double[][][] binds;
        final int[] parents;
        final int nodesOff, texOff, texEnd, pkEnd, anim0;
        final List<String> clipNames = new ArrayList<>();
        final List<Integer> clipOff = new ArrayList<>(), clipLen = new ArrayList<>();

        PackRead(byte[] b) {
            nn = le32(b, 8);
            npk = le32(b, 12);
            nt = le32(b, 16);
            na = le32(b, 20);
            height = Float.intBitsToFloat(le32(b, 24));
            System.arraycopy(b, 28, name32, 0, 32);
            mode = le32(b, 60);
            int p = 64;
            nodesOff = p;
            binds = new double[nn][4][4];
            parents = new int[nn];
            for (int i = 0; i < nn; i++) {
                parents[i] = le32(b, p + 4);
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++) binds[i][r][c] = Float.intBitsToFloat(le32(b, p + 92 + 4 * (4 * r + c)));
                p += 156;
            }
            texOff = p;
            for (int i = 0; i < nt; i++) {
                final int sz = le32(b, p + 4);
                p += 8 + ((sz + 3) & ~3);
            }
            texEnd = p;
            for (int i = 0; i < npk; i++) {
                final int nv = le32(b, p + 4), ni = le32(b, p + 8);
                p += 12 + (nv & 0x0FFFFFFF) * 52 + ((ni * 2 + 3) & ~3);
            }
            pkEnd = p;
            anim0 = p;
            for (int i = 0; i < na; i++) {
                int e = p;
                while (e < p + 48 && b[e] != 0) e++;
                clipNames.add(new String(b, p, e - p, java.nio.charset.StandardCharsets.ISO_8859_1));
                final int keys = le32(b, p + 48);
                final int size = 52 + keys * nn * 28 + ((nn + 3) & ~3);
                clipOff.add(p);
                clipLen.add(size);
                p += size;
            }
        }

        static int le32(byte[] b, int o) {
            return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
        }
    }
}
