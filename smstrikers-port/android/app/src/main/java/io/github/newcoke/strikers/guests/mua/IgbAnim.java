package io.github.newcoke.strikers.guests.mua;

import java.io.IOException;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * MUA's animations (igb.py: animation_tracks, enbaya_decode, sample_track, pose_world), number for number: the
 * Enbaya streams decoded as the Python does (doubles, its own rounding order), each bone's key sampled (slerp as
 * numpy's normalised lerp), the posed world matrices by numpy's products.
 */
public final class IgbAnim {
    private IgbAnim() {}

    /** One bone's track: key times (s), quaternions x y z w, translations. */
    public static final class Track {
        public final double[] times;
        public final double[][] q, tr;
        /** np.any(np.abs(tr) > 1e-6): the track moves the bone (else the skeleton's bind translation). */
        final boolean moves;

        Track(double[] times, double[][] q, double[][] tr) {
            this.times = times;
            this.q = q;
            this.tr = tr;
            boolean m = false;
            for (double[] t : tr) for (double v : t) if (Math.abs(v) > 1e-6) m = true;
            moves = m;
        }
    }

    /** A clip: its tracks by bone name, and its length (igb clip_sampler's rule). */
    public static final class Clip {
        public final Map<String, Track> tracks;
        public final double dur;

        Clip(Map<String, Track> tracks, double dur) {
            this.tracks = tracks;
            this.dur = dur;
        }
    }

    // ------------------------------------------------------------------------------------------------ the set
    /** Decoded Enbaya streams by memory index (shared by the tracks of one animation set). */
    private static final Map<Igb, Map<Integer, double[][][]>> CACHE = new java.util.WeakHashMap<>();

    /** The named animation (the first of that name), or null; dur as mua2gpk / mua_cels / build_home take it. */
    public static Clip clip(Igb ag, String name) throws IOException {
        if (ag == null) return null;
        for (Igb.Obj an : ag.ofType("igAnimation")) {
            if (!name.equals(an.s(2))) continue;
            final Map<String, Track> tracks = tracks(ag, an);
            double dur = 0.0;
            boolean any = false;
            for (Track t : tracks.values()) {
                if (t.times.length == 0) continue;
                final double last = t.times[t.times.length - 1];
                if (!any || last > dur) dur = last;
                any = true;
            }
            final Long ns = durationNs(an);
            if (ns != null) dur = ns / 1e9;
            return new Clip(tracks, dur);
        }
        return null;
    }

    /** The last positive igLong field of an igAnimation (its length in nanoseconds), or null. */
    static Long durationNs(Igb.Obj an) {
        Long out = null;
        for (int s : an.g.metaSlots(an)) {
            if (!"igLongMetaField".equals(an.fieldType(s))) continue;
            final Object v = an.get(s);
            long x;
            if (v instanceof Long) x = (Long) v;
            else if (v instanceof Integer) x = (Integer) v;
            else continue;
            if (x > 0) out = x;
        }
        return out;
    }

    /** igb.animation_tracks */
    static Map<String, Track> tracks(Igb g, Igb.Obj anim) throws IOException {
        final Map<String, Track> out = new LinkedHashMap<>();
        final Igb.Obj tl = anim.obj(5);
        if (tl == null) return out;
        Map<Integer, double[][][]> cache;
        synchronized (CACHE) {
            cache = CACHE.computeIfAbsent(g, k -> new HashMap<>());
        }
        for (Igb.Obj t : g.items(tl)) {
            if (t == null) continue;
            final Igb.Obj src = t.obj(3);
            final String name = t.s(2) != null ? t.s(2) : "";
            if (src != null && src.type.equals("igEnbayaTransformSource")) {
                final Igb.Obj es = src.obj(3);
                double[][][] dec;
                synchronized (cache) {
                    dec = cache.get(es.index);
                    if (dec == null) {
                        dec = enbaya(g, es.mem(2));
                        cache.put(es.index, dec);
                    }
                }
                // dec[0] = times (as one row), dec[1 + 2 * track] = quats, dec[2 + 2 * track] = translations
                final int k = src.i(2, 0);
                out.put(name, new Track(dec[0][0], dec[1 + 2 * k], dec[2 + 2 * k]));
            } else if (src != null && src.type.startsWith("igTransformSequence")) {
                final Track r = sequence(g, src);
                if (r != null) out.put(name, r);
            } else {
                double[] q = t.vec(4);
                double[] tr = t.vec(5);
                if (q == null || q.length == 0) q = new double[] {0, 0, 0, 1};
                if (tr == null || tr.length == 0) tr = new double[] {0, 0, 0};
                out.put(name, new Track(new double[] {0.0}, new double[][] {q.clone()}, new double[][] {tr.clone()}));
            }
        }
        return out;
    }

