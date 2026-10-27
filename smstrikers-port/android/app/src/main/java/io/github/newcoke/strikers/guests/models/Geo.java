package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** meleepack's mesh steps: weld, Loop subdivision, smooth normals, triangle strips, the packet record. */
final class Geo {
    private Geo() {}

    /** One texture's packet: Strikers-space positions, per vertex weights (joint index, weight) + UV, triangles. */
    static final class Packet {
        int th;
        double[][] P;
        int[][] wj;
        double[][] ww;
        double[] u, v;
        int[] tris;     // triples
        double[][] N;   // normals (smooth_normals)

        int n() {
            return P.length;
        }

        int ntris() {
            return tris.length / 3;
        }
    }

    /** unit(): null for the zero vector (Python's (0.0, 0.0, 0.0)). */
    static double[] unit(double x, double y, double z) {
        final double ln = Math.sqrt(x * x + y * y + z * z);
        return ln > 1e-20 ? new double[] {x / ln, y / ln, z / ln} : null;
    }

    private static final double[] ZERO = {0.0, 0.0, 0.0};

    /** A tuple key of longs (Python's tuple of rounded floats and ints). */
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

    // ------------------------------------------------------------------------------------------------ weld
    static Packet weld(int th, double[][] P, List<MeleePack.Vx> V, List<int[]> tris) {
        final int n = P.length;
        // face_normals (area weighted, every triangle), unit
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
            final MeleePack.Vx v = V.get(i);
            final long[] k = new long[3 + 2 * v.wj.length + 2];
            k[0] = Py.rkey(P[i][0], 5);
            k[1] = Py.rkey(P[i][1], 5);
            k[2] = Py.rkey(P[i][2], 5);
            for (int q = 0; q < v.wj.length; q++) {
                k[3 + 2 * q] = v.wj[q];
                k[4 + 2 * q] = Py.rkey(v.ww[q], 4);
            }
            k[k.length - 2] = Py.rkey(v.u, 5);
            k[k.length - 1] = Py.rkey(v.v, 5);
            final Key key = new Key(k);
            double[] nn = unit(acc[i][0], acc[i][1], acc[i][2]);
            if (nn == null) nn = ZERO;
            int found = -1;
            final List<Integer> g = groups.get(key);
            if (g != null) {
                for (int kk : g) {
                    final double[] r = rep.get(kk);
                    if (nn[0] * r[0] + nn[1] * r[1] + nn[2] * r[2] > 0.7) {
                        found = kk;
                        break;
                    }
                }
            }
            if (found < 0) {
                found = keepIdx.size();
                keepIdx.add(i);
                rep.add(nn);
                groups.computeIfAbsent(key, x -> new ArrayList<>()).add(found);
            }
            remap[i] = found;
        }
        final Set<Key> seen = new HashSet<>();
        final int[] out = new int[tris.size() * 3];
        int m = 0;
        for (int[] t : tris) {
            final int a = remap[t[0]], b = remap[t[1]], c = remap[t[2]];
            if (a == b || b == c || a == c) continue;
            final Key k = new Key(canon(a, b, c));
            if (!seen.add(k)) continue;
            out[m++] = a;
            out[m++] = b;
            out[m++] = c;
        }
        final Packet p = new Packet();
        p.th = th;
        final int nk = keepIdx.size();
        p.P = new double[nk][];
        p.wj = new int[nk][];
        p.ww = new double[nk][];
        p.u = new double[nk];
        p.v = new double[nk];
        for (int i = 0; i < nk; i++) {
            final int s = keepIdx.get(i);
            final MeleePack.Vx v = V.get(s);
            p.P[i] = P[s];
            p.wj[i] = v.wj;
            p.ww[i] = v.ww;
            p.u[i] = v.u;
            p.v[i] = v.v;
        }
        p.tris = Arrays.copyOf(out, m);
        return p;
    }

    /** min((a, b, c), (b, c, a), (c, a, b)) */
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
        return new Key(new long[] {Py.rkey(p[0], 4), Py.rkey(p[1], 4), Py.rkey(p[2], 4)});
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
        // edge -> [(opposite vertex, face unit normal)]
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
                for (int[] e : uvw) {
                    edgeFaces.computeIfAbsent(ekey(e[0], e[1]), x -> new ArrayList<>()).add(new Object[] {e[2], n});
                }
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
        final LinkedHashMap<Integer, Py.IntSet> nbr = new LinkedHashMap<>();
        final Map<Integer, List<Integer>> creaseNbr = new HashMap<>();
        for (long e : edgeFaces.keySet()) {
            final int u = (int) (e >>> 32), v = (int) e;
            nbr.computeIfAbsent(u, x -> new Py.IntSet()).add(v);
            nbr.computeIfAbsent(v, x -> new Py.IntSet()).add(u);
            if (creases.contains(e)) {
                creaseNbr.computeIfAbsent(u, x -> new ArrayList<>()).add(v);
                creaseNbr.computeIfAbsent(v, x -> new ArrayList<>()).add(u);
            }
        }
        final double[][] even = new double[gpos.size()][];
        for (int g = 0; g < even.length; g++) even[g] = gpos.get(g);
        for (Map.Entry<Integer, Py.IntSet> e : nbr.entrySet()) {
            final int g = e.getKey();
            final List<Integer> cn = creaseNbr.get(g);
            final double[] p = gpos.get(g);
            if (cn == null || cn.isEmpty()) {
                final int[] ns = e.getValue().toArray();
                final int n = ns.length;
                final double beta = Py.loopBeta(n);
                final double[] r = new double[3];
                final double[] buf = new double[n];
                for (int c = 0; c < 3; c++) {
                    for (int i = 0; i < n; i++) buf[i] = gpos.get(ns[i])[c];
                    final double s = Py.sum(buf, n);
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
            r.th = p.th;
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

    /** mix_w: halfway weights, the 4 biggest (a stable sort), renormalised. */
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
        // stable sort by -weight (insertion sort keeps equal ones in order)
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
        double tot = Py.sum(tw, top);
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
        final Map<Key, List<double[]>> faces = new HashMap<>();  // place -> [fu (3), fn (3)]
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
        final Map<Long, int[]> edge = new HashMap<>();  // directed edge -> triangles (count first)
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

    // ------------------------------------------------------------------------------------------------ emit
    /** The packet record(s) (two halves when the strip is too long): returns how many. */
    static int emit(Out o, Packet p, boolean soft) throws IOException {
        final int[] strip = stripify(p.tris);
        if (strip.length > 65535 && p.ntris() > 1) {
            final int h = p.ntris() / 2;
            int n = 0;
            for (int part = 0; part < 2; part++) {
                final int t0 = part == 0 ? 0 : h, t1 = part == 0 ? h : p.ntris();
                final java.util.TreeSet<Integer> usedS = new java.util.TreeSet<>();
                for (int t = t0; t < t1; t++) for (int q = 0; q < 3; q++) usedS.add(p.tris[t * 3 + q]);
                final int[] used = new int[usedS.size()];
                int i = 0;
                for (int u : usedS) used[i++] = u;
                final Map<Integer, Integer> re = new HashMap<>();
                for (int k = 0; k < used.length; k++) re.put(used[k], k);
                final Packet q = new Packet();
                q.th = p.th;
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
                n += emit(o, q, soft);
            }
            return n;
        }
        if (!checkStrip(strip, p.tris)) throw new IOException("strip check failed");
        o.le32(p.th).le32(p.n() | (soft ? 0x80000000 : 0)).le32(strip.length);
        for (int vi = 0; vi < p.n(); vi++) {
            final double[] P = p.P[vi], N = p.N[vi];
            final int[] wj = p.wj[vi];
            final double[] ww = p.ww[vi];
            double tot = Py.sum(ww, ww.length);
            if (tot == 0.0) tot = 1.0;
            o.lef32((float) P[0]).lef32((float) P[1]).lef32((float) P[2]);
            o.lef32((float) N[0]).lef32((float) N[1]).lef32((float) N[2]);
            o.lef32((float) p.u[vi]).lef32((float) p.v[vi]);
            for (int q = 0; q < 4; q++) {
                if (q < wj.length) {
                    if (wj[q] > 255) throw new IOException("joint index " + wj[q] + " past 255");
                    o.u8(wj[q]);
                } else {
                    o.u8(0xFF);
                }
            }
            for (int q = 0; q < 4; q++) o.lef32(q < wj.length ? (float) (ww[q] / tot) : 0.0f);
        }
        for (int s : strip) o.le16(s);
        o.align(4);
        return 1;
    }
}
