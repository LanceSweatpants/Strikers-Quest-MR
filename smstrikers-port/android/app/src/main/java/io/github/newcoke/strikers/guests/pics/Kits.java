package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.GxTex;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 * Guest captains' kits on the Strikers sidekicks (Toad, Koopa, Hammer Bro, Birdo) and the keeper, in the
 * guest's team colours (tools/outside/kitres.py, the same numbers): every kit of a family is the same picture
 * with the team's colours in it, so each pixel is fitted over the game's eight kits as sum w_area * area colour
 * + d (least squares, leaving out the one or two teams drawn differently there when that fits better), and a
 * guest's kit is that fit with its colours. guests/kits/<family>_<pack>.gtx: the game's own record header,
 * CMPR, every level, the size of the kit it stands in for.
 *
 * Numbers: kitres.py ran on Blender's numpy, whose lstsq works in double (lapack_lite's dgelsd) and rounds to
 * float32; here the same dgelsd step for step (Lapack), rounded the same way; everything else in float32 in
 * numpy's order (see Cmpr). Each different 8-team colour is fitted once (a pixel's fit is its own).
 */
final class Kits {
    private Kits() {}

    static final String[] TEAMS = {"daisy", "donkeykong", "luigi", "mario", "peach", "waluigi", "wario", "yoshi"};
    private static final String[] CAMEL = {"Daisy", "DonkeyKong", "Luigi", "Mario", "Peach", "Waluigi", "Wario", "Yoshi"};
    static final String[] FAMILIES = {"toad", "koopa", "hammer", "birdo", "goalie"};
    private static final String[] DIRS = {"Toad", "Koopa", "HammerBro", "Birdo", null};
    private static final int MARIO = 3;

    interface Step {
        void step(String what);
    }

    interface Warn {
        void warn(String what);
    }

    /** One family's eight team kits (level 0, RGBA bytes) and the first one's record. */
    static final class Family {
        final String name;
        final byte[][] kits = new byte[8][];
        byte[] rec0;
        int w, h, levels;

        Family(String name) {
            this.name = name;
        }
    }

    // ------------------------------------------------------------------ disc

    /** The team's kit file on the disc (its spelling varies: toad_Wario, hammer_DonkeyKong, ...). */
    static String kitPath(Source s, int fam, int team) throws IOException {
        final List<String> c = new ArrayList<>();
        final String t = TEAMS[team], ct = CAMEL[team];
        if (DIRS[fam] == null) {
            for (String d : new String[] {ct, t})
                for (String f : new String[] {ct, t}) c.add("art/characters/" + d + "Goalie/" + f + "Goalie.glt");
            c.add("art/characters/" + t + "goalie/" + t + "goalie.glt");
        } else {
            for (String d : new String[] {DIRS[fam], DIRS[fam].toLowerCase()})
                for (String v : new String[] {t, ct}) c.add("art/characters/" + d + "/" + FAMILIES[fam] + "_" + v + ".glt");
        }
        for (String p : c) if (s.has(p)) return p;
        throw new IOException("no " + c.get(0) + " on the Strikers disc");
    }

    /** kitres.record: the first texture record of a PTLG bundle. */
    static byte[] record(byte[] b) throws IOException {
        if (b.length < 0x30 || b[0] != 'P' || b[1] != 'T' || b[2] != 'L' || b[3] != 'G') throw new IOException("not a PTLG bundle");
        final int n = Bin.u32(b, 4);
        final long off = Bin.u32L(b, 0x24), size = Bin.u32L(b, 0x28);
        final long from = 0x20 + 16L * n + off;
        if (from + size > b.length) throw new IOException("PTLG: bad record");
        return Arrays.copyOfRange(b, (int) from, (int) (from + size));
    }

    static Family family(Source s, int fam) throws IOException {
        final Family f = new Family(FAMILIES[fam]);
        for (int t = 0; t < 8; t++) {
            final byte[] rec = record(s.read(kitPath(s, fam, t)));
            final int levels = Bin.u32(rec, 0), efmt = Bin.u32(rec, 4);
            final int w = Bin.u16(rec, 0x0E), h = Bin.u16(rec, 0x10);
            if (efmt != 2) throw new IOException(f.name + ": not a CMPR kit");
            if (t == 0) {
                f.rec0 = rec;
                f.w = w;
                f.h = h;
                f.levels = levels;
            } else if (w != f.w || h != f.h) {
                throw new IOException(f.name + ": kits of different sizes");
            }
            f.kits[t] = GxTex.decode(rec, 0x20, GxTex.CMPR, w, h, null);
        }
        return f;
    }