    private static Track sequence(Igb g, Igb.Obj seq) throws IOException {
        final double[][] tr = seq.obj(2) != null ? g.listArray(seq.obj(2)) : null;
        final double[][] qs = seq.obj(3) != null ? g.listArray(seq.obj(3)) : null;
        final double[][] tm = seq.obj(11) != null ? g.listArray(seq.obj(11)) : null;
        int n = 0;
        if (tr != null) n = Math.max(n, tr.length);
        if (qs != null) n = Math.max(n, qs.length);
        if (tm != null) n = Math.max(n, tm.length);
        if (n == 0) return null;
        final double[] times = new double[n];
        for (int i = 0; i < n; i++) times[i] = tm != null && tm.length == n ? tm[i][0] / 1e9 : i;
        final double[][] q = new double[n][];
        final double[][] t = new double[n][];
        for (int i = 0; i < n; i++) {
            q[i] = qs != null && qs.length == n ? qs[i].clone() : new double[] {0, 0, 0, 1.0};
            t[i] = tr != null && tr.length == n ? tr[i].clone() : new double[] {0, 0, 0};
        }
        return new Track(times, q, t);
    }

    // ------------------------------------------------------------------------------------------------ Enbaya
    private static final int[] I4 = {0, 8, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -9};

    private static final class Bits {
        final byte[] d;
        final int off, len, bits;
        int n;

        Bits(byte[] d, int off, int len, int bits) {
            this.d = d;
            this.off = off;
            this.len = len;
            this.bits = bits;
        }

        int get() {
            final int per = 8 / bits;
            final int b = d[off + n / per] & 0xFF;
            final int shift = 8 - bits * (n % per + 1);
            n++;
            return (b >> shift) & ((1 << bits) - 1);
        }

        int used() {
            final int per = 8 / bits;
            return (n + per - 1) / per;
        }
    }

    private static final class Vals {
        final byte[] d;
        final int off, len, size;
        final boolean signed;
        int p;

        Vals(byte[] d, int off, int len, int size, boolean signed) {
            this.d = d;
            this.off = off;
            this.len = len;
            this.size = size;
            this.signed = signed;
        }

        long get() {
            final int o = off + p;
            p += size;
            switch (size) {
                case 1: return signed ? d[o] : d[o] & 0xFF;
                case 2: {
                    final int v = (d[o] & 0xFF) | ((d[o + 1] & 0xFF) << 8);
                    return signed ? (short) v : v;
                }
                default: {
                    final int v = (d[o] & 0xFF) | ((d[o + 1] & 0xFF) << 8) | ((d[o + 2] & 0xFF) << 16) | ((d[o + 3] & 0xFF) << 24);
                    return signed ? v : v & 0xFFFFFFFFL;
                }
            }
        }
    }

