package io.github.newcoke.strikers.guests.mua;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * meleepack.py's mesh steps (weld, one level of Loop subdivision, smooth normals, strips) as mua2gpk.py runs them on
 * numpy positions: the same as the Melee port (models/Geo) but positions are numpy float64 scalars there, so their
 * round() is numpy's (x * 10^n to the nearest even) and Python's sum() over them is not compensated.
 */
final class MGeo {
    private MGeo() {}

    /** One texture's packet. */
    static final class Packet {
        String key;
        double[][] P;
        int[][] wj;
        double[][] ww;
        double[] u, v;
        int[] tris;
        double[][] N;

        int n() {
            return P.length;
        }

        int ntris() {
            return tris.length / 3;
        }
    }

    static double[] unit(double x, double y, double z) {
        final double ln = Math.sqrt(x * x + y * y + z * z);
        return ln > 1e-20 ? new double[] {x / ln, y / ln, z / ln} : null;
    }

    private static final double[] ZERO = {0.0, 0.0, 0.0};

    private static final class Key {
        final long[] k;
        final int h;

        Key(long[] k) {
            this.k = k;
            this.h = Arrays.hashCode(k);
        }

        @Override
        public boolean equals(Object o) {
            return o instanceof Key && Arrays.equals(k, ((Key) o).k);
        }

        @Override
        public int hashCode() {
            return h;
        }
    }

    /** round(np.float64 x, n) as a key: numpy's rint(x * 10^n). */
    static long nkey(double x, int n) {
        return (long) Math.rint(x * (n == 5 ? 1e5 : n == 4 ? 1e4 : Math.pow(10, n)));
    }

    /** round(python float x, n) as a key: the exact value rounded half to even. */
    static long pkey(double x, int nd) {
        final double y = x * (nd == 5 ? 1e5 : nd == 4 ? 1e4 : Math.pow(10, nd));
        if (Math.abs(y) < 1e9) {
            final double fl = Math.floor(y);
            final double fr = y - fl;
            if (Math.abs(fr - 0.5) > 1e-6) return (long) (fr < 0.5 ? fl : fl + 1.0);
        }
        return new BigDecimal(x).setScale(nd, RoundingMode.HALF_EVEN).unscaledValue().longValue();
    }