    // ------------------------------------------------------------------ the family's colour areas

    /** Which pixels change from team to team (the kits' colour spread > 12 in some channel; exact). */
    static boolean[] varying(byte[][] kits, int n) {
        final boolean[] v = new boolean[n];
        for (int p = 0; p < n; p++) {
            for (int c = 0; c < 3 && !v[p]; c++) {
                int s = 0;
                for (int t = 0; t < 8; t++) s += kits[t][p * 4 + c] & 255;
                long q = 0;  // 64 x the sum of squared deviations
                for (int t = 0; t < 8; t++) {
                    final int d = 8 * (kits[t][p * 4 + c] & 255) - s;
                    q += (long) d * d;
                }
                if (q > 144L * 512) v[p] = true;  // std > 12  <=>  q / 512 > 144
            }
        }
        return v;
    }

    /** Mario's kit colour, 16 levels a channel, as a key. */
    static int[] keys(byte[][] kits, int n) {
        final int[] k = new int[n];
        final byte[] m = kits[MARIO];
        for (int p = 0; p < n; p++)
            k[p] = ((m[p * 4] & 255) >> 4) * 256 + ((m[p * 4 + 1] & 255) >> 4) * 16 + ((m[p * 4 + 2] & 255) >> 4);
        return k;
    }

    /** np.unique(key[varying]) by count, most first (ties: the lower key first). {key, count} pairs. */
    static int[][] byCount(int[] key, boolean[] varying) {
        final int[] count = new int[4096];
        for (int p = 0; p < key.length; p++) if (varying[p]) count[key[p]]++;
        final List<int[]> l = new ArrayList<>();
        for (int k = 0; k < 4096; k++) if (count[k] > 0) l.add(new int[] {k, count[k]});
        l.sort((a, b) -> a[1] != b[1] ? Integer.compare(b[1], a[1]) : Integer.compare(a[0], b[0]));
        return l.toArray(new int[0][]);
    }

    /** np.median over the area of each team's kit, per channel: (8 x 3). */
    static float[][] medians(byte[][] kits, int[] key, boolean[] varying, int k) {
        final float[][] out = new float[8][3];
        final int[][] hist = new int[3][256];
        for (int t = 0; t < 8; t++) {
            for (int[] hh : hist) Arrays.fill(hh, 0);
            int n = 0;
            for (int p = 0; p < key.length; p++)
                if (varying[p] && key[p] == k) {
                    for (int c = 0; c < 3; c++) hist[c][kits[t][p * 4 + c] & 255]++;
                    n++;
                }
            for (int c = 0; c < 3; c++) {
                final int lo = nth(hist[c], (n - 1) / 2), hi = nth(hist[c], n / 2);
                out[t][c] = (n & 1) == 1 ? hi : (lo + hi) / 2f;
            }
        }
        return out;
    }

    private static int nth(int[] hist, int i) {
        int s = 0;
        for (int v = 0; v < 256; v++) {
            s += hist[v];
            if (s > i) return v;
        }
        return 255;
    }