    /** igb.enbaya_decode: {times}, then per track quats (n x 4), translations (n x 3). */
    static double[][][] enbaya(Igb g, Igb.Mem m) throws IOException {
        final byte[] d = g.d;
        final int b0 = m.off;
        final int ntr = g.le32(b0 + 4);
        final double qe = g.lef32(b0 + 8);
        final double dur = g.lef32(b0 + 12);
        final long rate = g.le32(b0 + 16) & 0xFFFFFFFFL;
        final int[] h = new int[15];
        for (int k = 0; k < 15; k++) h[k] = g.le32(b0 + 20 + 4 * k);
        final int lIi2 = h[0], lIi8 = h[1], lIi16 = h[2], lIi32 = h[3], lTi2 = h[4], lTi4 = h[5], lTi8 = h[6], lTi16 = h[7],
                lTi32 = h[8], lSu2 = h[9], lSu8 = h[10], lSu16 = h[11], lSu32 = h[12], lFl = h[13];
        int p = b0 + 0x50;
        final int[] lens = {lIi32, lTi32, lSu32, lIi16, lTi16, lSu16, lIi2, lIi8, lTi2, lTi4, lTi8, lSu2, lSu8, lFl};
        final int[] at = new int[lens.length];
        for (int k = 0; k < lens.length; k++) {
            at[k] = p;
            p += lens[k];
        }
        if (p != b0 + m.len) throw new IOException("enbaya: lengths add to " + (p - b0) + ", block is " + m.len);
        final Vals ii32 = new Vals(d, at[0], lIi32, 4, true), ti32 = new Vals(d, at[1], lTi32, 4, true);
        final Vals su32 = new Vals(d, at[2], lSu32, 4, false), ii16 = new Vals(d, at[3], lIi16, 2, true);
        final Vals ti16 = new Vals(d, at[4], lTi16, 2, true), su16 = new Vals(d, at[5], lSu16, 2, false);
        final Bits ii2 = new Bits(d, at[6], lIi2, 2);
        final Vals ii8 = new Vals(d, at[7], lIi8, 1, true);
        final Bits ti2 = new Bits(d, at[8], lTi2, 2), ti4 = new Bits(d, at[9], lTi4, 4);
        final Vals ti8 = new Vals(d, at[10], lTi8, 1, true);
        final Bits su2 = new Bits(d, at[11], lSu2, 2);
        final Vals su8 = new Vals(d, at[12], lSu8, 1, false);
        final int flOff = at[13];

        final int nc = ntr * 7;
        final double[][] val = new double[ntr][7];
        for (int t = 0; t < ntr; t++) {
            final double[] v = new double[7];
            for (int j = 0; j < 7; j++) {
                final int s = ii2.get();
                final long iv = s == 0 ? 0 : s == 1 ? ii8.get() : s == 2 ? ii16.get() : ii32.get();
                v[j] = iv * qe;
            }
            norm(v, val[t]);
        }
        final long[][] acc = new long[ntr][7];
        final int[] flags = new int[ntr];
        for (int t = 0; t < ntr; t++) flags[t] = t < lFl ? d[flOff + t] & 0xFF : 0;
        long nextStep = stateVal(su2, su8, su16, su32);
        final java.util.List<double[][]> frames = new java.util.ArrayList<>();
        final java.util.List<Double> times = new java.util.ArrayList<>();
        frames.add(copy(val));
        times.add(0.0);
        final int nmin = dur > 0 ? (int) Math.rint(dur * rate) + 1 : 1;  // (Python's round: half to even)
        int k = 0;
        while (true) {
            k++;
            if (k >= nmin && exhausted(ti2, ti4, ti8, ti16, ti32, lTi2, lTi4, lTi8, lTi16, lTi32)) break;
            if (k > nmin + 256) throw new IOException("enbaya: delta streams never ran out");
            if (k > 1) {
                int i = 0;
                while (i < nc) {
                    if (nextStep == 0) {
                        flags[i / 7] ^= 1 << (i % 7);
                        nextStep = stateVal(su2, su8, su16, su32);
                        i++;
                    } else {
                        final long s = Math.min(nextStep, nc - i);
                        i += (int) s;
                        nextStep -= s;
                    }
                }
            }
            for (int t = 0; t < ntr; t++) {
                final int f = flags[t];
                if (f != 0) {
                    for (int j = 0; j < 7; j++) if ((f & (1 << j)) != 0) acc[t][j] += delta(ti2, ti4, ti8, ti16, ti32);
                }
            }
            for (int t = 0; t < ntr; t++) {
                final double[] nv = new double[7];
                for (int j = 0; j < 7; j++) nv[j] = val[t][j] + acc[t][j] * qe;
                norm(nv, val[t]);
            }
            frames.add(copy(val));
            times.add(Math.min(k / (double) rate, dur));
        }
        final boolean ok = ti2.used() == lTi2 && ti4.used() == lTi4 && ti8.p == lTi8 && ti16.p == lTi16 && ti32.p == lTi32
                && ii2.used() == lIi2 && ii8.p == lIi8 && ii16.p == lIi16 && ii32.p == lIi32;
        if (!ok) throw new IOException("enbaya: streams not fully consumed");
        final int ns = frames.size();
        final double[][][] out = new double[1 + 2 * ntr][][];
        final double[] tm = new double[ns];
        for (int i = 0; i < ns; i++) tm[i] = times.get(i);
        out[0] = new double[][] {tm};
        for (int t = 0; t < ntr; t++) {
            final double[][] q = new double[ns][4], tr = new double[ns][3];
            for (int i = 0; i < ns; i++) {
                final double[] v = frames.get(i)[t];
                System.arraycopy(v, 0, q[i], 0, 4);
                System.arraycopy(v, 4, tr[i], 0, 3);
            }
            out[1 + 2 * t] = q;
            out[2 + 2 * t] = tr;
        }
        return out;
    }

    private static double[][] copy(double[][] v) {
        final double[][] r = new double[v.length][];
        for (int i = 0; i < v.length; i++) r[i] = v[i].clone();
        return r;
    }

    /** norm(q) + the translation: the quaternion part divided by its length (sum left to right, math.sqrt). */
    private static void norm(double[] v, double[] out) {
        final double n = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        if (n < 1e-7) {
            out[0] = 0.0;
            out[1] = 0.0;
            out[2] = 0.0;
            out[3] = 1.0;
        } else {
            for (int j = 0; j < 4; j++) out[j] = v[j] / n;
        }
        out[4] = v[4];
        out[5] = v[5];
        out[6] = v[6];
    }

    private static long stateVal(Bits su2, Vals su8, Vals su16, Vals su32) {
        final int s = su2.get();
        return s == 0 ? 0 : s == 1 ? su8.get() : s == 2 ? su16.get() : su32.get();
    }