    // ------------------------------------------------------------------------------------------------ weld
    static Packet weld(String key, double[][] P, int[][] wj, double[][] ww, double[] u, double[] v, List<int[]> tris) {
        final int n = P.length;
        final double[][] acc = new double[n][3];
        for (int[] t : tris) {
            final double[] a = P[t[0]], b = P[t[1]], c = P[t[2]];
            final double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
            final double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            final double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            for (int q : t) {
                acc[q][0] += nx;
                acc[q][1] += ny;
                acc[q][2] += nz;
            }
        }
        final Map<Key, List<Integer>> groups = new HashMap<>();
        final int[] remap = new int[n];
        final List<Integer> keepIdx = new ArrayList<>();
        final List<double[]> rep = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            final long[] k = new long[3 + 2 * wj[i].length + 2];
            k[0] = nkey(P[i][0], 5);
            k[1] = nkey(P[i][1], 5);
            k[2] = nkey(P[i][2], 5);
            for (int q = 0; q < wj[i].length; q++) {
                k[3 + 2 * q] = wj[i][q];
                k[4 + 2 * q] = pkey(ww[i][q], 4);
            }
            k[k.length - 2] = pkey(u[i], 5);
            k[k.length - 1] = pkey(v[i], 5);
            final Key kk = new Key(k);
            double[] nn = unit(acc[i][0], acc[i][1], acc[i][2]);
            if (nn == null) nn = ZERO;
            int found = -1;
            final List<Integer> g = groups.get(kk);
            if (g != null) {
                for (int x : g) {
                    final double[] r = rep.get(x);
                    if (nn[0] * r[0] + nn[1] * r[1] + nn[2] * r[2] > 0.7) {
                        found = x;
                        break;
                    }
                }
            }
            if (found < 0) {
                found = keepIdx.size();
                keepIdx.add(i);
                rep.add(nn);
                groups.computeIfAbsent(kk, x -> new ArrayList<>()).add(found);
            }
            remap[i] = found;
        }
        final Set<Key> seen = new HashSet<>();
        final int[] out = new int[tris.size() * 3];
        int m = 0;
        for (int[] t : tris) {
            final int a = remap[t[0]], b = remap[t[1]], c = remap[t[2]];
            if (a == b || b == c || a == c) continue;
            if (!seen.add(new Key(canon(a, b, c)))) continue;
            out[m++] = a;
            out[m++] = b;
            out[m++] = c;
        }
        final Packet p = new Packet();
        p.key = key;
        final int nk = keepIdx.size();
        p.P = new double[nk][];
        p.wj = new int[nk][];
        p.ww = new double[nk][];
        p.u = new double[nk];
        p.v = new double[nk];
        for (int i = 0; i < nk; i++) {
            final int s = keepIdx.get(i);
            p.P[i] = P[s];
            p.wj[i] = wj[s];
            p.ww[i] = ww[s];
            p.u[i] = u[s];
            p.v[i] = v[s];
        }
        p.tris = Arrays.copyOf(out, m);
        return p;
    }

    private static long[] canon(int a, int b, int c) {
        int[] best = {a, b, c};
        final int[][] rots = {{b, c, a}, {c, a, b}};
        for (int[] r : rots) if (less(r, best)) best = r;
        return new long[] {best[0], best[1], best[2]};
    }

    private static boolean less(int[] x, int[] y) {
        for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i];
        return false;
    }

    // ------------------------------------------------------------------------------------------------ subdivide
    private static Key key4(double[] p) {
        return new Key(new long[] {nkey(p[0], 4), nkey(p[1], 4), nkey(p[2], 4)});
    }

    static List<Packet> subdivide(List<Packet> packets, double crease) {
        final Map<Key, Integer> gidOf = new HashMap<>();
        final List<double[]> gpos = new ArrayList<>();
        final List<int[]> pkG = new ArrayList<>();
        for (Packet p : packets) {
            final int[] ids = new int[p.n()];
            for (int i = 0; i < p.n(); i++) {
                final Key k = key4(p.P[i]);
                Integer g = gidOf.get(k);
                if (g == null) {
                    g = gpos.size();
                    gidOf.put(k, g);
                    gpos.add(p.P[i]);
                }
                ids[i] = g;
            }
            pkG.add(ids);
        }
        final LinkedHashMap<Long, List<Object[]>> edgeFaces = new LinkedHashMap<>();
        for (int pi = 0; pi < packets.size(); pi++) {
            final Packet p = packets.get(pi);
            final int[] ids = pkG.get(pi);
            for (int t = 0; t < p.ntris(); t++) {
                final int ga = ids[p.tris[t * 3]], gb = ids[p.tris[t * 3 + 1]], gc = ids[p.tris[t * 3 + 2]];
                if (ga == gb || gb == gc || ga == gc) continue;
                final double[] A = gpos.get(ga), B = gpos.get(gb), Cc = gpos.get(gc);
                final double ax = B[0] - A[0], ay = B[1] - A[1], az = B[2] - A[2];
                final double bx = Cc[0] - A[0], by = Cc[1] - A[1], bz = Cc[2] - A[2];
                double[] n = unit(ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx);
                if (n == null) n = ZERO;
                final int[][] uvw = {{ga, gb, gc}, {gb, gc, ga}, {gc, ga, gb}};
                for (int[] e : uvw) edgeFaces.computeIfAbsent(ekey(e[0], e[1]), x -> new ArrayList<>()).add(new Object[] {e[2], n});
            }
        }
        final Set<Long> creases = new HashSet<>();
        for (Map.Entry<Long, List<Object[]>> e : edgeFaces.entrySet()) {
            final List<Object[]> fs = e.getValue();
            if (fs.size() != 2) {
                creases.add(e.getKey());
                continue;
            }
            final double[] n0 = (double[]) fs.get(0)[1], n1 = (double[]) fs.get(1)[1];
            if (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2] < crease) creases.add(e.getKey());
        }
        final LinkedHashMap<Integer, PySet> nbr = new LinkedHashMap<>();
        final Map<Integer, List<Integer>> creaseNbr = new HashMap<>();
        for (long e : edgeFaces.keySet()) {
            final int u = (int) (e >>> 32), v = (int) e;
            nbr.computeIfAbsent(u, x -> new PySet()).add(v);
            nbr.computeIfAbsent(v, x -> new PySet()).add(u);
            if (creases.contains(e)) {
                creaseNbr.computeIfAbsent(u, x -> new ArrayList<>()).add(v);
                creaseNbr.computeIfAbsent(v, x -> new ArrayList<>()).add(u);
            }
        }
        final double[][] even = new double[gpos.size()][];
        for (int g = 0; g < even.length; g++) even[g] = gpos.get(g);
        for (Map.Entry<Integer, PySet> e : nbr.entrySet()) {
            final int g = e.getKey();
            final List<Integer> cn = creaseNbr.get(g);
            final double[] p = gpos.get(g);
            if (cn == null || cn.isEmpty()) {
                final int[] ns = e.getValue().toArray();
                final int n = ns.length;
                final double beta = loopBeta(n);
                final double[] r = new double[3];
                final double[] buf = new double[n];
                for (int c = 0; c < 3; c++) {
                    for (int i = 0; i < n; i++) buf[i] = gpos.get(ns[i])[c];
                    final double s = Np.npSum(buf, n);
                    r[c] = (1 - n * beta) * p[c] + beta * s;
                }
                even[g] = r;
            } else if (cn.size() == 2) {
                final double[] a = gpos.get(cn.get(0)), b = gpos.get(cn.get(1));
                even[g] = new double[] {0.75 * p[0] + 0.125 * (a[0] + b[0]), 0.75 * p[1] + 0.125 * (a[1] + b[1]),
                    0.75 * p[2] + 0.125 * (a[2] + b[2])};
            }
        }
        final Map<Long, double[]> odd = new HashMap<>();
        for (Map.Entry<Long, List<Object[]>> e : edgeFaces.entrySet()) {
            final long k = e.getKey();
            final double[] a = gpos.get((int) (k >>> 32)), b = gpos.get((int) k);
            if (creases.contains(k)) {
                odd.put(k, new double[] {0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1]), 0.5 * (a[2] + b[2])});
            } else {
                final List<Object[]> fs = e.getValue();
                final double[] c1 = gpos.get((Integer) fs.get(0)[0]), c2 = gpos.get((Integer) fs.get(1)[0]);
                final double[] r = new double[3];
                for (int c = 0; c < 3; c++) r[c] = 0.375 * (a[c] + b[c]) + 0.125 * (c1[c] + c2[c]);
                odd.put(k, r);
            }
        }
        final List<Packet> out = new ArrayList<>();
        for (int pi = 0; pi < packets.size(); pi++) {
            final Packet p = packets.get(pi);
            final int[] ids = pkG.get(pi);
            final int n0 = p.n();
            final List<double[]> nP = new ArrayList<>(n0 * 3);
            final List<int[]> nWj = new ArrayList<>(n0 * 3);
            final List<double[]> nWw = new ArrayList<>(n0 * 3);
            final List<double[]> nUv = new ArrayList<>(n0 * 3);
            for (int i = 0; i < n0; i++) {
                nP.add(even[ids[i]]);
                nWj.add(p.wj[i]);
                nWw.add(p.ww[i]);
                nUv.add(new double[] {p.u[i], p.v[i]});
            }
            final Map<Long, Integer> mid = new HashMap<>();
            final int[] nt = new int[p.tris.length * 4];
            int m = 0;
            for (int t = 0; t < p.ntris(); t++) {
                final int a = p.tris[t * 3], b = p.tris[t * 3 + 1], c = p.tris[t * 3 + 2];
                final int ab = midpoint(a, b, p, ids, even, odd, mid, nP, nWj, nWw, nUv);
                final int bc = midpoint(b, c, p, ids, even, odd, mid, nP, nWj, nWw, nUv);
                final int ca = midpoint(c, a, p, ids, even, odd, mid, nP, nWj, nWw, nUv);
                final int[] q = {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca};
                System.arraycopy(q, 0, nt, m, 12);
                m += 12;
            }
            final Packet r = new Packet();
            r.key = p.key;
            final int nn = nP.size();
            r.P = nP.toArray(new double[0][]);
            r.wj = nWj.toArray(new int[0][]);
            r.ww = nWw.toArray(new double[0][]);
            r.u = new double[nn];
            r.v = new double[nn];
            for (int i = 0; i < nn; i++) {
                r.u[i] = nUv.get(i)[0];
                r.v[i] = nUv.get(i)[1];
            }
            r.tris = nt;
            out.add(r);
        }
        return out;
    }

    private static long ekey(int u, int v) {
        return ((long) Math.min(u, v) << 32) | (Math.max(u, v) & 0xFFFFFFFFL);
    }

    private static int midpoint(int i, int j, Packet p, int[] ids, double[][] even, Map<Long, double[]> odd,
                                Map<Long, Integer> mid, List<double[]> nP, List<int[]> nWj, List<double[]> nWw,
                                List<double[]> nUv) {
        final long k = ekey(i, j);
        final Integer have = mid.get(k);
        if (have != null) return have;
        final int gi = ids[i], gj = ids[j];
        double[] pos = gi != gj ? odd.get(ekey(gi, gj)) : even[gi];
        if (pos == null) {
            final double[] a = p.P[i], b = p.P[j];
            pos = new double[] {0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1]), 0.5 * (a[2] + b[2])};
        }
        final int idx = nP.size();
        mid.put(k, idx);
        nP.add(pos);
        mixW(p.wj[i], p.ww[i], p.wj[j], p.ww[j], nWj, nWw);
        nUv.add(new double[] {0.5 * (p.u[i] + p.u[j]), 0.5 * (p.v[i] + p.v[j])});
        return idx;
    }

    /** mix_w: halfway weights (Python floats), the 4 biggest (a stable sort), renormalised (compensated sum). */
    private static void mixW(int[] j1, double[] w1, int[] j2, double[] w2, List<int[]> outJ, List<double[]> outW) {
        final int[] keys = new int[j1.length + j2.length];
        final double[] acc = new double[keys.length];
        int n = 0;
        for (int pass = 0; pass < 2; pass++) {
            final int[] js = pass == 0 ? j1 : j2;
            final double[] ws = pass == 0 ? w1 : w2;
            for (int q = 0; q < js.length; q++) {
                int at = -1;
                for (int i = 0; i < n; i++) if (keys[i] == js[q]) {
                    at = i;
                    break;
                }
                if (at < 0) {
                    at = n++;
                    keys[at] = js[q];
                    acc[at] = 0.0;
                }
                acc[at] = acc[at] + 0.5 * ws[q];
            }
        }
        final Integer[] ord = new Integer[n];
        for (int i = 0; i < n; i++) ord[i] = i;
        for (int i = 1; i < n; i++) {
            final int x = ord[i];
            int k = i - 1;
            while (k >= 0 && -acc[ord[k]] > -acc[x]) {
                ord[k + 1] = ord[k];
                k--;
            }
            ord[k + 1] = x;
        }
        final int top = Math.min(4, n);
        final double[] tw = new double[top];
        for (int i = 0; i < top; i++) tw[i] = acc[ord[i]];
        double tot = Np.pySum(tw, top);
        if (tot == 0.0) tot = 1.0;
        final int[] rj = new int[top];
        final double[] rw = new double[top];
        for (int i = 0; i < top; i++) {
            rj[i] = keys[ord[i]];
            rw[i] = acc[ord[i]] / tot;
        }
        outJ.add(rj);
        outW.add(rw);
    }

    // ------------------------------------------------------------------------------------------------ normals
    static void smoothNormals(List<Packet> packets, double crease) {
        final Map<Key, List<double[]>> faces = new HashMap<>();
        final List<double[][]> own = new ArrayList<>();
        final List<Key[]> keys = new ArrayList<>();
        for (Packet p : packets) {
            final double[][] N = new double[p.n()][3];
            final Key[] kk = new Key[p.n()];
            for (int i = 0; i < p.n(); i++) kk[i] = key4(p.P[i]);
            keys.add(kk);
            for (int t = 0; t < p.ntris(); t++) {
                final int a = p.tris[t * 3], b = p.tris[t * 3 + 1], c = p.tris[t * 3 + 2];
                final double[] A = p.P[a];
                final double ux = p.P[b][0] - A[0], uy = p.P[b][1] - A[1], uz = p.P[b][2] - A[2];
                final double vx = p.P[c][0] - A[0], vy = p.P[c][1] - A[1], vz = p.P[c][2] - A[2];
                final double fx = uy * vz - uz * vy, fy = uz * vx - ux * vz, fz = ux * vy - uy * vx;
                final double[] fu = unit(fx, fy, fz);
                if (fu == null) continue;
                final double[] rec = {fu[0], fu[1], fu[2], fx, fy, fz};
                for (int q : new int[] {a, b, c}) {
                    N[q][0] += fx;
                    N[q][1] += fy;
                    N[q][2] += fz;
                    faces.computeIfAbsent(kk[q], x -> new ArrayList<>()).add(rec);
                }
            }
            final double[][] N0 = new double[p.n()][];
            for (int i = 0; i < p.n(); i++) {
                final double[] u = unit(N[i][0], N[i][1], N[i][2]);
                N0[i] = u == null ? ZERO : u;
            }
            own.add(N0);
        }
        for (int pi = 0; pi < packets.size(); pi++) {
            final Packet p = packets.get(pi);
            final double[][] N0 = own.get(pi);
            final Key[] kk = keys.get(pi);
            p.N = new double[p.n()][];
            for (int vi = 0; vi < p.n(); vi++) {
                final double[] ref = N0[vi];
                double sx = 0.0, sy = 0.0, sz = 0.0;
                final List<double[]> fl = faces.get(kk[vi]);
                if (fl != null) {
                    for (double[] f : fl) {
                        if (f[0] * ref[0] + f[1] * ref[1] + f[2] * ref[2] > crease) {
                            sx += f[3];
                            sy += f[4];
                            sz += f[5];
                        }
                    }
                }
                final double[] n = unit(sx, sy, sz);
                p.N[vi] = n != null ? n : ref;
            }
        }
    }

    // ------------------------------------------------------------------------------------------------ strips
    static int[] stripify(int[] tris) {
        final int nt = tris.length / 3;
        final Map<Long, int[]> edge = new HashMap<>();
        for (int t = 0; t < nt; t++) {
            for (int e = 0; e < 3; e++) {
                final int u = tris[t * 3 + e], v = tris[t * 3 + (e + 1) % 3];
                final long k = ((long) u << 32) | (v & 0xFFFFFFFFL);
                int[] l = edge.get(k);
                if (l == null) {
                    l = new int[3];
                    edge.put(k, l);
                } else if (l[0] + 1 >= l.length) {
                    l = Arrays.copyOf(l, l.length * 2);
                    edge.put(k, l);
                }
                l[++l[0]] = t;
            }
        }
        final boolean[] used = new boolean[nt];
        final int[] mark = new int[nt];
        int stamp = 0;
        final IntList out = new IntList(nt * 2 + 16);
        final IntList[] strips = {new IntList(64), new IntList(64), new IntList(64)};
        final IntList[] takens = {new IntList(64), new IntList(64), new IntList(64)};
        for (int t0 = 0; t0 < nt; t0++) {
            if (used[t0]) continue;
            int best = -1;
            for (int rot = 0; rot < 3; rot++) {
                stamp++;
                final IntList strip = strips[rot], taken = takens[rot];
                strip.n = 0;
                taken.n = 0;
                final int a = tris[t0 * 3], b = tris[t0 * 3 + 1], c = tris[t0 * 3 + 2];
                if (rot == 0) strip.add(a).add(b).add(c);
                else if (rot == 1) strip.add(b).add(c).add(a);
                else strip.add(c).add(a).add(b);
                taken.add(t0);
                mark[t0] = stamp;
                while (true) {
                    final int pos = strip.n - 2;
                    final int u = strip.a[strip.n - 2], v = strip.a[strip.n - 1];
                    final int k0 = pos % 2 == 0 ? u : v, k1 = pos % 2 == 0 ? v : u;
                    final int[] l = edge.get(((long) k0 << 32) | (k1 & 0xFFFFFFFFL));
                    int ntri = -1;
                    if (l != null) {
                        for (int i = 1; i <= l[0]; i++) {
                            final int t = l[i];
                            if (!used[t] && mark[t] != stamp) {
                                ntri = t;
                                break;
                            }
                        }
                    }
                    if (ntri < 0) break;
                    int third = -1;
                    for (int q = 0; q < 3; q++) {
                        final int x = tris[ntri * 3 + q];
                        if (x != k0 && x != k1) {
                            third = x;
                            break;
                        }
                    }
                    strip.add(third);
                    taken.add(ntri);
                    mark[ntri] = stamp;
                }
                if (best < 0 || taken.n > takens[best].n) best = rot;
            }
            final IntList s = strips[best];
            for (int i = 0; i < takens[best].n; i++) used[takens[best].a[i]] = true;
            if (out.n > 0) {
                out.add(out.a[out.n - 1]).add(s.a[0]);
                if (out.n % 2 == 1) out.add(s.a[0]);
            }
            for (int i = 0; i < s.n; i++) out.add(s.a[i]);
        }
        return Arrays.copyOf(out.a, out.n);
    }

    static final class IntList {
        int[] a;
        int n;

        IntList(int cap) {
            a = new int[Math.max(4, cap)];
        }

        IntList add(int v) {
            if (n == a.length) a = Arrays.copyOf(a, a.length * 2);
            a[n++] = v;
            return this;
        }
    }

    static boolean checkStrip(int[] strip, int[] tris) {
        final Set<Key> want = new HashSet<>();
        for (int t = 0; t < tris.length / 3; t++) want.add(new Key(canon(tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2])));
        final Set<Key> got = new HashSet<>();
        for (int k = 0; k + 2 < strip.length; k++) {
            int a = strip[k], b = strip[k + 1];
            final int c = strip[k + 2];
            if (a == b || b == c || a == c) continue;
            if (k % 2 == 1) {
                final int x = a;
                a = b;
                b = x;
            }
            got.add(new Key(canon(a, b, c)));
        }
        return got.equals(want);
    }

    // ------------------------------------------------------------------------------------------------ Python
    /** Loop subdivision's beta(n): the PC's own values (its cos differs from fdlibm in the last bit now and then). */
    static double loopBeta(int n) {
        if (n >= 1 && n <= BETA.length) return BETA[n - 1];
        final double c = 0.375 + 0.25 * StrictMath.cos(2 * Math.PI / n);
        return (0.625 - c * c) / n;
    }

    private static final double[] BETA = parseHex(
            "0x1.e000000000000p-3 0x1.3800000000000p-2 0x1.8000000000000p-3 0x1.f000000000000p-4 "
            + "0x1.58722191a02d6p-4 0x1.0000000000000p-4 0x1.919cb59d1c909p-5 0x1.483c499904994p-5 "
            + "0x1.14c451633a01ap-5 0x1.de0c5dfe26e56p-6 0x1.a4a25dacb877dp-6 0x1.77a17e1292effp-6 "
            + "0x1.5376be5037e45p-6 0x1.35c915de7fe41p-6 0x1.1cffe3879aa0cp-6 0x1.07fcdabe51748p-6 "
            + "0x1.ebe412c28874fp-7 0x1.cc8fa170783aep-7 0x1.b1188228c29a5p-7 0x1.98d0531ae3488p-7 "
            + "0x1.832f728e4119ep-7 0x1.6fcad7029be4ap-7 0x1.5e4ceb55ba47fp-7 0x1.4e7071e5444bdp-7 "
            + "0x1.3ffccb891b8d6p-7 0x1.32c336fd5a3c6p-7 0x1.269cc0a49b051p-7 0x1.1b68b1626cc39p-7 "
            + "0x1.110b5a5ab42e3p-7 0x1.076d2579c085dp-7 0x1.fcf3b311013ddp-8 0x1.ec400aa2028d4p-8 "
            + "0x1.dca11158dcc95p-8 0x1.cdfc69635fbd8p-8 0x1.c03afbde8be00p-8 0x1.b34879af62125p-8 "
            + "0x1.a712f2e0bea95p-8 0x1.9b8a7fff92de7p-8 0x1.90a0f9f18c2f5p-8 0x1.8649bd85c6e7bp-8 "
            + "0x1.7c79789432d94p-8 0x1.7325fef2cdc72p-8 0x1.6a4625e071658p-8 0x1.61d1a4ca01a00p-8 "
            + "0x1.59c0fa84f7854p-8 0x1.520d56450f2eap-8 0x1.4ab083b4dd6eap-8 0x1.43a4d9b43bbc5p-8 "
            + "0x1.3ce52b554bbe1p-8 0x1.366cbac274371p-8 0x1.30372dc607d3fp-8 0x1.2a4083b7f1a5ap-8 "
            + "0x1.24850c9f52bdep-8 0x1.1f01615dda8ebp-8 0x1.19b25cc1242b1p-8 0x1.1495155bc794fp-8 "
            + "0x1.0fa6d80c4c855p-8 0x1.0ae5231bedba0p-8 0x1.064da1e2429f9p-8 0x1.01de28dd8c563p-8 "
            + "0x1.fb2964634172cp-9 0x1.f2deb502a83f6p-9 0x1.ead8bc2fbf3ecp-9 0x1.e3142cb24388ap-9");

    private static double[] parseHex(String s) {
        final String[] p = s.trim().split("\\s+");
        final double[] out = new double[p.length];
        for (int i = 0; i < p.length; i++) out[i] = Double.parseDouble(p[i]);
        return out;
    }

    /** CPython's set of non-negative ints, only added to: its iteration order (slot order). */
    static final class PySet {
        private int[] keys = new int[8];
        private boolean[] full = new boolean[8];
        private int mask = 7, fill;

        void add(int key) {
            int i = key & mask;
            long perturb = key;
            while (true) {
                final int probes = (i + 9 <= mask) ? 9 : 0;
                int e = i;
                for (int k = 0; k <= probes; k++, e++) {
                    if (!full[e]) {
                        full[e] = true;
                        keys[e] = key;
                        fill++;
                        if (fill * 5 >= mask * 3) resize(fill > 50000 ? fill * 2 : fill * 4);
                        return;
                    }
                    if (keys[e] == key) return;
                }
                perturb >>>= 5;
                i = (int) ((i * 5L + 1 + perturb) & mask);
            }
        }

        private void resize(int minused) {
            int size = 8;
            while (size <= minused) size <<= 1;
            final int[] ok = keys;
            final boolean[] of = full;
            keys = new int[size];
            full = new boolean[size];
            mask = size - 1;
            for (int s = 0; s < ok.length; s++) {
                if (!of[s]) continue;
                final int key = ok[s];
                int i = key & mask;
                long perturb = key;
                outer:
                while (true) {
                    if (!full[i]) {
                        full[i] = true;
                        keys[i] = key;
                        break;
                    }
                    if (i + 9 <= mask) {
                        for (int j = 1; j <= 9; j++) {
                            if (!full[i + j]) {
                                full[i + j] = true;
                                keys[i + j] = key;
                                break outer;
                            }
                        }
                    }
                    perturb >>>= 5;
                    i = (int) ((i * 5L + 1 + perturb) & mask);
                }
            }
        }

        int[] toArray() {
            final int[] out = new int[fill];
            int n = 0;
            for (int s = 0; s <= mask; s++) if (full[s]) out[n++] = keys[s];
            return out;
        }
    }
}
