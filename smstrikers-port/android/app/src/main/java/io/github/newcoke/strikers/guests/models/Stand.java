package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * r130 Vaporeon: Colosseum stores it SITTING (body turned 72 deg, head to the camera, hind legs folded), so the PC
 * re-posed it into a standing four-legged bind (outside\colo-maps\vaporeon\vap_stand.py with its defaults, on the
 * plain map-less build) and put its own map on that (vap_applymap.py + colo-maps\vaporeon.json). The same here, step
 * for step (numpy's sums as Np / the PC's numpy does them, Python's set orders as PySet):
 *  0. the hind legs' skin weights blurred 4 passes (vaprepose.smooth_weights);
 *  1. its own attack animation (1) at frame 31, Colosseum's keys read little-endian (vapanim.py), put in pack space
 *     by the similarity that fits the model's joints to the pack's (vaprepose.fit);
 *  2. the spine straightened, the head faced along the body as the stored bind holds it, the body turned to face -Y
 *     over the origin, the legs set under the body (front: shoulder / elbow zigzag; hind: the frame's left leg swung
 *     under the hip, the right one its mirror image), each leg's angle bisected so all four paws reach one floor;
 *  3. the mesh skinned into it (dual quaternions), the hind knees relaxed (Taubin), on the floor, sized to 1.25 m, and
 *     that made the bind; then the map's hashes and mode bits.
 * The 3 x 3 SVDs (the fit, each joint's turn) are this code's own (Jacobi): they decide the last bits only.
 */
final class Stand {
    private Stand() {}

    private static final int ANIM = 1;
    private static final double FRAME = 31.0, HEIGHT = 1.25, SPLAY = 0.06, HIND_TILT = 0.0, FRONT_LIFT = 0.05;
    private static final int SMOOTH = 4, RELAX = 10;
    private static final int[] RELAX_JOINTS = {2, 3, 4, 5, 10, 11, 12, 13};
    private static final int[][] FRONT = {{50, 51, 53, 55, 54}, {56, 57, 59, 61, 60}};  // l, r: shoulder .. toe tip
    private static final int[][] HIND = {{2, 3, 4, 5, 7, 8}, {10, 11, 12, 13, 15, 16}};  // l, r: hip .. toe tip
    private static final double[] UP = {0.0, 0.0, 1.0}, FWD = {0.0, -1.0, 0.0};

    /** vap_stand.py's input: colo_guests.py's plain build with no bone map (outside\colo-guests\pk_vaporeon.gpk). */
    static byte[] rawBuild(Models.Spec s, Colo.Dat dat) throws IOException {
        final MeleePack.Options o = new MeleePack.Options();
        o.name = s.pack;
        o.root = dat.root;
        o.modelSpaceEnv = true;
        o.subdivide = 1;
        if (dat.matanim != 0) o.matanim = dat.matanim;
        o.height = Py.num(Py.fixed(s.gameHeight != null ? Py.num(s.gameHeight) : Models.gameHeight(Py.num(s.heightM)), 3));
        o.map = new MeleePack.BoneMap();
        o.ground = 0.0;
        return MeleePack.run(dat.dat, o).gpk;
    }

    static byte[] build(Models.Spec s, Colo.Dat dat, byte[] fsys) throws IOException {
        if (!"vaporeon".equals(s.stand)) throw new IOException("stand: only Vaporeon's is known");
        final Pk pk = new Pk(rawBuild(s, dat));
        smoothWeights(pk, range(2, 18), SMOOTH, 2);
        final double[][] keep0 = copy(pk.bw[0]), keep76 = copy(pk.bw[76]);
        final double[][][] wp = pose(pk, new Model(dat), s.standFit);
        wp[0] = keep0;
        wp[76] = keep76;
        final double[][][] pn = skinDq(pk, wp);
        double[][] P = pn[0], N = pn[1];
        final double[][][] rx = relax(pk, P, N, RELAX_JOINTS, RELAX, 0.5, -0.53);
        P = rx[0];
        N = rx[1];
        // floor
        double lo = Double.POSITIVE_INFINITY;
        for (double[] p : P) lo = Math.min(lo, p[2]);
        final double dz = -lo;
        for (double[] p : P) p[2] += dz;
        for (int k = 0; k < pk.nn; k++) if (k != 0 && k != 76) wp[k][3][2] += dz;
        double h = Double.NEGATIVE_INFINITY;
        for (double[] p : P) h = Math.max(h, p[2]);
        final double sc = HEIGHT / h;
        for (double[] p : P) for (int c = 0; c < 3; c++) p[c] *= sc;
        for (int k = 0; k < pk.nn; k++) if (k != 0 && k != 76) for (int c = 0; c < 3; c++) wp[k][3][c] *= sc;
        final byte[] out = pk.write(wp, P, N, h * sc);
        return applyMap(out, s);
    }

    // ================================================================================================ the pack
    /** vaprepose.Pack: a GPK2 pack's nodes, skin (P, N, I, W per vertex) and faces. */
    static final class Pk {
        final byte[] b;
        final int nn;
        final int[] parent, map;
        final double[][][] inv, bw;
        final List<int[]> blocks = new ArrayList<>();  // {vertex block offset, count}
        double[][] P, N, W;
        int[][] I, tris;
        boolean weightsChanged;

        Pk(byte[] in) {
            b = in;
            nn = Bin.le32(b, 8);
            final int npk = Bin.le32(b, 12), nt = Bin.le32(b, 16);
            parent = new int[nn];
            map = new int[nn];
            inv = new double[nn][][];
            bw = new double[nn][][];
            for (int i = 0; i < nn; i++) {
                final int o = 64 + 156 * i;
                parent[i] = Bin.le32(b, o + 4);
                inv[i] = Fix.mat(b, o + 24);
                map[i] = Bin.le32(b, o + 88);
                bw[i] = Fix.mat(b, o + 92);
            }
            int p = 64 + 156 * nn;
            for (int i = 0; i < nt; i++) p += 8 + ((Bin.le32(b, p + 4) + 3) & ~3);
            final List<double[]> pl = new ArrayList<>(), nl = new ArrayList<>(), wl = new ArrayList<>();
            final List<int[]> il = new ArrayList<>(), tl = new ArrayList<>();
            int base = 0;
            for (int k = 0; k < npk; k++) {
                final int nv = Bin.le32(b, p + 4) & 0x0FFFFFFF, ni = Bin.le32(b, p + 8);
                p += 12;
                blocks.add(new int[] {p, nv});
                for (int v = 0; v < nv; v++) {
                    final int q = p + 52 * v;
                    pl.add(new double[] {Bin.lef32(b, q), Bin.lef32(b, q + 4), Bin.lef32(b, q + 8)});
                    nl.add(new double[] {Bin.lef32(b, q + 12), Bin.lef32(b, q + 16), Bin.lef32(b, q + 20)});
                    final int[] js = new int[4];
                    final double[] ws = new double[4];
                    for (int c = 0; c < 4; c++) {
                        js[c] = b[q + 32 + c] & 0xFF;
                        ws[c] = Bin.lef32(b, q + 36 + 4 * c);
                        if (js[c] >= nn) {
                            ws[c] = 0.0;
                            js[c] = 0;
                        }
                    }
                    il.add(js);
                    wl.add(ws);
                }
                final int so = p + 52 * nv;
                for (int s = 0; s + 2 < ni; s++) {
                    int a = Bin.le16(b, so + 2 * s), bb = Bin.le16(b, so + 2 * s + 2);
                    final int c = Bin.le16(b, so + 2 * s + 4);
                    if (s % 2 == 1) {
                        final int t = a;
                        a = bb;
                        bb = t;
                    }
                    if (a != bb && bb != c && a != c) tl.add(new int[] {a + base, bb + base, c + base});
                }
                base += nv;
                p += 52 * nv + ((ni * 2 + 3) & ~3);
            }
            P = pl.toArray(new double[0][]);
            N = nl.toArray(new double[0][]);
            W = wl.toArray(new double[0][]);
            I = il.toArray(new int[0][]);
            tris = tl.toArray(new int[0][]);
        }

        /** Each vertex's leading joint: I[v][argmax W[v]] (the first of equal biggest). */
        int[] dom() {
            final int[] d = new int[P.length];
            for (int v = 0; v < P.length; v++) {
                int k = 0;
                for (int c = 1; c < 4; c++) if (W[v][c] > W[v][k]) k = c;
                d[v] = I[v][k];
            }
            return d;
        }

        /** vaprepose.Pack.skin's places (linear blend): only for the legs' floor search, so only its height (z). */
        double[] skinZ(double[][][] worlds) {
            final double[][][] m = new double[nn][][];
            for (int i = 0; i < nn; i++) m[i] = Np.mm(inv[i], worlds[i]);
            final double[] z = new double[P.length];
            for (int v = 0; v < P.length; v++) {
                final double[] p = P[v];
                double acc = 0.0, tw = 0.0;
                for (int k = 0; k < 4; k++) {
                    final double[][] mk = m[I[v][k]];
                    final double e = ((p[0] * mk[0][2] + p[1] * mk[1][2]) + p[2] * mk[2][2]) + mk[3][2];
                    acc = acc + W[v][k] * e;
                    tw = tw + W[v][k];
                }
                z[v] = acc / (tw > 0 ? tw : 1.0);
            }
            return z;
        }

        /** rebind + write: the pose made the bind (nodes: bind world, inverse bind, rest offset), vertices, weights. */
        byte[] write(double[][][] worlds, double[][] p, double[][] n, double height) {
            final byte[] o = Arrays.copyOf(b, b.length);
            Fix.putF(o, 24, height);
            final double[][][] iv = new double[nn][][];
            for (int i = 0; i < nn; i++) iv[i] = Np.inv(worlds[i]);
            for (int i = 0; i < nn; i++) {
                final int off = 64 + 156 * i;
                final double[] tr = parent[i] >= 0 ? Np.mm(worlds[i], Np.inv(worlds[parent[i]]))[3] : worlds[i][3];
                for (int c = 0; c < 3; c++) Fix.putF(o, off + 8 + 4 * c, tr[c]);
                Fix.putMat(o, off + 24, iv[i]);
                Fix.putI(o, off + 88, map[i]);
                Fix.putMat(o, off + 92, worlds[i]);
            }
            int v0 = 0;
            for (int[] blk : blocks) {
                for (int v = 0; v < blk[1]; v++) {
                    final int q = blk[0] + 52 * v;
                    for (int c = 0; c < 3; c++) Fix.putF(o, q + 4 * c, p[v0 + v][c]);
                    for (int c = 0; c < 3; c++) Fix.putF(o, q + 12 + 4 * c, n[v0 + v][c]);
                    if (weightsChanged) {
                        final double[] w = W[v0 + v];
                        for (int c = 0; c < 4; c++) {
                            o[q + 32 + c] = (byte) (w[c] > 0 ? I[v0 + v][c] : 0xFF);
                            Fix.putF(o, q + 36 + 4 * c, w[c] > 0 ? w[c] : 0.0);
                        }
                    }
                }
                v0 += blk[1];
            }
            return o;
        }
    }

    // ================================================================================================ weights
    /** Vertices joined by place (np.round(P, 5) as the key): ids in first-seen order. */
    private static int[] weld(Pk pk, int[] count) {
        final Map<List<Long>, Integer> key = new HashMap<>();
        final int[] wid = new int[pk.P.length];
        for (int i = 0; i < pk.P.length; i++) {
            final List<Long> k = new ArrayList<>(3);
            for (int c = 0; c < 3; c++) k.add(Double.doubleToLongBits(Math.rint(pk.P[i][c] * 100000.0) / 100000.0 + 0.0));
            Integer id = key.get(k);
            if (id == null) {
                id = key.size();
                key.put(k, id);
            }
            wid[i] = id;
        }
        count[0] = key.size();
        return wid;
    }

    /** The neighbour sets (Python sets, their order kept): each face's corners. */
    private static PySet[] neighbours(Pk pk, int[] wid, int nw) {
        final PySet[] nb = new PySet[nw];
        for (int i = 0; i < nw; i++) nb[i] = new PySet();
        for (int[] t : pk.tris) {
            final int a = wid[t[0]], b = wid[t[1]], c = wid[t[2]];
            nb[a].merge(PySet.of(b, c));
            nb[b].merge(PySet.of(a, c));
            nb[c].merge(PySet.of(a, b));
        }
        return nb;
    }

    /** numpy's pairwise sum of a[0..n) (add.reduce along a contiguous axis), from the identity 0. */
    static double pairwise(double[] a, int off, int n) {
        return 0.0 + pw(a, off, n);
    }

    private static double pw(double[] a, int off, int n) {
        if (n < 8) {
            double res = -0.0;
            for (int i = 0; i < n; i++) res += a[off + i];
            return res;
        } else if (n <= 128) {
            final double[] r = new double[8];
            for (int j = 0; j < 8; j++) r[j] = a[off + j];
            int i;
            for (i = 8; i < n - (n % 8); i += 8) for (int j = 0; j < 8; j++) r[j] += a[off + i + j];
            double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
            for (; i < n; i++) res += a[off + i];
            return res;
        }
        int n2 = n / 2;
        n2 -= n2 % 8;
        return pw(a, off, n2) + pw(a, off + n2, n - n2);
    }

    /**
     * vaprepose.smooth_weights: where the given joints carry weight (+ rings of neighbours), each pass a vertex takes
     * the mean of itself and its neighbours; then its 4 biggest, summing to 1.
     */
    static void smoothWeights(Pk pk, int[] joints, int iters, int rings) {
        final int n = pk.P.length, nn = pk.nn;
        final int[] cnt0 = new int[1];
        final int[] wid = weld(pk, cnt0);
        final int nw = cnt0[0];
        final PySet[] nb = neighbours(pk, wid, nw);
        double[][] wd = new double[nw][nn];
        final double[] cnt = new double[nw];
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < 4; k++) wd[wid[i]][pk.I[i][k]] += pk.W[i][k];
            cnt[wid[i]] += 1;
        }
        for (int r = 0; r < nw; r++) {
            final double d = Math.max(cnt[r], 1.0);
            for (int c = 0; c < nn; c++) wd[r][c] /= d;
        }
        final Set<Integer> region = new HashSet<>();
        final double[] sel = new double[joints.length];
        for (int r = 0; r < nw; r++) {
            for (int k = 0; k < joints.length; k++) sel[k] = wd[r][joints[k]];
            if (pairwise(sel, 0, joints.length) > 1e-6) region.add(r);
        }
        for (int g = 0; g < rings; g++) {
            final Set<Integer> add = new HashSet<>();
            for (int i : region) for (int j : nb[i].toArray()) add.add(j);
            region.addAll(add);
        }
        final int[] reg = region.stream().mapToInt(Integer::intValue).sorted().toArray();
        for (int it = 0; it < iters; it++) {
            final double[][] nw2 = new double[nw][];
            for (int r = 0; r < nw; r++) nw2[r] = wd[r].clone();
            for (int i : reg) {
                if (nb[i].isEmpty()) continue;
                final int[] ks = nb[i].toArray();
                final double[] s = new double[nn];
                for (int j : ks) for (int c = 0; c < nn; c++) s[c] = s[c] + wd[j][c];
                final double div = 1 + ks.length;
                for (int c = 0; c < nn; c++) nw2[i][c] = (wd[i][c] + s[c]) / div;
            }
            wd = nw2;
        }
        final int[][] I = new int[n][4];
        final double[][] W = new double[n][4];
        for (int i = 0; i < n; i++) {
            if (!region.contains(wid[i])) {
                I[i] = pk.I[i].clone();
                W[i] = pk.W[i].clone();
                continue;
            }
            final double[] row = wd[wid[i]];
            final int[] top = top4(row);
            final double[] w = new double[4];
            for (int k = 0; k < 4; k++) w[k] = row[top[k]] > 1e-4 ? row[top[k]] : 0.0;
            final double sum = pairwise(w, 0, 4);
            for (int k = 0; k < 4; k++) w[k] = w[k] / sum;
            I[i] = top;
            W[i] = w;
        }
        pk.I = I;
        pk.W = W;
        pk.weightsChanged = true;
    }

    /** np.argsort(-row)[:4]: the 4 biggest (equal ones by index: none of the made weights tie). */
    private static int[] top4(double[] row) {
        final Integer[] idx = new Integer[row.length];
        for (int i = 0; i < idx.length; i++) idx[i] = i;
        Arrays.sort(idx, (x, y) -> {
            final double a = -row[x], b = -row[y];
            if (a < b) return -1;
            if (a > b) return 1;
            return Integer.compare(x, y);
        });
        return new int[] {idx[0], idx[1], idx[2], idx[3]};
    }

    /** vaprepose.relax: Taubin smoothing where the joints lead, seams joined by place; their normals from the faces. */
    static double[][][] relax(Pk pk, double[][] P, double[][] N, int[] joints, int iters, double lam, double mu) {
        final int n = P.length;
        final int[] cnt0 = new int[1];
        final int[] wid = weld(pk, cnt0);
        final int nw = cnt0[0];
        final PySet[] nb = neighbours(pk, wid, nw);
        final int[] dom = pk.dom();
        final Set<Integer> js = new HashSet<>();
        for (int j : joints) js.add(j);
        final boolean[] sel = new boolean[nw];
        for (int i = 0; i < n; i++) if (js.contains(dom[i])) sel[wid[i]] = true;
        double[][] Q = new double[nw][3];
        for (int i = 0; i < n; i++) Q[wid[i]] = P[i].clone();  // (Q[wid] = P: the last one wins)
        for (int it = 0; it < iters; it++) {
            for (double f : new double[] {lam, mu}) {
                final double[][] q2 = new double[nw][];
                for (int r = 0; r < nw; r++) q2[r] = Q[r].clone();
                for (int i = 0; i < nw; i++) {
                    if (!sel[i] || nb[i].isEmpty()) continue;
                    final int[] ks = nb[i].toArray();
                    final double[] m = new double[3];
                    for (int j : ks) for (int c = 0; c < 3; c++) m[c] = m[c] + Q[j][c];
                    for (int c = 0; c < 3; c++) {
                        m[c] = m[c] / ks.length;
                        q2[i][c] = Q[i][c] + f * (m[c] - Q[i][c]);
                    }
                }
                Q = q2;
            }
        }
        final double[][] P2 = new double[n][];
        for (int i = 0; i < n; i++) P2[i] = Q[wid[i]].clone();
        final double[][] acc = new double[nw][3];
        final double[][] fn = new double[pk.tris.length][];
        for (int t = 0; t < pk.tris.length; t++) {
            final double[] a = P2[pk.tris[t][0]], b = P2[pk.tris[t][1]], c = P2[pk.tris[t][2]];
            fn[t] = Np.cross(new double[] {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, new double[] {c[0] - a[0], c[1] - a[1], c[2] - a[2]});
        }
        for (int k = 0; k < 3; k++)
            for (int t = 0; t < pk.tris.length; t++) {
                final double[] r = acc[wid[pk.tris[t][k]]];
                for (int c = 0; c < 3; c++) r[c] = r[c] + fn[t][c];
            }
        final double[][] N2 = new double[n][];
        for (int i = 0; i < n; i++) {
            N2[i] = N[i].clone();
            if (!sel[wid[i]]) continue;
            final double[] v = acc[wid[i]].clone();
            final double ln = Math.sqrt(pairwise(new double[] {v[0] * v[0], v[1] * v[1], v[2] * v[2]}, 0, 3));
            final double d = ln > 1e-12 ? ln : 1.0;
            for (int c = 0; c < 3; c++) v[c] = v[c] / d;
            final double flip = pairwise(new double[] {v[0] * N[i][0], v[1] * N[i][1], v[2] * N[i][2]}, 0, 3);
            if (flip < 0) for (int c = 0; c < 3; c++) v[c] *= -1;
            N2[i] = v;
        }
        return new double[][][] {P2, N2};
    }

    // ================================================================================================ skin
    /** vap_stand.skin_dq: dual-quaternion skinning into the worlds (each joint's turn made a pure rotation). */
    static double[][][] skinDq(Pk pk, double[][][] worlds) {
        final int nn = pk.nn;
        final double[][] qr = new double[nn][], qd = new double[nn][];
        for (int i = 0; i < nn; i++) {
            final double[][] s = Np.mm(pk.inv[i], worlds[i]);
            final double[][] rc = new double[3][3];
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) rc[r][c] = s[c][r];
            final double[][][] usv = Svd3.svd(rc);
            final double[][] rot = Np.mm(usv[0], usv[2]);
            final double[] q = matToQuat(rot);
            qr[i] = q;
            final double[] dq = qmul(new double[] {0.0, s[3][0], s[3][1], s[3][2]}, q);
            for (int c = 0; c < 4; c++) dq[c] = 0.5 * dq[c];
            qd[i] = dq;
        }
        final int n = pk.P.length;
        final int[] dom = pk.dom();
        final double[][] P = new double[n][], N = new double[n][];
        for (int v = 0; v < n; v++) {
            final double[] br = new double[4], bd = new double[4];
            final double[] rd = qr[dom[v]];
            for (int k = 0; k < 4; k++) {
                final double[] r = qr[pk.I[v][k]], d = qd[pk.I[v][k]];
                final double dt = pairwise(new double[] {r[0] * rd[0], r[1] * rd[1], r[2] * rd[2], r[3] * rd[3]}, 0, 4);
                double sgn = Math.signum(dt);
                if (sgn == 0) sgn = 1.0;
                final double f = pk.W[v][k] * sgn;
                for (int c = 0; c < 4; c++) {
                    br[c] = br[c] + f * r[c];
                    bd[c] = bd[c] + f * d[c];
                }
            }
            final double nrm = Math.sqrt(pairwise(new double[] {br[0] * br[0], br[1] * br[1], br[2] * br[2], br[3] * br[3]}, 0, 4));
            for (int c = 0; c < 4; c++) {
                br[c] = br[c] / nrm;
                bd[c] = bd[c] / nrm;
            }
            final double[] conj = {br[0], br[1] * -1.0, br[2] * -1.0, br[3] * -1.0};
            final double[] tq = qmul(bd, conj);
            final double[] t = {2.0 * tq[1], 2.0 * tq[2], 2.0 * tq[3]};
            final double[] p = qrot(br, pk.P[v]);
            P[v] = new double[] {p[0] + t[0], p[1] + t[1], p[2] + t[2]};
            N[v] = qrot(br, pk.N[v]);
        }
        return new double[][][] {P, N};
    }

    /** vap_stand.mat_to_quat: column-form rotation -> (w, x, y, z), made unit. */
    static double[] matToQuat(double[][] m) {
        final double tr = m[0][0] + m[1][1] + m[2][2];
        final double[] q;
        if (tr > 0) {
            final double s = Math.sqrt(tr + 1.0) * 2;
            q = new double[] {0.25 * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s};
        } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            final double s = Math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
            q = new double[] {(m[2][1] - m[1][2]) / s, 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s};
        } else if (m[1][1] > m[2][2]) {
            final double s = Math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
            q = new double[] {(m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s};
        } else {
            final double s = Math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
            q = new double[] {(m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s};
        }
        final double n = Np.norm(q);
        return new double[] {q[0] / n, q[1] / n, q[2] / n, q[3] / n};
    }

    static double[] qmul(double[] a, double[] b) {
        final double w1 = a[0], x1 = a[1], y1 = a[2], z1 = a[3], w2 = b[0], x2 = b[1], y2 = b[2], z2 = b[3];
        return new double[] {
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2};
    }

    /** vap_stand.qrot: v turned by the unit quaternion q. */
    static double[] qrot(double[] q, double[] v) {
        final double w = q[0];
        final double[] u = {q[1], q[2], q[3]};
        final double[] c = Np.cross(u, v);
        final double[] t = {2.0 * c[0], 2.0 * c[1], 2.0 * c[2]};
        final double[] c2 = Np.cross(u, t);
        return new double[] {(v[0] + w * t[0]) + c2[0], (v[1] + w * t[1]) + c2[1], (v[2] + w * t[2]) + c2[2]};
    }

    // ================================================================================================ the pose
    /**
     * vaprepose.fit's numbers (sc, Rm, tv) from this code's own 3 x 3 SVD, then nudged by the data's differences
     * (stand_fit: numpy's LAPACK rounds the last bits its own way, as libm.txt's nudges for cos / sin; Rm's near-zero
     * entries take the SVD's noise), so the pose is the PC's to the last bit. Each nudge is tiny (checked).
     */
    static Fit fitOf(Pk pk, Model m, double[] nudge) throws IOException {
        if (m.joints.size() != pk.nn) throw new IOException("stand: pack " + pk.nn + " nodes, model " + m.joints.size());
        final double[][] X = new double[pk.nn][], Y = new double[pk.nn][];
        for (int i = 0; i < pk.nn; i++) {
            final double[][] b = m.bind.get(i);
            X[i] = new double[] {b[0][3], b[1][3], b[2][3]};
            Y[i] = new double[] {pk.bw[i][3][0], pk.bw[i][3][1], pk.bw[i][3][2]};
        }
        final Fit f = fit(X, Y);
        if (nudge != null && nudge.length == 13) {
            f.sc = nudged(f.sc, nudge[0]);
            for (int i = 0; i < 9; i++) f.rm[i / 3][i % 3] = nudged(f.rm[i / 3][i % 3], nudge[1 + i]);
            for (int i = 0; i < 3; i++) f.tv[i] = nudged(f.tv[i], nudge[10 + i]);
        }
        return f;
    }

    /** v + d where d is noise-sized (else the data doesn't belong to these files: v kept). */
    private static double nudged(double v, double d) {
        return Math.abs(d) <= 1e-12 * Math.max(1.0, Math.abs(v)) ? v + d : v;
    }

    /** The 13 fit numbers before any nudge (the PC data tool works the nudges out from them). */
    static double[] fitValues(Models.Spec s, Colo.Dat dat) throws IOException {
        final Fit f = fitOf(new Pk(rawBuild(s, dat)), new Model(dat), null);
        final double[] r = new double[13];
        r[0] = f.sc;
        for (int i = 0; i < 9; i++) r[1 + i] = f.rm[i / 3][i % 3];
        for (int i = 0; i < 3; i++) r[10 + i] = f.tv[i];
        return r;
    }

    /** vap_stand.build with its defaults: the frame in pack space, straightened, head and body turned, legs set. */
    static double[][][] pose(Pk pk, Model m, double[] nudge) throws IOException {
        final Fit fit = fitOf(pk, m, nudge);
        final Map<Integer, Map<Integer, Integer>> tracks = m.animTracks(ANIM);
        final List<double[][]> frame = m.pose(ANIM, FRAME, tracks);
        final double[][][] wp = new double[pk.nn][][];
        for (int i = 0; i < pk.nn; i++) wp[i] = toPack(frame.get(i), fit);
        final Legs g = new Legs(pk, wp);
        g.straighten();
        g.head();
        g.body();
        g.standLegs();
        return wp;
    }

    static final class Fit {
        double sc;
        double[][] rm;
        double[] tv;
    }

    /** vaprepose.fit: file space -> pack space similarity from the joints' bind places. */
    static Fit fit(double[][] X, double[][] Y) {
        final int n = X.length;
        final double[] mx = mean0(X), my = mean0(Y);
        final double[][] xc = new double[n][3], yc = new double[n][3];
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 3; c++) {
                xc[i][c] = X[i][c] - mx[c];
                yc[i][c] = Y[i][c] - my[c];
            }
        final double[][] h = new double[3][3];  // (X - mx).T @ (Y - my): dgemm, k in order
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                double s = 0.0;
                for (int k = 0; k < n; k++) s = Fma.fma(xc[k][i], yc[k][j], s);
                h[i][j] = s;
            }
        final double[][][] usv = Svd3.svd(h);
        final double[][] u = usv[0], vt = usv[2];
        final double[] sv = usv[1][0];
        final double[][] d = Fix.eye(3);
        d[2][2] = Math.signum(det3(Np.mm(u, vt)));
        final Fit f = new Fit();
        f.rm = Np.mm(Np.mm(u, d), vt);
        final double num = pairwise(new double[] {sv[0] * d[0][0], sv[1] * d[1][1], sv[2] * d[2][2]}, 0, 3);
        final double[] sq = new double[n * 3];
        for (int i = 0; i < n; i++) for (int c = 0; c < 3; c++) sq[i * 3 + c] = xc[i][c] * xc[i][c];
        f.sc = num / pairwise(sq, 0, n * 3);
        final double[] mr = Np.vm(mx, f.rm);
        f.tv = new double[] {my[0] - f.sc * mr[0], my[1] - f.sc * mr[1], my[2] - f.sc * mr[2]};
        return f;
    }

    /** np.mean(a, 0): rows added in order from 0, then divided. */
    static double[] mean0(double[][] a) {
        final double[] s = new double[3];
        for (double[] r : a) for (int c = 0; c < 3; c++) s[c] = s[c] + r[c];
        for (int c = 0; c < 3; c++) s[c] = s[c] / a.length;
        return s;
    }

    static double det3(double[][] m) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    /** vaprepose.to_pack: a file-space world (column form) -> pack form. */
    static double[][] toPack(double[][] w, Fit f) {
        final double[][] m = Fix.eye(4);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                double s = 0.0;
                for (int k = 0; k < 3; k++) s = Fma.fma(w[k][i], f.rm[k][j], s);
                m[i][j] = s;
            }
        final double[] t = Np.vm(new double[] {w[0][3], w[1][3], w[2][3]}, f.rm);
        for (int c = 0; c < 3; c++) m[3][c] = f.sc * t[c] + f.tv[c];
        return m;
    }

    // ================================================================================================ re-posing
    static final class Legs {
        final Pk pk;
        final double[][][] wp;
        final int[] dom;
        final Map<Integer, List<Integer>> kids = new HashMap<>();
        final Map<Long, double[][]> flips = new HashMap<>();

        Legs(Pk pk, double[][][] wp) {
            this.pk = pk;
            this.wp = wp;
            this.dom = pk.dom();
            for (int j = 0; j < pk.nn; j++) kids.computeIfAbsent(pk.parent[j], k -> new ArrayList<>()).add(j);
        }

        /** vap_stand.subtree: i and everything under it, sorted. */
        int[] subtree(int i) {
            final List<Integer> out = new ArrayList<>();
            final List<Integer> st = new ArrayList<>();
            st.add(i);
            while (!st.isEmpty()) {
                final int j = st.remove(st.size() - 1);
                out.add(j);
                final List<Integer> k = kids.get(j);
                if (k != null) st.addAll(k);
            }
            return out.stream().mapToInt(Integer::intValue).sorted().toArray();
        }

        double[] J(int j) {
            return new double[] {wp[j][3][0], wp[j][3][1], wp[j][3][2]};
        }

        void apply(int[] nodes, double[][] a) {
            for (int k : nodes) wp[k] = Np.mm(wp[k], a);
        }

        void straighten() {
            final int[] chest = subtree(18);
            final Set<Integer> cs = new HashSet<>();
            for (int k : chest) cs.add(k);
            final List<Integer> rl = new ArrayList<>();
            for (int k : subtree(1)) if (!cs.contains(k)) rl.add(k);
            final int[] rear = rl.stream().mapToInt(Integer::intValue).toArray();
            final double[] fr = horiz(sub(J(1), half(add(J(2), J(10)))));
            final double[] fc = horiz(sub(half(add(J(50), J(56))), J(18)));
            final double d = signedAngle(fc, fr);
            apply(chest, rotAbout(UP, d, J(18)));
            final double[] f = horiz(sub(half(add(J(50), J(56))), half(add(J(2), J(10)))));
            final double[] hl = sub(J(2), J(10));
            final double roll = Trig.atan2(hl[2], Py.hypot(hl[0], hl[1]));
            double bestDz = 0;
            double[][] best = null;
            for (int sgn : new int[] {1, -1}) {
                final double[][] a = rotAbout(f, sgn * roll, J(1));
                final double[][] a3 = {{a[0][0], a[0][1], a[0][2]}, {a[1][0], a[1][1], a[1][2]}, {a[2][0], a[2][1], a[2][2]}};
                final double dz = Math.abs(Np.vm(sub(J(2), J(10)), a3)[2]);
                if (best == null || dz < bestDz) {
                    bestDz = dz;
                    best = a;
                }
            }
            apply(rear, best);
        }

        double[] bodyFwd() {
            return horiz(sub(half(add(J(50), J(56))), half(add(J(2), J(10)))));
        }

        void head() {
            final double[] fb = bodyFwd();
            final double[] lh = horiz(sub(J(28), J(31)));
            final double[] fh = horiz(Np.cross(lh, UP));
            signedAngle(fb, fh);  // (turn0: the log's; its atan2 as the PC called it)
            final int[] head = subtree(20);
            final double[][][] bw = pk.bw;
            final double[] fhb = horiz(Np.cross(horiz(sub(row3(bw[28]), row3(bw[31]))), UP));
            final double yaw = signedAngle(fhb, fb);
            final double[][] ra = rotAbout(UP, yaw, new double[3]);
            final double[][] rt = Np.mm(m33(bw[20]), m33(ra));
            final double[][] w20 = m33(wp[20]);
            final double[][] dr = new double[3][3];  // wp[20][:3,:3].T @ Rt
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    double s = 0.0;
                    for (int k = 0; k < 3; k++) s = Fma.fma(w20[k][i], rt[k][j], s);
                    dr[i][j] = s;
                }
            final double[][] a = Fix.eye(4);
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) a[i][j] = dr[i][j];
            final double[] j20 = J(20), pr = Np.vm(j20, dr);
            for (int c = 0; c < 3; c++) a[3][c] = j20[c] - pr[c];
            apply(head, a);
        }

        void body() {
            final double g = signedAngle(bodyFwd(), FWD);
            final double[][] a = rotAbout(UP, g, new double[3]);
            final int[] all = new int[pk.nn];
            for (int k = 0; k < all.length; k++) all[k] = k;
            apply(all, a);
            final double[] mid = half(add(half(add(J(2), J(10))), half(add(J(50), J(56)))));
            final double[][] t = Fix.eye(4);
            t[3][0] = -mid[0];
            t[3][1] = -mid[1];
            apply(all, t);
        }

        // ---------------------------------------------------------------------------------------- legs
        void aim(int j, int c, double[] t0) {
            final double[] p = J(j);
            double[] d = sub(J(c), p);
            d = div(d, Np.norm(d));
            final double[] t = div(t0, Np.norm(t0));
            final double[] ax = Np.cross(d, t);
            final double s = Np.norm(ax);
            if (s < 1e-9) return;
            final double ang = Trig.atan2(s, Np.dot(d, t));
            apply(subtree(j), rotAbout(ax, ang, p));
        }

        void twist(int j, int c, int s, double[] t) {
            final double[] p = J(j);
            double[] ax = sub(J(c), p);
            ax = div(ax, Np.norm(ax));
            double[] v = sub(J(s), p);
            final double dv = Np.dot(v, ax);
            v = new double[] {v[0] - dv * ax[0], v[1] - dv * ax[1], v[2] - dv * ax[2]};
            final double dw = Np.dot(t, ax);
            double[] w = {t[0] - dw * ax[0], t[1] - dw * ax[1], t[2] - dw * ax[2]};
            if (Np.norm(v) < 1e-9 || Np.norm(w) < 1e-9) return;
            v = div(v, Np.norm(v));
            w = div(w, Np.norm(w));
            final double ang = Trig.atan2(Np.dot(Np.cross(v, w), ax), Np.dot(v, w));
            apply(subtree(j), rotAbout(ax, ang, p));
        }

        /** The signed axis permutation taking joint a's piece onto its mirror twin b's (closest points, both ways). */
        double[][] findFlip(int a, int b) {
            final long key = ((long) a << 32) | b;
            if (flips.containsKey(key)) return flips.get(key);
            final double[][] pa = local(a), pb = local(b);
            double[][] best = null;
            if (pa.length >= 4 && pb.length >= 4) {
                double be = 0;
                final int[][] perms = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
                final double[] dmin1 = new double[pa.length], dmin0 = new double[pb.length];
                for (int[] perm : perms) {
                    for (int sg = 0; sg < 8; sg++) {
                        final double[][] g = new double[3][3];
                        for (int r = 0; r < 3; r++) g[r][perm[r]] = ((sg >> (2 - r)) & 1) == 0 ? 1.0 : -1.0;
                        Arrays.fill(dmin1, Double.POSITIVE_INFINITY);
                        Arrays.fill(dmin0, Double.POSITIVE_INFINITY);
                        for (int i = 0; i < pa.length; i++) {
                            final double[] x = Np.vm(pa[i], g);
                            for (int j = 0; j < pb.length; j++) {
                                final double dx = x[0] - pb[j][0], dy = x[1] - pb[j][1], dz = x[2] - pb[j][2];
                                final double dd = Math.sqrt(dx * dx + dy * dy + dz * dz);
                                if (dd < dmin1[i]) dmin1[i] = dd;
                                if (dd < dmin0[j]) dmin0[j] = dd;
                            }
                        }
                        double s1 = 0, s0 = 0;
                        for (double v : dmin1) s1 += v;
                        for (double v : dmin0) s0 += v;
                        final double e = (s1 / pa.length + s0 / pb.length) / 2;
                        if (best == null || e < be) {
                            be = e;
                            best = g;
                        }
                    }
                }
            }
            flips.put(key, best);
            return best;
        }

        double[][] local(int j) {
            final List<double[]> out = new ArrayList<>();
            final double[][] r = m33(pk.bw[j]);
            final double[] t = row3(pk.bw[j]);
            for (int v = 0; v < pk.P.length; v++) {
                if (dom[v] != j) continue;
                final double[] d = sub(pk.P[v], t);
                final double[] o = new double[3];
                for (int c = 0; c < 3; c++) o[c] = d[0] * r[c][0] + d[1] * r[c][1] + d[2] * r[c][2];
                out.add(o);
            }
            return out.toArray(new double[0][]);
        }

        /** vap_stand.mirror_leg: dst leg = src leg mirrored across x = the hips' middle. */
        void mirrorLeg(int src, int dst) {
            final int[] S = subtree(src), D = subtree(dst);
            if (S.length != D.length) throw new IllegalStateException("legs differ");
            final double xm = (wp[2][3][0] + wp[10][3][0]) / 2;
            final double[][] h = {{-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
            final Map<Integer, double[][]> G = new HashMap<>();
            for (int k = 0; k < S.length; k++) {
                final double[][] g = findFlip(S[k], D[k]);
                if (g != null) G.put(S[k], g);
            }
            for (int k = 0; k < S.length; k++) {
                int j = S[k];
                while (!G.containsKey(j)) j = pk.parent[j];
                final double[][] gr = G.get(j);
                final double[][] grT = new double[3][3];
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) grT[r][c] = gr[c][r];
                final double[][] r3 = Np.mm(Np.mm(grT, m33(wp[S[k]])), h);
                final double[][] m = Fix.eye(4);
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) m[r][c] = r3[r][c];
                final double[] p = J(S[k]);
                p[0] = 2 * xm - p[0];
                m[3][0] = p[0];
                m[3][1] = p[1];
                m[3][2] = p[2];
                wp[D[k]] = m;
            }
        }

        double[][][] keep;

        void poseLeg(int side, boolean front, double a) {
            if (front) {
                final int[] f = FRONT[side];
                final double[] ls = {side == 0 ? 1.0 : -1.0, 0.0, 0.0};
                aim(f[0], f[1], legDir(a, -1, ls));
                aim(f[1], f[2], legDir(a, 1, ls));
                aim(f[2], f[3], new double[] {-0.0, -0.0, -1.0});
                twist(f[2], f[3], f[4], FWD);
            } else {
                final int hp = HIND[0][0], ts = HIND[0][4];
                for (int k : subtree(hp)) wp[k] = copy(keep[k]);
                final double a2 = a - Trig.radians(HIND_TILT);
                aim(hp, ts, legDir(a2, 1, new double[] {1.0, 0.0, 0.0}));
                mirrorLeg(hp, HIND[1][0]);
            }
        }

        /** -U cos a (+|-) F sin a + Ls splay, as numpy works it. */
        double[] legDir(double a, int sign, double[] ls) {
            final double ca = Trig.cos(a), sa = Trig.sin(a);
            final double[] nu = {-0.0 * ca, -0.0 * ca, -1.0 * ca};
            final double[] fs = {0.0 * sa, -1.0 * sa, 0.0 * sa};
            final double[] r = new double[3];
            for (int c = 0; c < 3; c++) r[c] = (sign < 0 ? nu[c] - fs[c] : nu[c] + fs[c]) + ls[c] * SPLAY;
            return r;
        }

        double lowest(int top) {
            final double[] z = pk.skinZ(wp);
            final Set<Integer> st = new HashSet<>();
            for (int k : subtree(top)) st.add(k);
            double lo = Double.POSITIVE_INFINITY;
            for (int v = 0; v < z.length; v++) if (st.contains(dom[v]) && z[v] < lo) lo = z[v];
            return lo;
        }

        void reset(int top) {
            for (int k : subtree(top)) wp[k] = copy(keep[k]);
        }

        void standLegs() {
            keep = new double[pk.nn][][];
            for (int k = 0; k < pk.nn; k++) keep[k] = copy(wp[k]);
            final int[] sides = {0, 1, 0};
            final boolean[] fronts = {true, true, false};
            final int[] tops = {FRONT[0][0], FRONT[1][0], HIND[0][0]};
            final double[] straight = new double[3];
            for (int l = 0; l < 3; l++) {
                poseLeg(sides[l], fronts[l], 0.0);
                straight[l] = lowest(tops[l]);
                reset(tops[l]);
            }
            double floor = straight[0];
            for (int l = 1; l < 3; l++) if (straight[l] > floor) floor = straight[l];
            for (int l = 0; l < 3; l++) {
                double lo = 0.0, hi = Trig.radians(75);
                for (int it = 0; it < 30; it++) {
                    final double mid = (lo + hi) / 2;
                    reset(tops[l]);
                    poseLeg(sides[l], fronts[l], mid);
                    if (lowest(tops[l]) < floor + (fronts[l] ? FRONT_LIFT : 0.0)) lo = mid;
                    else hi = mid;
                }
                reset(tops[l]);
                poseLeg(sides[l], fronts[l], lo);
            }
            mirrorLeg(HIND[0][0], HIND[1][0]);
        }
    }

    /** vap_stand.rot_about: 4 x 4 row form, ang (right hand about axis) about pivot. */
    static double[][] rotAbout(double[] axis, double ang, double[] pivot) {
        final double na = Np.norm(axis);
        final double[] a = {axis[0] / na, axis[1] / na, axis[2] / na};
        final double c = Trig.cos(ang), s = Trig.sin(ang);
        final double[][] k = {{0.0, -a[2], a[1]}, {a[2], 0.0, -a[0]}, {-a[1], a[0], 0.0}};
        final double[][] kk = Np.mm(k, k);
        final double[][] rc = new double[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) rc[i][j] = ((i == j ? 1.0 : 0.0) + s * k[i][j]) + (1 - c) * kk[i][j];
        final double[][] m = Fix.eye(4);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m[i][j] = rc[j][i];
        final double[] pr = Np.vmT(pivot, rc);
        for (int i = 0; i < 3; i++) m[3][i] = pivot[i] - pr[i];
        return m;
    }

    static double[] horiz(double[] v0) {
        final double[] v = {v0[0], v0[1], 0.0};
        double n = Np.norm(v);
        if (n == 0.0) n = 1.0;
        return div(v, n);
    }

    static double signedAngle(double[] a, double[] b) {
        return Trig.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]);
    }

    static double[] add(double[] a, double[] b) {
        return new double[] {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
    }

    static double[] sub(double[] a, double[] b) {
        return new double[] {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    }

    static double[] half(double[] a) {
        return new double[] {a[0] / 2, a[1] / 2, a[2] / 2};
    }

    static double[] div(double[] a, double d) {
        return new double[] {a[0] / d, a[1] / d, a[2] / d};
    }

    static double[] row3(double[][] m) {
        return new double[] {m[3][0], m[3][1], m[3][2]};
    }

    static double[][] m33(double[][] m) {
        return new double[][] {{m[0][0], m[0][1], m[0][2]}, {m[1][0], m[1][1], m[1][2]}, {m[2][0], m[2][1], m[2][2]}};
    }

    static double[][] copy(double[][] m) {
        return Fix.copy(m);
    }

    static int[] range(int a, int b) {
        final int[] r = new int[b - a];
        for (int i = a; i < b; i++) r[i - a] = i;
        return r;
    }

    // ================================================================================================ the map
    /** vap_applymap.py: the map's hashes on the nodes (a quad's arm keys -> the other side's legs), flaps, mode. */
    static byte[] applyMap(byte[] in, Models.Spec s) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = Bin.le32(b, 8);
        final MeleePack.BoneMap bm = s.map;
        final boolean quad = "quad".equals(bm.cls);
        final Map<String, String> trot = new HashMap<>();
        if (quad) {
            final String[][] pairs = {{"upperarm", "thigh"}, {"forearm", "calf"}, {"hand", "foot"}};
            for (String[] p : pairs) {
                trot.put("bip01 l " + p[0], "bip01 r " + p[1]);
                trot.put("bip01 r " + p[0], "bip01 l " + p[1]);
            }
        }
        final Map<Integer, Integer> mapping = new LinkedHashMap<>();
        for (int k = 0; k < bm.bones.size(); k++) {
            final String bip = bm.bones.get(k);
            final int j = bm.joints.get(k);
            if (j < 0 || j >= nn) throw new IOException(bip + " -> " + j + ": no such node");
            if (mapping.containsKey(j)) throw new IOException("node " + j + " mapped twice");
            mapping.put(j, Py.lowerHash(trot.getOrDefault(bip, bip)));
        }
        for (int side = 0; side < 2; side++)
            for (int j : side == 0 ? bm.flapL : bm.flapR)
                if (j >= 0 && j < nn && !mapping.containsKey(j)) mapping.put(j, Py.lowerHash("guest flap " + (side == 0 ? "l" : "r")));
        final int mode = 1 | ("fly".equals(bm.cls) ? 2 : "float".equals(bm.cls) ? 4 : 0);
        final int swirl = Py.lowerHash("guest swirl");
        for (int i = 0; i < nn; i++) {
            final int o = 64 + 156 * i;
            final int old = Bin.le32(b, o + 88);
            Fix.putI(b, o + 88, mapping.containsKey(i) ? mapping.get(i) : old == swirl ? swirl : 0);
        }
        Fix.putI(b, 60, mode);
        if (s.hover != null && Py.num(s.hover) != 0.0) throw new IOException("stand: _hover not handled");
        return b;
    }

    // ================================================================================================ the model
    /** vapanim.Model: Colosseum's joints (meleepack's walk order), rest SRTs, binds, and its animations. */
    static final class Model {
        final Hsd d;
        final int root;
        final List<Integer> anims = new ArrayList<>(), joints = new ArrayList<>(), parent = new ArrayList<>();
        final List<double[][]> rest = new ArrayList<>();  // {s, r, t}
        final List<double[][]> bind = new ArrayList<>();
        final Map<Integer, Integer> index = new HashMap<>();

        Model(Colo.Dat dat) throws IOException {
            d = new Hsd(dat.dat);
            root = dat.root;
            final int scene = d.root("scene_data");
            final int modelset = d.u32(d.u32(scene));
            final int an = d.u32(modelset + 0x04);
            for (int i = 0; an != 0 && i < 64; i++) {
                final int a = d.u32(an + 4 * i);
                if (a == 0) break;
                anims.add(a);
            }
            walk(root, -1, M4.ident());
            for (int k = 0; k < joints.size(); k++) index.put(joints.get(k), k);
        }

        private void walk(int j, int par, double[][] pw) {
            while (j != 0) {
                final double[] s = d.f32(j + 0x20, 3), r = d.f32(j + 0x14, 3), t = d.f32(j + 0x2C, 3);
                double[][] w = M4.mul(pw, M4.srt(s, r, t));
                final int ib = d.u32(j + 0x38);
                if (ib != 0) {
                    final double[] m = d.f32(ib, 12);
                    w = M4.inv(new double[][] {{m[0], m[1], m[2], m[3]}, {m[4], m[5], m[6], m[7]}, {m[8], m[9], m[10], m[11]},
                        {0.0, 0.0, 0.0, 1.0}});
                }
                final int k = joints.size();
                joints.add(j);
                parent.add(par);
                rest.add(new double[][] {s, r, t});
                bind.add(w);
                final int fl = d.u32(j + 0x04), ch = d.u32(j + 0x08);
                if (ch != 0 && (fl & 0x1000) == 0) walk(ch, k, w);
                j = d.u32(j + 0x0C);
            }
        }

        /** {joint index: {track kind: fobj}} of animation ai. */
        Map<Integer, Map<Integer, Integer>> animTracks(int ai) {
            final Map<Integer, Map<Integer, Integer>> out = new HashMap<>();
            tracksWalk(root, anims.get(ai), out);
            return out;
        }

        private void tracksWalk(int j, int a, Map<Integer, Map<Integer, Integer>> out) {
            while (j != 0) {
                final int k = index.get(j);
                if (a != 0) {
                    final int aobj = d.u32(a + 0x08);
                    if (aobj != 0) {
                        int f = d.u32(aobj + 0x08);
                        while (f != 0) {
                            out.computeIfAbsent(k, x -> new LinkedHashMap<>()).put(d.u8(f + 0x0C), f);
                            f = d.u32(f);
                        }
                    }
                }
                final int fl = d.u32(j + 0x04), ch = d.u32(j + 0x08);
                if (ch != 0 && (fl & 0x1000) == 0) tracksWalk(ch, a != 0 ? d.u32(a) : 0, out);
                j = d.u32(j + 0x0C);
                a = a != 0 ? d.u32(a + 0x04) : 0;
            }
        }

        /** vapanim.keys: [(op, frame, value, slope)], keys little-endian (Colosseum's). */
        List<double[]> keys(int fobj) {
            final int ln = d.u32(fobj + 0x04);
            final double start = d.f32(fobj + 0x08);
            final int vf = d.u8(fobj + 0x0D), sf = d.u8(fobj + 0x0E);
            final int[] p = {d.u32(fobj + 0x10)};
            final int end = p[0] + ln;
            final List<double[]> out = new ArrayList<>();
            double clock = start;
            while (p[0] < end) {
                int hb = d.u8(p[0]++);
                final int op = hb & 0x0F;
                int n = (hb >> 4) & 7;
                int sh = 3;
                while ((hb & 0x80) != 0) {
                    hb = d.u8(p[0]++);
                    n |= (hb & 0x7F) << sh;
                    sh += 7;
                }
                n += 1;
                if (op == 0) break;
                for (int i = 0; i < n; i++) {
                    double val = 0.0, slope = 0.0;
                    int wait = 0;
                    if (op == 1 || op == 2 || op == 3) {
                        val = num(vf, p);
                        wait = packed(p);
                    } else if (op == 4) {
                        val = num(vf, p);
                        slope = num(sf, p);
                        wait = packed(p);
                    } else if (op == 5) {
                        slope = num(sf, p);
                    } else if (op == 6) {
                        val = num(vf, p);
                    }
                    out.add(new double[] {op, clock, val, slope});
                    clock += wait;
                }
            }
            return out;
        }

        private int packed(int[] p) {
            int v = d.u8(p[0]++);
            int val = v & 0x7F, sh = 7;
            while ((v & 0x80) != 0) {
                v = d.u8(p[0]++);
                val |= (v & 0x7F) << sh;
                sh += 7;
            }
            return val;
        }

        private double num(int fmt, int[] p) {
            final int kind = fmt & 0xE0;
            final double scale = (double) (1L << (fmt & 0x1F));
            final byte[] b = d.b;
            final int o = Hsd.D + p[0];
            if (kind == 0x00) {
                p[0] += 4;
                return Bin.lef32(b, o);
            }
            final int v;
            if (kind == 0x20) {
                v = (short) Bin.le16(b, o);
                p[0] += 2;
            } else if (kind == 0x40) {
                v = Bin.le16(b, o);
                p[0] += 2;
            } else if (kind == 0x60) {
                v = b[o];
                p[0] += 1;
            } else {
                v = b[o] & 0xFF;
                p[0] += 1;
            }
            return v / scale;
        }

        /** vapanim.Model.value: the HSD FObj player. */
        static double value(List<double[]> keys, double frame) {
            double p0 = 0, p1 = 0, d0 = 0, d1 = 0, t0 = 0, t1 = 0;
            int opi = 1, op = 1;
            for (double[] k : keys) {
                if ((int) k[0] != 5) {
                    if (frame <= k[1]) return k[2];
                    break;
                }
            }
            for (double[] k : keys) {
                final int kop = (int) k[0];
                final double kf = k[1], kv = k[2], ks = k[3];
                opi = op;
                op = kop;
                if (op == 1 || op == 2) {
                    p0 = p1;
                    p1 = kv;
                    if (opi != 5) {
                        d0 = d1;
                        d1 = 0.0;
                    }
                    t0 = t1;
                    t1 = kf;
                } else if (op == 3) {
                    p0 = p1;
                    d0 = d1;
                    p1 = kv;
                    d1 = 0.0;
                    t0 = t1;
                    t1 = kf;
                } else if (op == 4) {
                    p0 = p1;
                    p1 = kv;
                    d0 = d1;
                    d1 = ks;
                    t0 = t1;
                    t1 = kf;
                } else if (op == 5) {
                    d0 = d1;
                    d1 = ks;
                } else if (op == 6) {
                    p0 = kv;
                    p1 = kv;
                }
                if (t1 > frame && kop != 5) break;
                opi = kop;
            }
            if (t0 == t1 || opi == 1 || opi == 6) return p0;
            final double fd = frame - t0;
            final double w = fd / (t1 - t0);
            if (opi == 2) return p0 + (p1 - p0) * w;
            if (opi == 3 || opi == 4 || opi == 5) {
                final double dt = t1 - t0;
                final double w2 = w * w, w3 = w * w * w;
                return p0 * (2 * w3 - 3 * w2 + 1) + p1 * (-2 * w3 + 3 * w2) + d0 * dt * (w3 - 2 * w2 + w) + d1 * dt * (w3 - w2);
            }
            return p0;
        }

        /** vapanim.Model.pose: every joint's world (column form, Y up) of animation ai at frame. */
        List<double[][]> pose(int ai, double frame, Map<Integer, Map<Integer, Integer>> tracks) {
            final List<double[][]> world = new ArrayList<>();
            for (int k = 0; k < joints.size(); k++) {
                final double[] s = rest.get(k)[0].clone(), r = rest.get(k)[1].clone(), t = rest.get(k)[2].clone();
                final Map<Integer, Integer> tr = tracks.get(k);
                if (tr != null) {
                    for (Map.Entry<Integer, Integer> e : tr.entrySet()) {
                        final List<double[]> ks = keys(e.getValue());
                        if (ks.isEmpty()) continue;
                        final double v = value(ks, frame);
                        final int kind = e.getKey();
                        if (1 <= kind && kind <= 3) r[kind - 1] = v;
                        else if (5 <= kind && kind <= 7) t[kind - 5] = v;
                        else if (8 <= kind && kind <= 10) s[kind - 8] = v;
                    }
                }
                final double[][] m = M4.srt(s, r, t);
                final int par = parent.get(k);
                world.add(par >= 0 ? M4.mul(world.get(par), m) : m);
            }
            return world;
        }
    }
}