    /** Toad's team colours: (3 role: main / trim / patch, 8 team, rgb). */
    static float[][][] roles(Family toad, StringBuilder log) {
        final int n = toad.w * toad.h;
        final boolean[] varying = varying(toad.kits, n);
        final int[] key = keys(toad.kits, n);
        final int[][] order = byCount(key, varying);
        final List<float[]> cols = new ArrayList<>();
        final List<Integer> areas = new ArrayList<>();
        final byte[] mario = toad.kits[MARIO];
        for (int oi = 0; oi < order.length && areas.size() < 3; oi++) {
            final int k = order[oi][0];
            float s0 = 0f, s1 = 0f, s2 = 0f;  // mario[m].mean(0): a float32 sum, first to last
            int cnt = 0;
            for (int p = 0; p < n; p++)
                if (varying[p] && key[p] == k) {
                    s0 += mario[p * 4] & 255;
                    s1 += mario[p * 4 + 1] & 255;
                    s2 += mario[p * 4 + 2] & 255;
                    cnt++;
                }
            final float[] col = {s0 / cnt, s1 / cnt, s2 / cnt};
            boolean near = false;
            for (float[] r : cols) {
                final float d = Math.max(Math.abs(col[0] - r[0]), Math.max(Math.abs(col[1] - r[1]), Math.abs(col[2] - r[2])));
                if (d < 40f) near = true;
            }
            if (near) continue;
            if (log != null) tieCheck(order, oi, log, "toad roles");
            cols.add(col);
            areas.add(k);
        }
        final float[][][] roles = new float[areas.size()][][];
        for (int r = 0; r < roles.length; r++) roles[r] = medians(toad.kits, key, varying, areas.get(r));
        return roles;
    }

    /** kitres.bases_of: the family's own colour areas (k, 8 team, rgb), shades of one counted once. */
    static float[][][] bases(Family f, int kmax, StringBuilder log) {
        final int n = f.w * f.h;
        final boolean[] varying = varying(f.kits, n);
        final int[] key = keys(f.kits, n);
        final int[][] order = byCount(key, varying);
        final List<float[][]> bases = new ArrayList<>();
        for (int oi = 0; oi < Math.min(40, order.length); oi++) {
            if (order[oi][1] < 0.005 * n) break;
            if (log != null) tieCheck(order, oi, log, f.name + " bases");
            final float[][] seq = medians(f.kits, key, varying, order[oi][0]);
            boolean shade = false;
            for (float[][] b : bases) {
                double sb = 0, bb = 0;  // exact (sums of quarter-integers)
                for (int t = 0; t < 8; t++)
                    for (int c = 0; c < 3; c++) {
                        sb += (double) seq[t][c] * b[t][c];
                        bb += (double) b[t][c] * b[t][c];
                    }
                final float s = (float) sb / Math.max((float) bb, 1e-6f);
                float d2 = 0f, n2 = 0f;
                for (int t = 0; t < 8; t++)
                    for (int c = 0; c < 3; c++) {
                        final float d = seq[t][c] - s * b[t][c];
                        d2 += d * d;
                        n2 += seq[t][c] * seq[t][c];
                    }
                final float nd = (float) Math.sqrt(d2), ns = (float) Math.sqrt(n2);
                if (nd < 0.15 * ns) {
                    shade = true;
                    break;
                }
            }
            if (!shade) bases.add(seq);
            if (bases.size() == kmax) break;
        }
        return bases.toArray(new float[0][][]);
    }

    /** Notes a count tie at a place that was used (numpy's argsort isn't stable: the PC check wants none). */
    private static void tieCheck(int[][] order, int i, StringBuilder log, String what) {
        if ((i > 0 && order[i - 1][1] == order[i][1]) || (i + 1 < order.length && order[i + 1][1] == order[i][1]))
            log.append("TIE ").append(what).append(" at ").append(i).append(" count ").append(order[i][1]).append('\n');
    }

    private static float norm3(float a, float b, float c) {
        return (float) Math.sqrt((a * a + b * b) + c * c);
    }

    // ------------------------------------------------------------------ the fit

    /** A family fitted: per pixel the area weights + offset (k + 3 floats), the bad pixels, alpha. */
    static final class Fit {
        final Family f;
        final float[][][] roles;
        final int k;
        final int[] roleOf;
        final float[] x;     // (k + 3) x n
        final boolean[] bad;
        final byte[] alpha;
        double minSv = Double.MAX_VALUE;  // (the PC check: how well-posed the least squares were)
        int unique;                       // (the PC check: different 8-team colours fitted)

        Fit(Family f, float[][][] roles, int k, int[] roleOf, int n) {
            this.f = f;
            this.roles = roles;
            this.k = k;
            this.roleOf = roleOf;
            this.x = new float[(k + 3) * n];
            this.bad = new boolean[n];
            this.alpha = new byte[n];
        }
    }

