package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * r127 / r130: what the PC did to some Pokemon packs after colo_guests.py built them, done the same way here (the
 * tools are outside\colo-maps\..., tools\outside\gpk_reparent.py; float64 sums as numpy does them, Np):
 *  - reparent: nodes hung on another parent, their rest kept (gpk_reparent.py: Aggron's feet on its legs, Flaaffy's
 *    wool / ears on its face, Jynx, Hitmonchan);
 *  - repose: parts of the bind turned (gpk_repose.py: Slaking's lean, its right leg mirrored from the left);
 *  - rebind --flat: nodes re-seated over their parent (gpk_rebind.py: Vigoroth's feet);
 *  - hariyama: its knees moved into the legs and the leg weights shared with the hip carriers (hariyama_fix.py).
 */
final class Fix {
    private Fix() {}

    static final int HDR = 64, NODE = 156;

    static double[][] mat(byte[] b, int off) {
        final double[][] m = new double[4][4];
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) m[r][c] = Bin.lef32(b, off + 4 * (r * 4 + c));
        return m;
    }

    static void putMat(byte[] b, int off, double[][] m) {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) putF(b, off + 4 * (r * 4 + c), m[r][c]);
    }

    static void putF(byte[] b, int off, double v) {
        putI(b, off, Float.floatToRawIntBits((float) v));
    }

    static void putI(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
        b[off + 2] = (byte) (v >> 16);
        b[off + 3] = (byte) (v >> 24);
    }

    static int nodes(byte[] b) {
        return Bin.le32(b, 8);
    }

    static int parent(byte[] b, int i) {
        return Bin.le32(b, HDR + NODE * i + 4);
    }

    static double[][] bindWorld(byte[] b, int i) {
        return mat(b, HDR + NODE * i + 92);
    }

    static int[] parents(byte[] b) {
        final int nn = nodes(b);
        final int[] p = new int[nn];
        for (int i = 0; i < nn; i++) p[i] = parent(b, i);
        return p;
    }

    /** Each packet's vertex block: {offset, count}. */
    static List<int[]> vertexBlocks(byte[] b) {
        final int nn = nodes(b), np = Bin.le32(b, 12), nt = Bin.le32(b, 16);
        int p = HDR + NODE * nn;
        for (int i = 0; i < nt; i++) p += 8 + ((Bin.le32(b, p + 4) + 3) & ~3);
        final List<int[]> out = new ArrayList<>();
        for (int i = 0; i < np; i++) {
            final int nv = Bin.le32(b, p + 4) & 0x0FFFFFFF, ni = Bin.le32(b, p + 8);
            p += 12;
            out.add(new int[] {p, nv});
            p += 52 * nv + ((ni * 2 + 3) & ~3);
        }
        return out;
    }

    static double[] row3(double[][] m) {
        return new double[] {m[3][0], m[3][1], m[3][2]};
    }

    static double[][] copy(double[][] m) {
        final double[][] r = new double[m.length][];
        for (int i = 0; i < m.length; i++) r[i] = m[i].clone();
        return r;
    }

    static double[][] eye(int n) {
        final double[][] r = new double[n][n];
        for (int i = 0; i < n; i++) r[i][i] = 1.0;
        return r;
    }

    /** gpk_repose.subtree: r and every later node under it, in index order. */
    static List<Integer> subtree(int[] par, int r) {
        final List<Integer> s = new ArrayList<>();
        final Set<Integer> in = new HashSet<>();
        s.add(r);
        in.add(r);
        for (int i = r + 1; i < par.length; i++) {
            if (in.contains(par[i])) {
                s.add(i);
                in.add(i);
            }
        }
        return s;
    }

    /**
     * gpk_repose.turn: the least rotation taking direction a onto b, in column form (R = its transpose, row form; numpy
     * keeps it a transposed view, which changes how p @ R is summed: about). null = no turn (np.eye).
     */
    static double[][] turnCol(double[] a0, double[] b0) {
        final double na = Np.norm(a0), nb = Np.norm(b0);
        final double[] a = {a0[0] / na, a0[1] / na, a0[2] / na}, b = {b0[0] / nb, b0[1] / nb, b0[2] / nb};
        final double[] v = Np.cross(a, b);
        final double c = Np.dot(a, b);
        if (Np.norm(v) < 1e-9) return null;
        final double[][] k = {{0.0, -v[2], v[1]}, {v[2], 0.0, -v[0]}, {-v[1], v[0], 0.0}};
        final double[][] kk = Np.mm(k, k);
        final double d = 1.0 + c;
        final double[][] col = new double[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) col[i][j] = ((i == j ? 1.0 : 0.0) + k[i][j]) + kk[i][j] / d;
        return col;
    }

    /** gpk_repose.about: the turn (column form from turnCol; null none) about the point p, 4 x 4 row form. */
    static double[][] about(double[][] col, double[] p) {
        final double[][] t = eye(4);
        if (col == null) {  // np.eye(3): p - p @ I
            final double[] pr = Np.vm(p, eye(3));
            for (int k = 0; k < 3; k++) t[3][k] = p[k] - pr[k];
            return t;
        }
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i][j] = col[j][i];
        final double[] pr = Np.vmT(p, col);
        for (int k = 0; k < 3; k++) t[3][k] = p[k] - pr[k];
        return t;
    }

    /** np.allclose(a, b) (rtol 1e-5, atol 1e-8). */
    static boolean allclose(double[][] a, double[][] b) {
        for (int i = 0; i < a.length; i++)
            for (int j = 0; j < a[i].length; j++)
                if (!(Math.abs(a[i][j] - b[i][j]) <= 1e-8 + 1e-5 * Math.abs(b[i][j]))) return false;
        return true;
    }

    static double[] mean3(List<double[]> rows) {
        final double[] s = rows.get(0).clone();
        for (int i = 1; i < rows.size(); i++) for (int k = 0; k < 3; k++) s[k] = s[k] + rows.get(i)[k];
        for (int k = 0; k < 3; k++) s[k] = s[k] / rows.size();
        return s;
    }

    /** A vertex's place and normal moved by its weights' deltas (gpk_repose / gpk_rebind), written back. */
    private static void writeVertex(byte[] b, int q, double[] npos, double[] nnrm) {
        double ln = Math.sqrt(Np.dot(new double[] {nnrm[0], nnrm[1], nnrm[2]}, new double[] {nnrm[0], nnrm[1], nnrm[2]}));
        if (ln == 0.0) ln = 1.0;
        for (int k = 0; k < 3; k++) putF(b, q + 4 * k, npos[k]);
        for (int k = 0; k < 3; k++) putF(b, q + 12 + 4 * k, nnrm[k] / ln);
    }

    private static double[] pos4(byte[] b, int q) {
        return new double[] {Bin.lef32(b, q), Bin.lef32(b, q + 4), Bin.lef32(b, q + 8), 1.0};
    }

    private static double[] nrm4(byte[] b, int q) {
        return new double[] {Bin.lef32(b, q + 12), Bin.lef32(b, q + 16), Bin.lef32(b, q + 20), 0.0};
    }

    /** npos += s * v (numpy: the product rounded, then the sum). */
    private static void addScaled(double[] acc, double s, double[] v) {
        for (int k = 0; k < 4; k++) acc[k] = acc[k] + s * v[k];
    }

    /**
     * gpk_repose.py: "mirror:<src root>:<dst root>:<centre x>" (each joint of the dst limb turned so its bone points
     * as the mirrored src bone does) and "unlean:<pivot>:<tip>:<subtree root>" (no sideways lean of pivot -> tip);
     * skin and normals follow.
     */
    static byte[] repose(byte[] in, String[] ops) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        final int[] par = parents(b);
        final double[][][] bw0 = new double[nn][][], cur = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            bw0[i] = bindWorld(b, i);
            cur[i] = copy(bw0[i]);
        }
        for (String op : ops) {
            final String[] a = op.split(":");
            if (a[0].equals("mirror")) {
                final int src = Integer.parseInt(a[1]), dst = Integer.parseInt(a[2]);
                final double cx = Double.parseDouble(a[3]);
                final List<Integer> S = subtree(par, src), D = subtree(par, dst);
                if (S.size() != D.size()) throw new IOException("repose: limbs differ");
                for (int k = 0; k < S.size(); k++)
                    if (S.contains(par[S.get(k)]) != D.contains(par[D.get(k)])) throw new IOException("repose: limbs differ");
                final Map<Integer, Integer> m = new HashMap<>();
                for (int k = 0; k < D.size(); k++) m.put(D.get(k), S.get(k));
                for (int d : D) {
                    final List<Integer> kids = new ArrayList<>();
                    for (int k : D) if (par[k] == d) kids.add(k);
                    if (kids.isEmpty()) continue;
                    final List<double[]> nowL = new ArrayList<>(), wantL = new ArrayList<>();
                    for (int k : kids) {
                        nowL.add(row3(cur[k]));
                        wantL.add(mirror(row3(cur[m.get(k)]), cx));
                    }
                    final double[] nm = mean3(nowL), wm = mean3(wantL), cd = row3(cur[d]), md = mirror(row3(cur[m.get(d)]), cx);
                    final double[] now = {nm[0] - cd[0], nm[1] - cd[1], nm[2] - cd[2]};
                    final double[] want = {wm[0] - md[0], wm[1] - md[1], wm[2] - md[2]};
                    final double[][] T = about(turnCol(now, want), cd);
                    for (int j : subtree(par, d)) cur[j] = Np.mm(cur[j], T);
                }
            } else if (a[0].equals("unlean")) {
                final int pv = Integer.parseInt(a[1]), tip = Integer.parseInt(a[2]), rt = Integer.parseInt(a[3]);
                final double[] t = row3(cur[tip]), p = row3(cur[pv]);
                final double[] now = {t[0] - p[0], t[1] - p[1], t[2] - p[2]};
                final double[] want = {0.0, now[1], Math.sqrt(now[0] * now[0] + now[2] * now[2])};
                final double[][] T = about(turnCol(now, want), p);
                for (int j : subtree(par, rt)) cur[j] = Np.mm(cur[j], T);
            } else {
                throw new IOException("repose: unknown op " + op);
            }
        }
        final Map<Integer, double[][]> delta = new TreeMap<>();
        for (int j = 0; j < nn; j++) if (!allclose(cur[j], bw0[j])) delta.put(j, Np.mm(Np.inv(bw0[j]), cur[j]));
        for (int d : delta.keySet()) {
            final int off = HDR + NODE * d;
            putMat(b, off + 92, cur[d]);
            putMat(b, off + 24, Np.inv(cur[d]));
            if (par[d] >= 0) putRow3(b, off + 8, Np.mm(cur[d], Np.inv(cur[par[d]])));
        }
        for (int i = 0; i < nn; i++)
            if (par[i] >= 0 && delta.containsKey(par[i]) && !delta.containsKey(i))
                putRow3(b, HDR + NODE * i + 8, Np.mm(cur[i], Np.inv(cur[par[i]])));
        final double[][] I4 = eye(4);
        for (int[] blk : vertexBlocks(b)) {
            for (int v = 0; v < blk[1]; v++) {
                final int q = blk[0] + 52 * v;
                final int[] js = new int[4];
                final double[] ws = new double[4];
                int n = 0;
                boolean any = false;
                for (int c = 0; c < 4; c++) {
                    final double w = Bin.lef32(b, q + 36 + 4 * c);
                    if (w > 0) {
                        js[n] = b[q + 32 + c] & 0xFF;
                        ws[n++] = w;
                        if (delta.containsKey(js[n - 1])) any = true;
                    }
                }
                if (!any) continue;
                double tw = Py.sum(ws, n);
                if (tw == 0.0) tw = 1.0;
                final double[] pos = pos4(b, q), nrm = nrm4(b, q), npos = new double[4], nnrm = new double[4];
                for (int k = 0; k < n; k++) {
                    final double[][] dl = delta.getOrDefault(js[k], I4);
                    addScaled(npos, ws[k] / tw, Np.vm(pos, dl));
                    addScaled(nnrm, ws[k] / tw, Np.vm(nrm, dl));
                }
                writeVertex(b, q, npos, nnrm);
            }
        }
        return b;
    }

    private static double[] mirror(double[] p, double cx) {
        return new double[] {2 * cx - p[0], p[1], p[2]};
    }

    private static void putRow3(byte[] b, int off, double[][] m) {
        for (int k = 0; k < 3; k++) putF(b, off + 4 * k, m[3][k]);
    }

    /**
     * gpk_rebind.py --flat: each node keeps its bind's own (flat) piece, turned deg about the vertical, at its parent's
     * place (x, y; its own height); the vertices weighted to it move along (blends by weight), normals too.
     */
    static byte[] rebindFlat(byte[] in, double deg, int[] fix) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        final int[] par = parents(b);
        final double[][][] bw = new double[nn][][], nw = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            bw[i] = bindWorld(b, i);
            nw[i] = copy(bw[i]);
        }
        final Set<Integer> fixed = new HashSet<>();
        for (int n : fix) fixed.add(n);
        final double a = Trig.radians(deg), ca = Trig.cos(a), sa = Trig.sin(a);
        final double[][] rz = {{ca, sa, 0.0}, {-sa, ca, 0.0}, {0.0, 0.0, 1.0}};
        for (int n : fix) {
            if (n <= 0 || n >= nn) throw new IOException("rebind node " + n);
            final int p = par[n];
            final double[][] r3 = new double[3][3];
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r3[i][j] = bw[n][i][j];
            final double[][] t = Np.mm(r3, rz);
            nw[n] = copy(bw[n]);
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) nw[n][i][j] = t[i][j];
            nw[n][3][0] = nw[p][3][0];
            nw[n][3][1] = nw[p][3][1];
            nw[n][3][2] = bw[n][3][2];
        }
        final Map<Integer, double[][]> delta = new HashMap<>();
        for (int n : fix) delta.put(n, Np.mm(Np.inv(bw[n]), nw[n]));
        for (int n : fix) {
            final int off = HDR + NODE * n;
            putMat(b, off + 92, nw[n]);
            putMat(b, off + 24, Np.inv(nw[n]));
            putRow3(b, off + 8, Np.mm(nw[n], Np.inv(nw[par[n]])));
        }
        for (int i = 0; i < nn; i++)
            if (par[i] >= 0 && fixed.contains(par[i]) && !fixed.contains(i))
                putRow3(b, HDR + NODE * i + 8, Np.mm(bw[i], Np.inv(nw[par[i]])));
        for (int[] blk : vertexBlocks(b)) {
            for (int v = 0; v < blk[1]; v++) {
                final int q = blk[0] + 52 * v;
                final int[] js = new int[4];
                final double[] ws = new double[4], all = new double[4];
                int n = 0, na = 0;
                for (int c = 0; c < 4; c++) {
                    final double w = Bin.lef32(b, q + 36 + 4 * c);
                    final int j = b[q + 32 + c] & 0xFF;
                    if (w > 0) {
                        all[na++] = w;
                        if (delta.containsKey(j)) {
                            js[n] = j;
                            ws[n++] = w;
                        }
                    }
                }
                if (n == 0) continue;
                double tw = Py.sum(all, na);
                if (tw == 0.0) tw = 1.0;
                final double[] pos = pos4(b, q), nrm = nrm4(b, q), npos = new double[4], nnrm = new double[4];
                double restW = tw;
                for (int k = 0; k < n; k++) {
                    final double[][] dl = delta.get(js[k]);
                    addScaled(npos, ws[k] / tw, Np.vm(pos, dl));
                    addScaled(nnrm, ws[k] / tw, Np.vm(nrm, dl));
                    restW -= ws[k];
                }
                addScaled(npos, restW / tw, pos);
                addScaled(nnrm, restW / tw, nrm);
                writeVertex(b, q, npos, nnrm);
            }
        }
        return b;
    }

    // ------------------------------------------------------------------------------------------------ Hariyama
    private static final double KNEE_Z = 0.32, ALPHA_FOOT = 0.40, ALPHA_TOP = 0.30, ALPHA_Z = 0.40, ALPHA_HALF = 0.20;
    private static final int[][] LEGS = {{5, 6, 4}, {16, 17, 15}};  // thigh bulb, knee (moved), hip carrier
    private static final double[] LEG_X = {0.363, -0.363};

    /** hariyama_fix.py: knees 6 / 17 into the legs' centre line, leg weights shared with the hip carriers 4 / 15. */
    static byte[] hariyama(byte[] in) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final String name = Bin.cstr(b, 28, 32);
        final int nn = nodes(b);
        if (!name.startsWith("pk_hariyama") || nn != 97) throw new IOException("not Colosseum's Hariyama pack");
        final int[] par = parents(b);
        final double[][][] bw = new double[nn][][];
        for (int i = 0; i < nn; i++) bw[i] = bindWorld(b, i);
        for (int l = 0; l < 2; l++) {
            final double[] kp = row3(bw[LEGS[l][1]]);
            if (Math.abs(kp[1]) < 0.05) throw new IOException("Hariyama: already fixed");
            if (!(-0.40 < kp[1] && kp[1] < -0.30 && Math.abs(Math.abs(kp[0]) - 0.363) < 0.02))
                throw new IOException("Hariyama: unexpected knee " + LEGS[l][1]);
        }
        final List<int[]> blocks = vertexBlocks(b);
        for (int l = 0; l < 2; l++) {
            final int knee = LEGS[l][1], thigh = LEGS[l][0], carrier = LEGS[l][2];
            bw[knee] = copy(bw[knee]);
            bw[knee][3][0] = LEG_X[l];
            bw[knee][3][1] = 0.0;
            bw[knee][3][2] = KNEE_Z;
            writeNode(b, bw, par, knee);
            for (int k = 0; k < nn; k++) if (par[k] == knee) writeNode(b, bw, par, k);
            final Set<Integer> mov = new HashSet<>();
            for (int i = 0; i < nn; i++) {
                if (i == thigh) {
                    mov.add(i);
                    continue;
                }
                for (int p = par[i]; p >= 0; p = par[p]) {
                    if (p == thigh) {
                        mov.add(i);
                        break;
                    }
                }
            }
            for (int[] blk : blocks) {
                for (int v = 0; v < blk[1]; v++) {
                    final int o = blk[0] + 52 * v;
                    final double z = Bin.lef32(b, o + 8);
                    final int[] js = new int[4];
                    final double[] ws = new double[4], legs = new double[4];
                    int nl = 0;
                    for (int c = 0; c < 4; c++) {
                        js[c] = b[o + 32 + c] & 0xFF;
                        ws[c] = Bin.lef32(b, o + 36 + 4 * c);
                        if (ws[c] > 0 && mov.contains(js[c])) legs[nl++] = ws[c];
                    }
                    final double legW = Py.sum(legs, nl);
                    if (legW <= 0) continue;
                    final double a = ALPHA_FOOT + (ALPHA_TOP - ALPHA_FOOT) * ramp(z, ALPHA_Z, ALPHA_HALF);
                    final List<Integer> keys = new ArrayList<>();
                    final Map<Integer, Double> acc = new HashMap<>();
                    for (int c = 0; c < 4; c++) {
                        if (!(ws[c] > 0)) continue;
                        if (!acc.containsKey(js[c])) keys.add(js[c]);
                        acc.put(js[c], acc.getOrDefault(js[c], 0.0) + (mov.contains(js[c]) ? ws[c] * a : ws[c]));
                    }
                    if (!acc.containsKey(carrier)) keys.add(carrier);
                    acc.put(carrier, acc.getOrDefault(carrier, 0.0) + legW * (1 - a));
                    final List<Integer> order = new ArrayList<>(keys);
                    order.sort((x, y) -> Double.compare(-acc.get(x), -acc.get(y)));  // (stable, as sorted())
                    final int nt = Math.min(4, order.size());
                    final double[] tw = new double[nt];
                    for (int k = 0; k < nt; k++) tw[k] = acc.get(order.get(k));
                    final double tot = Py.sum(tw, nt);
                    for (int k = 0; k < 4; k++) {
                        b[o + 32 + k] = (byte) (k < nt ? order.get(k) : 0);
                        putF(b, o + 36 + 4 * k, k < nt ? tw[k] / tot : 0.0);
                    }
                }
            }
        }
        return b;
    }

    private static double ramp(double z, double at, double half) {
        final double x = (z - (at - half)) / (2 * half);
        final double lo = x > 0.0 ? x : 0.0;  // max(0.0, x)
        return 1.0 < lo ? 1.0 : lo;           // min(1.0, ...)
    }

    /** hariyama_fix.write_node: bind world, inverse bind, rest offset from the parent of node i. */
    private static void writeNode(byte[] b, double[][][] bw, int[] par, int i) {
        final int off = HDR + NODE * i;
        putMat(b, off + 92, bw[i]);
        putMat(b, off + 24, Np.inv(bw[i]));
        final double[][] local = par[i] >= 0 ? Np.mm(bw[i], Np.inv(bw[par[i]])) : bw[i];
        putRow3(b, off + 8, local);
    }

    /** gpk_reparent.py: node -> new parent (each {node, parent}), the rest offset from it in the node's trans. */
    static byte[] reparent(byte[] in, List<int[]> moves) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        final double[][][] bw = new double[nn][][];
        for (int i = 0; i < nn; i++) bw[i] = bindWorld(b, i);
        for (int[] mv : moves) {
            final int node = mv[0], par = mv[1];
            if (!(0 <= par && par < node && node < nn)) throw new IOException("reparent " + node + ":" + par);
            final int off = HDR + NODE * node;
            final double[][] local = Np.mm(bw[node], Np.inv(bw[par]));
            putI(b, off + 4, par);
            for (int k = 0; k < 3; k++) putF(b, off + 8 + 4 * k, local[3][k]);
        }
        return b;
    }
}
