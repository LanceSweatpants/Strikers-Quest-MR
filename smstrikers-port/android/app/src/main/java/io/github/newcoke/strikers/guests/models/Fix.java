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
        return allclose(a, b, 1e-8);
    }

    /** np.allclose(a, b, atol=atol) (rtol 1e-5). */
    static boolean allclose(double[][] a, double[][] b, double atol) {
        for (int i = 0; i < a.length; i++)
            for (int j = 0; j < a[i].length; j++)
                if (!(Math.abs(a[i][j] - b[i][j]) <= atol + 1e-5 * Math.abs(b[i][j]))) return false;
        return true;
    }

    /** about() for a turn given in row form R (C-ordered: p @ R is numpy's dgemv_n), 4 x 4 row form. */
    static double[][] aboutRow(double[][] r, double[] p) {
        final double[][] t = eye(4);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i][j] = r[i][j];
        final double[] pr = Np.vm(p, r);
        for (int k = 0; k < 3; k++) t[3][k] = p[k] - pr[k];
        return t;
    }

    /**
     * r135 gpk_repose.py (g22 copy) "turn": the column-form turn of th radians about a unit axis,
     * np.eye(3) + sin(th) k + ((1 - cos(th)) k) @ k.
     */
    static double[][] axisTurnCol(double[] axis, double th) {
        final double[][] k = {{0.0, -axis[2], axis[1]}, {axis[2], 0.0, -axis[0]}, {-axis[1], axis[0], 0.0}};
        final double s = Trig.sin(th), c1 = 1 - Trig.cos(th);
        final double[][] sk = new double[3][3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) sk[i][j] = c1 * k[i][j];
        final double[][] kk = Np.mm(sk, k);
        final double[][] col = new double[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) col[i][j] = ((i == j ? 1.0 : 0.0) + s * k[i][j]) + kk[i][j];
        return col;
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
            } else if (a[0].equals("pitch")) {
                // r135 (colo-maps\g30\tools\gpk_tilt.py): pitch:<pivot>:<subtree root>:<deg> about the pack's X axis
                final int pv = Integer.parseInt(a[1]), rt = Integer.parseInt(a[2]);
                final double dg = Trig.radians(Py.num(a[3]));
                final double c = Trig.cos(dg), s = Trig.sin(dg);
                final double[][] R = {{1.0, 0.0, 0.0}, {0.0, c, s}, {0.0, -s, c}};
                final double[][] T = aboutRow(R, row3(cur[pv]));
                for (int j : subtree(par, rt)) cur[j] = Np.mm(cur[j], T);
            } else if (a[0].equals("turn")) {
                // r135 (colo-maps\g22\gpk_repose.py): turn:<joint>:<x|y|z|ax,ay,az>:<deg>, the joint's subtree about it
                final int j0 = Integer.parseInt(a[1]);
                double[] axis;
                if (a[2].equals("x")) axis = new double[] {1.0, 0.0, 0.0};
                else if (a[2].equals("y")) axis = new double[] {0.0, 1.0, 0.0};
                else if (a[2].equals("z")) axis = new double[] {0.0, 0.0, 1.0};
                else {
                    final String[] v = a[2].split(",");
                    axis = new double[] {Py.num(v[0]), Py.num(v[1]), Py.num(v[2])};
                }
                final double n = Np.norm(axis);
                axis = new double[] {axis[0] / n, axis[1] / n, axis[2] / n};
                final double[][] T = about(axisTurnCol(axis, Trig.radians(Py.num(a[3]))), row3(cur[j0]));
                for (int j : subtree(par, j0)) cur[j] = Np.mm(cur[j], T);
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

    // ------------------------------------------------------------------------------------------------ r135
    private static final double[][] K3 = {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    /** unit_rows: the 3 x 3's rows made unit length ({rows, norms}; numpy: (a0^2 + a1^2) + a2^2, sqrt, divide). */
    private static double[][][] unitRows(double[][] m) {
        final double[][] u = new double[3][3], n = new double[1][3];
        for (int r = 0; r < 3; r++) {
            final double s = (m[r][0] * m[r][0] + m[r][1] * m[r][1]) + m[r][2] * m[r][2];
            n[0][r] = Math.sqrt(s);
            final double d = n[0][r] > 1e-12 ? n[0][r] : 1.0;
            for (int c = 0; c < 3; c++) u[r][c] = m[r][c] / d;
        }
        return new double[][][] {u, n};
    }

    private static double[][] transpose3(double[][] m) {
        final double[][] t = new double[3][3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i][j] = m[j][i];
        return t;
    }

    /**
     * retargetsim.host_bind (as render.Nl.hostBind; here so the models part stands alone): {bone id: bind world} from a
     * Strikers .glg's skin records (chunk 0x1B00A, big-endian, the first of a bone wins).
     */
    static Map<Integer, double[][]> glgBind(byte[] b) {
        final Map<Integer, double[][]> out = new HashMap<>();
        glgWalk(b, 0, b.length, out);
        return out;
    }

    private static void glgWalk(byte[] b, int s, int e, Map<Integer, double[][]> out) {
        int p = s;
        while (p + 8 <= e) {  // (shier.chunks)
            final int cid = Bin.u32(b, p), size = Bin.u32(b, p + 4);
            final int al = (cid & 0x7F000000) >>> 24;
            final int ds = al != 0 ? ((p + (1 << al) + 7) & ~((1 << al) - 1)) : p + 8, end = p + 8 + size;
            final int id = cid & 0x80FFFFFF;
            if (id == 0x1B00A) {
                for (int k = 0; k < (end - ds) / 0x44; k++) {
                    final int at = ds + k * 0x44, bone = Bin.u32(b, at);
                    if (out.containsKey(bone)) continue;
                    final double[][] m = new double[4][4];
                    for (int i = 0; i < 16; i++) m[i / 4][i % 4] = Bin.f32(b, at + 4 + 4 * i);
                    out.put(bone, m);
                }
            } else if ((id & 0x80000000) != 0) {
                glgWalk(b, ds, end, out);
            }
            p = end;
        }
    }

    /** The legs' bone hashes gpk_standbind turns from standing: bip01 l/r thigh, calf, foot, toe0. */
    static Set<Integer> legHashes() {
        final Set<Integer> s = new HashSet<>();
        for (String side : new String[] {"l", "r"})
            for (String p : new String[] {"thigh", "calf", "foot", "toe0"}) s.add(Py.lowerHash("bip01 " + side + " " + p));
        return s;
    }

    /**
     * hostref_all.txt's standing turns of one Strikers class ({bone hash: 3 x 3}; a later line wins, as the dict):
     * "<class> <hash hex> r00 .. r22 x y z".
     */
    static Map<Integer, double[][]> standing(String hostrefAll, String cls) {
        final String want = cls.equalsIgnoreCase("hammerbro") ? "hammerbros" : cls.toLowerCase(java.util.Locale.ROOT);
        final Map<Integer, double[][]> out = new HashMap<>();
        for (String line : hostrefAll.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty()) continue;
            final String[] p = l.split("\\s+");
            if (p.length >= 14 && p[0].equals(want)) {
                final double[][] m = new double[3][3];
                for (int i = 0; i < 9; i++) m[i / 3][i % 3] = Double.parseDouble(p[2 + i]);
                out.put((int) Long.parseLong(p[1], 16), m);
            }
        }
        return out;
    }

    /**
     * r135 gpk_standbind.py (colo-maps\g20\tools): the pack's leg bones turn from the Strikers class's STANDING pose
     * (hostref_all.txt) instead of its .glg bind: only the bindWorld rows change (C = K @ S^T @ B per leg node, others
     * their parent's; new turn = old @ C, new offset from the parent = old offset @ C(parent)).
     */
    static byte[] standbind(byte[] in, Map<Integer, double[][]> binds, Map<Integer, double[][]> stand) {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        final Set<Integer> legs = legHashes();
        final int[] par = new int[nn], mp = new int[nn];
        final double[][][] bw = new double[nn][][], nw = new double[nn][][], C = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            final int o = HDR + NODE * i;
            par[i] = Bin.le32(b, o + 4);
            mp[i] = Bin.le32(b, o + 88);
            bw[i] = mat(b, o + 92);
            nw[i] = copy(bw[i]);
        }
        final double[][] I3 = eye(3);
        for (int i = 0; i < nn; i++) {
            final int p = 0 <= par[i] && par[i] < i ? par[i] : -1;
            final double[][] Cp = p >= 0 ? C[p] : I3;
            double[][] Ci = Cp;
            if (legs.contains(mp[i]) && binds.containsKey(mp[i]) && stand.containsKey(mp[i])) {
                final double[][] Bm = unitRows(binds.get(mp[i]))[0], S = unitRows(stand.get(mp[i]))[0];
                Ci = Np.mm(Np.mm(K3, transpose3(S)), Bm);
            }
            C[i] = Ci;
            final double[][][] gs = unitRows(bw[i]);
            final double[][] gc = Np.mm(gs[0], Ci);
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) nw[i][r][c] = gc[r][c] * gs[1][0][r];
            if (p >= 0) {
                final double[] d = {bw[i][3][0] - bw[p][3][0], bw[i][3][1] - bw[p][3][1], bw[i][3][2] - bw[p][3][2]};
                final double[] v = Np.vm(d, Cp);
                for (int k = 0; k < 3; k++) nw[i][3][k] = nw[p][3][k] + v[k];
            }
        }
        for (int i = 0; i < nn; i++) putMat(b, HDR + NODE * i + 92, nw[i]);
        return b;
    }

    /**
     * r135 gpk_fix135.py (colo-maps\g25): "pivot:<j>:<z>[:<x>:<y>]" (a joint's bind place moved to z x the pack's height
     * over its lowest vertex; no vertex moves), "turn:<j>:<x|y|z>:<deg>" (the joint's subtree turned about it, skin and
     * normals follow), "ground" (lowest vertex back at z 0).
     */
    static byte[] fix135(byte[] in, String[] ops) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        final double height = Bin.lef32(b, 24);
        final int[] par = parents(b);
        for (int i = 0; i < nn; i++) if (par[i] >= nn) par[i] = -1;
        final double[][][] bw0 = new double[nn][][], cur = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            bw0[i] = bindWorld(b, i);
            cur[i] = copy(bw0[i]);
        }
        final List<Integer> vrec = new ArrayList<>();
        for (int[] blk : vertexBlocks(b)) for (int v = 0; v < blk[1]; v++) vrec.add(blk[0] + 52 * v);
        final int nv = vrec.size();
        final double[][] V = new double[nv][6];
        final int[][] js = new int[nv][4];
        final double[][] ws = new double[nv][4];
        for (int v = 0; v < nv; v++) {
            final int q = vrec.get(v);
            for (int k = 0; k < 6; k++) V[v][k] = Bin.lef32(b, q + 4 * k);
            for (int c = 0; c < 4; c++) {
                js[v][c] = b[q + 32 + c] & 0xFF;
                ws[v][c] = Bin.lef32(b, q + 36 + 4 * c);
            }
        }
        final double[][] I4 = eye(4);
        for (String op : ops) {
            final String[] a = op.split(":");
            if (a[0].equals("pivot")) {
                final int j = Integer.parseInt(a[1]);
                final double zf = Py.num(a[2]);
                final double floor = minZ(V);
                final double[] pos = row3(cur[j]);
                pos[2] = floor + zf * height;
                if (a.length >= 5) {
                    pos[0] = Py.num(a[3]) * height;
                    pos[1] = Py.num(a[4]) * height;
                }
                cur[j] = copy(cur[j]);
                for (int k = 0; k < 3; k++) cur[j][3][k] = pos[k];
            } else if (a[0].equals("turn")) {
                final int j = Integer.parseInt(a[1]);
                final double ang = Trig.radians(Py.num(a[3]));
                final double c = Trig.cos(ang), s = Trig.sin(ang);
                final double[][] col;
                switch (a[2]) {
                    case "x": col = new double[][] {{1.0, 0.0, 0.0}, {0.0, c, -s}, {0.0, s, c}}; break;
                    case "y": col = new double[][] {{c, 0.0, s}, {0.0, 1.0, 0.0}, {-s, 0.0, c}}; break;
                    default: col = new double[][] {{c, -s, 0.0}, {s, c, 0.0}, {0.0, 0.0, 1.0}}; break;
                }
                final double[][] T = about(col, row3(cur[j]));
                final List<Integer> sub = subtree(par, j);
                final Set<Integer> subS = new HashSet<>(sub);
                for (int k : sub) cur[k] = Np.mm(cur[k], T);
                for (int v = 0; v < nv; v++) {
                    final int[] jj = new int[4];
                    final double[] ww = new double[4];
                    int n = 0;
                    boolean any = false;
                    for (int q = 0; q < 4; q++) {
                        if (ws[v][q] > 0) {
                            jj[n] = js[v][q];
                            ww[n++] = ws[v][q];
                            if (subS.contains(js[v][q])) any = true;
                        }
                    }
                    if (!any) continue;
                    double tw = Py.sum(ww, n);
                    if (tw == 0.0) tw = 1.0;
                    final double[] pos = {V[v][0], V[v][1], V[v][2], 1.0}, nrm = {V[v][3], V[v][4], V[v][5], 0.0};
                    final double[] npos = new double[4], nnrm = new double[4];
                    for (int q = 0; q < n; q++) {
                        final double[][] dl = subS.contains(jj[q]) ? T : I4;
                        addScaled(npos, ww[q] / tw, Np.vm(pos, dl));
                        addScaled(nnrm, ww[q] / tw, Np.vm(nrm, dl));
                    }
                    double ln = Math.sqrt(Np.dot(new double[] {nnrm[0], nnrm[1], nnrm[2]}, new double[] {nnrm[0], nnrm[1], nnrm[2]}));
                    if (ln == 0.0) ln = 1.0;
                    for (int k = 0; k < 3; k++) {
                        V[v][k] = npos[k];
                        V[v][3 + k] = nnrm[k] / ln;
                    }
                }
            } else if (a[0].equals("ground")) {
                final double dz = -minZ(V);
                for (int k = 0; k < nn; k++) {
                    cur[k] = copy(cur[k]);
                    cur[k][3][2] += dz;
                }
                for (double[] v : V) v[2] += dz;
            } else {
                throw new IOException("fix135: unknown op " + op);
            }
        }
        final boolean[] changed = new boolean[nn];
        for (int d = 0; d < nn; d++) changed[d] = !allclose(cur[d], bw0[d], 1e-7);
        for (int d = 0; d < nn; d++) {
            final boolean pc = par[d] >= 0 && changed[par[d]];
            if (!changed[d] && !pc) continue;
            final int off = HDR + NODE * d;
            if (changed[d]) {
                putMat(b, off + 92, cur[d]);
                putMat(b, off + 24, Np.inv(cur[d]));
            }
            if (par[d] >= 0) putRow3(b, off + 8, Np.mm(cur[d], Np.inv(cur[par[d]])));
            else if (changed[d]) for (int k = 0; k < 3; k++) putF(b, off + 8 + 4 * k, cur[d][3][k]);
        }
        for (int v = 0; v < nv; v++) for (int k = 0; k < 6; k++) putF(b, vrec.get(v) + 4 * k, V[v][k]);
        return b;
    }

    private static double minZ(double[][] V) {
        double m = Double.POSITIVE_INFINITY;
        for (double[] v : V) if (v[2] < m) m = v[2];
        return m;
    }

    /**
     * r135 gpk_pivot.py (colo-maps\g19): "<node>:z=<z>" or "<node>:<x>,<y>,<z>": the node's pivot (bind place) moved, no
     * vertex moves (its inverse bind keeps the skin's rest), its own and its children's rest offsets made again.
     */
    static byte[] pivot(byte[] in, String[] moves) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        final int[] par = parents(b);
        final double[][][] bw = new double[nn][][], inv = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            bw[i] = bindWorld(b, i);
            inv[i] = mat(b, HDR + NODE * i + 24);
        }
        final Set<Integer> moved = new HashSet<>();
        for (String mv : moves) {
            final String[] a = mv.split(":");
            final int node = Integer.parseInt(a[0]);
            if (node < 0 || node >= nn) throw new IOException("pivot node " + node);
            final double[][] rest = Np.mm(inv[node], bw[node]);
            if (a[1].startsWith("z=")) {
                bw[node][3][2] = Py.num(a[1].substring(2));
            } else {
                final String[] xyz = a[1].split(",");
                for (int k = 0; k < 3; k++) bw[node][3][k] = Py.num(xyz[k]);
            }
            inv[node] = Np.mm(rest, Np.inv(bw[node]));
            moved.add(node);
        }
        final Set<Integer> touch = new java.util.TreeSet<>(moved);
        for (int i = 0; i < nn; i++) if (moved.contains(par[i])) touch.add(i);
        for (int i : touch) {
            final int off = HDR + NODE * i;
            if (moved.contains(i)) {
                putMat(b, off + 24, inv[i]);
                putMat(b, off + 92, bw[i]);
            }
            final int p = par[i];
            if (0 <= p && p < nn) putRow3(b, off + 8, Np.mm(bw[i], Np.inv(bw[p])));
        }
        return b;
    }

    // ------------------------------------------------------------------------------------------------ r135 Milotic
    private static final double[][] YZ = {{1, 0, 0, 0}, {0, 0, -1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}};

    /** fish_repose.to_strikers: a file-space world (column form) -> pack row form, scale s, ground shift sh. */
    static double[][] toStrikers(double[][] col, double s, double sh) {
        final double[][] m = copy(col);
        m[1][3] -= sh;
        final double[][] w = Np.mm(YZ, m);
        final double[][] row = new double[4][4];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) row[i][j] = w[j][i];
        for (int k = 0; k < 3; k++) row[3][k] *= s;
        return row;
    }

    /** (PC check FishCheck: the last fish()'s size / centring numbers: f, off, lo, hi) */
    static volatile double[] fishLast;

    /** np.median of a 1-D array (sorted; the middle one, or the mean of the two middle ones). */
    static double median(double[] a) {
        final double[] s = a.clone();
        Arrays.sort(s);
        final int n = s.length;
        if (n % 2 == 1) return s[n / 2];
        return (s[n / 2 - 1] + s[n / 2]) / 2.0;
    }

    /**
     * r135 fish_repose.py (colo-maps\g29, Milotic): the built pack re-posed into one frame of the model's own Colosseum
     * animation, that pose made the bind (linear-blend skin with the pack's weights), sized to height, centred over x / y,
     * its lowest point at hover. Joint order = meleepack's walk (Stand.Model).
     */
    static byte[] fish(byte[] in, Stand.Model M, int ai, double frame, double height, double hover) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nn = nodes(b);
        if (nn != M.joints.size()) throw new IOException("fish: pack has " + nn + " nodes, the model " + M.joints.size());
        final double[][][] BW = new double[nn][][];
        for (int i = 0; i < nn; i++) BW[i] = bindWorld(b, i);
        final double[][] Bf = new double[nn][], Bs = new double[nn][];
        for (int i = 0; i < nn; i++) {
            final double[][] w = M.bind.get(i);
            Bf[i] = new double[] {w[0][3], w[1][3], w[2][3]};
            Bs[i] = row3(BW[i]);
        }
        final List<Double> ratio = new ArrayList<>();
        for (int i = 0; i < nn; i++) if (Math.abs(Bf[i][0]) > 1e-3) ratio.add(Bs[i][0] / Bf[i][0]);
        if (ratio.isEmpty()) throw new IOException("fish: no joint off the centre line (not handled)");
        final double[] ra = new double[ratio.size()];
        for (int i = 0; i < ra.length; i++) ra[i] = ratio.get(i);
        final double s = median(ra);
        final double[] shs = new double[nn];
        for (int i = 0; i < nn; i++) shs[i] = Bf[i][1] - Bs[i][2] / s;
        final double sh = median(shs);
        double err = 0.0;
        for (int i = 0; i < nn; i++) {
            final double[][] chk = toStrikers(M.bind.get(i), s, sh);
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) err = Math.max(err, Math.abs(chk[r][c] - BW[i][r][c]));
        }
        if (err > 1e-3) throw new IOException("fish: the pack's binds are not the model's inverse binds");
        final List<double[][]> pose = M.pose(ai, frame, M.animTracks(ai));
        final double[][][] PW = new double[nn][][], K = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            PW[i] = toStrikers(pose.get(i), s, sh);
            K[i] = Np.mm(Np.inv(BW[i]), PW[i]);
        }
        final List<Integer> recs = new ArrayList<>();
        final List<double[]> allP = new ArrayList<>(), allN = new ArrayList<>();
        for (int[] blk : vertexBlocks(b)) {
            for (int v = 0; v < blk[1]; v++) {
                final int q = blk[0] + 52 * v;
                final double[] p = pos4(b, q), nrm = nrm4(b, q);
                double[] P = new double[4], N = new double[4];
                double tw = 0.0;
                for (int c = 0; c < 4; c++) {
                    final double w = Bin.lef32(b, q + 36 + 4 * c);
                    final int j = b[q + 32 + c] & 0xFF;
                    if (w <= 0 || j >= nn) continue;
                    addScaled(P, w, Np.vm(p, K[j]));
                    addScaled(N, w, Np.vm(nrm, K[j]));
                    tw = tw + w;
                }
                if (tw <= 0) {
                    P = p;
                    N = nrm;
                    tw = 1.0;
                }
                allP.add(new double[] {P[0] / tw, P[1] / tw, P[2] / tw});
                final double[] n3 = {N[0], N[1], N[2]};
                double ln = Math.sqrt(Np.dot(n3, n3));
                if (ln == 0.0) ln = 1.0;
                allN.add(new double[] {n3[0] / ln, n3[1] / ln, n3[2] / ln});
                recs.add(q);
            }
        }
        final double[] lo = {Double.POSITIVE_INFINITY, Double.POSITIVE_INFINITY, Double.POSITIVE_INFINITY};
        final double[] hi = {Double.NEGATIVE_INFINITY, Double.NEGATIVE_INFINITY, Double.NEGATIVE_INFINITY};
        for (double[] p : allP)
            for (int k = 0; k < 3; k++) {
                if (p[k] < lo[k]) lo[k] = p[k];
                if (p[k] > hi[k]) hi[k] = p[k];
            }
        final double f = height / (hi[2] - lo[2]);
        final double[] off = {-(lo[0] + hi[0]) / 2 * f, -(lo[1] + hi[1]) / 2 * f, hover - lo[2] * f};
        fishLast = new double[] {f, off[0], off[1], off[2], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]};
        for (int v = 0; v < recs.size(); v++) {
            final int q = recs.get(v);
            final double[] p = allP.get(v), n = allN.get(v);
            for (int k = 0; k < 3; k++) putF(b, q + 4 * k, p[k] * f + off[k]);
            for (int k = 0; k < 3; k++) putF(b, q + 12 + 4 * k, n[k]);
        }
        final double[][][] PW2 = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            PW2[i] = copy(PW[i]);
            for (int k = 0; k < 3; k++) PW2[i][3][k] = PW[i][3][k] * f + off[k];
        }
        for (int i = 0; i < nn; i++) {
            final int nb = HDR + NODE * i;
            final int p = Bin.le32(b, nb + 4);
            final double[][] local = p >= 0 ? Np.mm(PW2[i], Np.inv(PW2[p])) : PW2[i];
            putRow3(b, nb + 8, local);
            putMat(b, nb + 24, Np.inv(PW2[i]));
            putMat(b, nb + 92, PW2[i]);
        }
        putF(b, 24, height);
        return b;
    }

    // ------------------------------------------------------------------------------------------------ r145
    private static final byte[] SQ = new byte[256];

    static {
        for (int c = 0; c < 256; c++) SQ[c] = (byte) ((c * c + 127) / 255);
    }

    /**
     * r145 gpk_texsquare.py (outside\colo-maps\fix145, Porygon): every texture picture of a built pack with its colour
     * channels squared, c' = (c * c + 127) / 255, alpha kept - Colosseum shows these materials' pictures twice
     * (LIGHTMAP_DIFFUSE + COLORMAP_BLEND); its sprites match the squared colours. The pack's records are RGBA8 tiles
     * (4 x 4: 32 bytes AR, 32 bytes GB) after a 32-byte header (meleepack.texture_record).
     */
    static byte[] texSquare(byte[] in) throws IOException {
        final byte[] b = Arrays.copyOf(in, in.length);
        final int nt = Bin.le32(b, 16);
        int o = HDR + NODE * nodes(b);
        for (int i = 0; i < nt; i++) {
            final int n = Bin.le32(b, o + 4), r = o + 8;
            final int levels = Bin.u32(b, r), fmt = Bin.u32(b, r + 4);
            final int w = ((b[r + 14] & 0xFF) << 8) | (b[r + 15] & 0xFF), h = ((b[r + 16] & 0xFF) << 8) | (b[r + 17] & 0xFF);
            final int tiles = ((w + 3) / 4) * ((h + 3) / 4);
            if (fmt != 3 || levels != 1 || 32 + 64 * tiles != n)
                throw new IOException("texSquare: texture " + i + " is not one level of RGBA8");
            for (int t = 0; t < tiles; t++) {
                final int p = r + 32 + 64 * t;
                for (int k = 0; k < 16; k++) {
                    b[p + 2 * k + 1] = SQ[b[p + 2 * k + 1] & 0xFF];
                    b[p + 32 + 2 * k] = SQ[b[p + 32 + 2 * k] & 0xFF];
                    b[p + 32 + 2 * k + 1] = SQ[b[p + 32 + 2 * k + 1] & 0xFF];
                }
            }
            o += 8 + ((n + 3) & ~3);
        }
        return b;
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