    static Fit fit(Family f, float[][][] roles, ExecutorService pool, StringBuilder log) throws IOException {
        final float[][][] bases = bases(f, 5, log);
        final int k = bases.length;
        if (k == 0) throw new IOException(f.name + ": no colour areas");
        final int[] roleOf = new int[k];
        for (int j = 0; j < k; j++) {
            final int[] votes = new int[3];
            for (int t = 0; t < 8; t++) {
                int best = 0;
                float bd = 0f;
                for (int r = 0; r < 3; r++) {
                    final float d = norm3(bases[j][t][0] - roles[r][t][0], bases[j][t][1] - roles[r][t][1],
                            bases[j][t][2] - roles[r][t][2]);
                    if (r == 0 || d < bd) {
                        bd = d;
                        best = r;
                    }
                }
                votes[best]++;
            }
            int r = 0;
            for (int i = 1; i < 3; i++) if (votes[i] > votes[r]) r = i;
            roleOf[j] = r;
        }
        final int n = f.w * f.h, nc = k + 3;
        final Fit fit = new Fit(f, roles, k, roleOf, n);
        // A (24 x nc): the areas' colours in each team's kit, then 1 for the channel's offset
        final float[][] a = new float[24][nc];
        for (int t = 0; t < 8; t++)
            for (int c = 0; c < 3; c++) {
                for (int j = 0; j < k; j++) a[t * 3 + c][j] = bases[j][t][c];
                a[t * 3 + c][k + c] = 1f;
            }
        // subsets: all eight, then each one left out, then each two
        final List<int[]> subsets = new ArrayList<>();
        subsets.add(new int[] {0, 1, 2, 3, 4, 5, 6, 7});
        for (int d = 0; d < 8; d++) subsets.add(without(d, -1));
        for (int d0 = 0; d0 < 8; d0++)
            for (int d1 = d0 + 1; d1 < 8; d1++) subsets.add(without(d0, d1));
        final int ns = subsets.size();
        final int[][] rows = new int[ns][];
        final float[] charge = new float[ns];
        for (int s = 0; s < ns; s++) {
            final int[] sub = subsets.get(s);
            rows[s] = new int[sub.length * 3];
            for (int i = 0; i < sub.length; i++)
                for (int c = 0; c < 3; c++) rows[s][i * 3 + c] = sub[i] * 3 + c;
            charge[s] = (float) (6.0 * (8 - sub.length));
        }
        // each different 8-team colour once (a pixel's fit depends on its own colours only)
        final Unique u = new Unique(f.kits, n);
        final float[] ux = new float[u.count * nc];
        final float[] uerr = new float[u.count];
        final double[] minSv = new double[ns];
        Arrays.fill(minSv, Double.MAX_VALUE);
        final int batch = 1024;
        final List<Future<?>> jobs = new ArrayList<>();
        for (int u0 = 0; u0 < u.count; u0 += batch) {
            final int b0 = u0, b1 = Math.min(u.count, u0 + batch);
            jobs.add(pool.submit(() -> fitColours(u, b0, b1, k, a, rows, charge, ux, uerr, minSv)));
        }
        wait(jobs);
        for (double v : minSv) fit.minSv = Math.min(fit.minSv, v);
        fit.unique = u.count;
        for (int p = 0; p < n; p++) {
            final int id = u.index[p];
            for (int j = 0; j < nc; j++) fit.x[j * n + p] = ux[id * nc + j];
            fit.bad[p] = uerr[id] > 40f;
        }
        for (int p = 0; p < n; p++) {
            int m = 255;
            for (int t = 0; t < 8; t++) m = Math.min(m, f.kits[t][p * 4 + 3] & 255);
            fit.alpha[p] = (byte) m;
        }
        return fit;
    }

    private static int[] without(int d0, int d1) {
        final int[] s = new int[d1 < 0 ? 7 : 6];
        int i = 0;
        for (int t = 0; t < 8; t++) if (t != d0 && t != d1) s[i++] = t;
        return s;
    }

    /** The different 8-team colours (24 bytes) of a family's pixels: index[pixel] -> its colour's number. */
    static final class Unique {
        final int[] index;
        final byte[] cols;  // count x 24
        final int count;