    private static long delta(Bits ti2, Bits ti4, Vals ti8, Vals ti16, Vals ti32) {
        final int s = ti2.get();
        if (s == 0) return 0;
        if (s == 1) return 1;
        if (s == 3) return -1;
        final int k = ti4.get();
        if (k != 0) return I4[k];
        long v = ti8.get();
        if (v == 0) {
            v = ti16.get();
            if (v == 0) v = ti32.get();
            return v;
        }
        if (v >= 1 && v <= 8) return v + 127;
        if (v >= -8 && v <= -1) return v - 128;
        return v;
    }

    private static boolean exhausted(Bits ti2, Bits ti4, Vals ti8, Vals ti16, Vals ti32, int l2, int l4, int l8, int l16, int l32) {
        return ti2.used() >= l2 && ti4.used() >= l4 && ti8.p >= l8 && ti16.p >= l16 && ti32.p >= l32;
    }

    // ------------------------------------------------------------------------------------------------ posing
    /** igb.sample_track: (quat, trans) at time t. */
    static double[][] sample(Track tr, double t) {
        final double[] times = tr.times;
        final int n = times.length;
        if (n == 1 || t <= times[0]) return new double[][] {tr.q[0], tr.tr[0]};
        if (t >= times[n - 1]) return new double[][] {tr.q[n - 1], tr.tr[n - 1]};
        // np.searchsorted(times, t, side="right") - 1
        int lo = 0, hi = n;
        while (lo < hi) {
            final int mid = (lo + hi) >>> 1;
            if (times[mid] <= t) lo = mid + 1;
            else hi = mid;
        }
        int i = lo - 1;
        i = Math.min(Math.max(i, 0), n - 2);
        final double span = times[i + 1] - times[i];
        final double f = span <= 0 ? 0.0 : (t - times[i]) / span;
        final double[] q0 = tr.q[i];
        double[] q1 = tr.q[i + 1].clone();
        if (Np.dot(q0, q1) < 0) for (int j = 0; j < 4; j++) q1[j] = -q1[j];
        final double[] qq = new double[4];
        for (int j = 0; j < 4; j++) qq[j] = q0[j] * (1 - f) + q1[j] * f;
        final double ln = Np.norm(qq);
        for (int j = 0; j < 4; j++) qq[j] = qq[j] / ln;
        final double[] t0 = tr.tr[i], t1 = tr.tr[i + 1];
        return new double[][] {qq, {t0[0] * (1 - f) + t1[0] * f, t0[1] * (1 - f) + t1[1] * f, t0[2] * (1 - f) + t1[2] * f}};
    }

    /** igb.quat_to_rowmat */
    static double[][] quatRows(double[] q) {
        final double x = q[0], y = q[1], z = q[2], w = q[3];
        return new double[][] {
            {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
            {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
            {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    }

    /** The skeleton's bind_local per bone (pose_world's W[i] @ inv(W[p]), or W[i] for a root). */
    static double[][][] bindLocal(IgbModel.Skel sk) {
        synchronized (sk) {
            if (sk.bindLocal == null) {
                final int n = sk.names.length;
                final double[][][] bl = new double[n][][];
                for (int i = 0; i < n; i++) {
                    final int p = sk.parents[i];
                    bl[i] = p >= 0 && p < i ? Np.mm(sk.world[i], Np.inv(sk.world[p])) : sk.world[i];
                }
                sk.bindLocal = bl;
            }
            return sk.bindLocal;
        }
    }

    /** igb.pose_world: every bone's world matrix (row vectors) at time t of a clip. */
    public static double[][][] poseWorld(IgbModel.Skel sk, Map<String, Track> tracks, double t) {
        final int n = sk.names.length;
        final double[][][] bl = bindLocal(sk);
        final double[][][] out = new double[n][][];
        for (int i = 0; i < n; i++) {
            final int p = sk.parents[i];
            final double[][] pw = p >= 0 && p < i ? out[p] : null;
            final Track trk = tracks.get(sk.names[i]);
            double[][] local;
            if (trk == null) {
                local = bl[i];
            } else {
                final double[][] s = sample(trk, t);
                local = Np.eye4();
                final double[][] r = quatRows(s[0]);
                for (int a = 0; a < 3; a++) System.arraycopy(r[a], 0, local[a], 0, 3);
                if (trk.moves) System.arraycopy(s[1], 0, local[3], 0, 3);
                else System.arraycopy(bl[i][3], 0, local[3], 0, 3);
            }
            out[i] = Np.mm(local, pw != null ? pw : Np.eye4());
        }
        return out;
    }

    /** All igAnimation names of a set, in order. */
    public static List<String> names(Igb ag) {
        final List<String> out = new java.util.ArrayList<>();
        for (Igb.Obj an : ag.ofType("igAnimation")) out.add(an.s(2));
        return out;
    }
}