        Unique(byte[][] kits, int n) {
            index = new int[n];
            int cap = Integer.highestOneBit(Math.max(16, n)) << 1;
            final long[] k0 = new long[cap], k1 = new long[cap], k2 = new long[cap];
            final int[] slot = new int[cap];
            Arrays.fill(slot, -1);
            byte[] c = new byte[24 * 1024];
            int count = 0;
            final byte[] y = new byte[24];
            for (int p = 0; p < n; p++) {
                for (int t = 0; t < 8; t++)
                    for (int ch = 0; ch < 3; ch++) y[t * 3 + ch] = kits[t][p * 4 + ch];
                final long a = pack(y, 0), b = pack(y, 8), d = pack(y, 16);
                long hsh = a * 0x9E3779B97F4A7C15L ^ b * 0xC2B2AE3D27D4EB4FL ^ d * 0x165667B19E3779F9L;
                int i = (int) (hsh ^ (hsh >>> 29)) & (cap - 1);
                while (slot[i] >= 0 && (k0[i] != a || k1[i] != b || k2[i] != d)) i = (i + 1) & (cap - 1);
                if (slot[i] < 0) {
                    slot[i] = count;
                    k0[i] = a;
                    k1[i] = b;
                    k2[i] = d;
                    if ((count + 1) * 24 > c.length) c = Arrays.copyOf(c, c.length * 2);
                    System.arraycopy(y, 0, c, count * 24, 24);
                    count++;
                }
                index[p] = slot[i];
            }
            this.cols = c;
            this.count = count;
        }

        private static long pack(byte[] y, int o) {
            long v = 0;
            for (int i = 0; i < 8; i++) v = (v << 8) | (y[o + i] & 255);
            return v;
        }
    }

    /**
     * kitres.py's fit for colours [u0, u1): each subset's least squares (numpy's lstsq: dgelsd in double, then
     * float32), its error (np.abs(A @ Xs - Y).max(0) in float32, the product summed first to last), the best score.
     */
    private static void fitColours(Unique u, int u0, int u1, int k, float[][] a, int[][] rows, float[] charge,
                                   float[] ux, float[] uerr, double[] minSv) {
        final int nc = k + 3, nb = u1 - u0;
        final float[] best = new float[nb];
        Arrays.fill(best, Float.POSITIVE_INFINITY);
        Arrays.fill(uerr, u0, u1, Float.POSITIVE_INFINITY);
        final float[] xs = new float[nc];
        final double[] sv = new double[nc];
        for (int s = 0; s < rows.length; s++) {
            final int[] r = rows[s];
            final int m = r.length;
            final double[] ad = new double[m * nc];
            for (int j = 0; j < nc; j++)
                for (int i = 0; i < m; i++) ad[i + j * m] = a[r[i]][j];
            final double[] b = new double[m * nb];
            for (int c = 0; c < nb; c++)
                for (int i = 0; i < m; i++) b[i + c * m] = u.cols[(u0 + c) * 24 + r[i]] & 255;
            Lapack.dgelsd(m, nc, nb, ad, b, sv, 0x1p-52 * Math.max(m, nc));
            synchronized (minSv) {
                minSv[s] = Math.min(minSv[s], sv[nc - 1] / sv[0]);
            }
            for (int c = 0; c < nb; c++) {
                for (int j = 0; j < nc; j++) xs[j] = (float) b[j + c * m];
                // (zero terms of A change nothing; its 1s add the offsets after the areas, as A's columns go)
                float es = 0f;
                for (int i = 0; i < m; i++) {
                    final float[] ar = a[r[i]];
                    float acc = 0f;
                    for (int j = 0; j < k; j++) acc += ar[j] * xs[j];
                    acc += xs[k + r[i] % 3];
                    final float e = Math.abs(acc - (u.cols[(u0 + c) * 24 + r[i]] & 255));
                    if (e > es) es = e;
                }
                final float score = es + charge[s];
                if (score < best[c]) {
                    best[c] = score;
                    uerr[u0 + c] = es;
                    System.arraycopy(xs, 0, ux, (u0 + c) * nc, nc);
                }
            }
        }
    }

    // ------------------------------------------------------------------ a guest's kit

    /** The guest's kit record (cols: main, trim, patch). */
    static byte[] kit(Fit fit, float[][] cols) throws IOException {
        final Family f = fit.f;
        final int n = f.w * f.h, k = fit.k;
        final float[][] g = new float[k][];
        for (int r = 0; r < k; r++) g[r] = cols[fit.roleOf[r]];
        // the team whose main + trim are nearest the guest's (its pixels where the fit doesn't hold)
        int near = 0;
        float nd = 0f;
        for (int t = 0; t < 8; t++) {
            final float d = norm3(fit.roles[0][t][0] - cols[0][0], fit.roles[0][t][1] - cols[0][1], fit.roles[0][t][2] - cols[0][2])
                    + norm3(fit.roles[1][t][0] - cols[1][0], fit.roles[1][t][1] - cols[1][1], fit.roles[1][t][2] - cols[1][2]);
            if (t == 0 || d < nd) {
                nd = d;
                near = t;
            }
        }
        final byte[] nk = f.kits[near];
        float[] img = new float[n * 4];
        for (int p = 0; p < n; p++) {
            for (int c = 0; c < 3; c++) {
                float v;
                if (fit.bad[p]) {
                    v = nk[p * 4 + c] & 255;
                } else {
                    float acc = 0f;
                    for (int r = 0; r < k; r++) acc += fit.x[r * n + p] * g[r][c];
                    v = acc + fit.x[(k + c) * n + p];
                }
                img[p * 4 + c] = v < 0f ? 0f : v > 255f ? 255f : v;
            }
            img[p * 4 + 3] = fit.alpha[p] & 255;
        }
        final int size = f.rec0.length;
        final byte[] rec = new byte[size];
        System.arraycopy(f.rec0, 0, rec, 0, 0x20);
        int o = 0x20, w = f.w, h = f.h;
        for (int l = 0; l < f.levels; l++) {
            if (l > 0) {
                img = Cmpr.half(img, w, h);
                w /= 2;
                h /= 2;
            }
            if (o + GxTex.sizeOf(GxTex.CMPR, w, h) > size) throw new IOException(f.name + ": kit bigger than the game's");
            o = Cmpr.encode(img, w, h, rec, o);
        }
        if (o != size) throw new IOException(f.name + ": kit " + o + " bytes, the game's " + size);
        return rec;
    }

    // ------------------------------------------------------------------ the run

    /**
     * Every guest's five kits (guests: pack -> main, trim, patch) into outDir/<family>_<pack>.gtx, on the pool's
     * threads; step / warn are called from this thread only.
     */
    static void make(Source strikers, Map<String, float[][]> guests, File outDir, ExecutorService pool, Step step,
                     Warn warn) throws IOException {
        float[][][] roles = null;
        for (int fam = 0; fam < FAMILIES.length; fam++) {
            step.step("Kits: " + FAMILIES[fam] + " (fitting the game's eight)");
            final Family f = family(strikers, fam);
            if (roles == null) roles = roles(fam == 0 ? f : family(strikers, 0), null);
            final Fit fit = fit(f, roles, pool, null);
            final List<String> packs = new ArrayList<>(guests.keySet());
            final List<Future<?>> done = new ArrayList<>();
            for (String p : packs) {
                final File to = new File(outDir, FAMILIES[fam] + "_" + p + ".gtx");
                done.add(pool.submit(() -> {
                    Out.save(to, kit(fit, guests.get(p)));
                    return null;
                }));
            }
            for (int i = 0; i < packs.size(); i++) {
                step.step("Kits: " + FAMILIES[fam] + " " + packs.get(i));
                try {
                    done.get(i).get();
                } catch (Exception e) {
                    warn.warn("Kit " + FAMILIES[fam] + "_" + packs.get(i) + ": " + e);
                }
            }
        }
    }

    /** The run's worker threads (daemons; the caller shuts the pool down). */
    static ExecutorService pool() {
        final int threads = Math.max(1, Math.min(8, Runtime.getRuntime().availableProcessors()));
        return Executors.newFixedThreadPool(threads, r -> {
            final Thread t = new Thread(r, "guest-pictures");
            t.setDaemon(true);
            return t;
        });
    }

    static void wait(List<Future<?>> jobs) throws IOException {
        for (Future<?> j : jobs) {
            try {
                j.get();
            } catch (Exception e) {
                throw new IOException("kits: " + e, e);
            }
        }
    }
}
