package io.github.newcoke.strikers.guests.models;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * The PC tools are Python (tools/outside/meleepack.py & co.); the packs on Lance's headset are their output. To make
 * the same bytes here, a few of Python's number rules are copied exactly:
 *  - sum() over floats is Neumaier-compensated (CPython 3.12+, ints in a float sum too since 3.14), not left to right;
 *  - round(x, n) rounds the exact binary value to n decimals, ties to even (only used as dictionary keys here);
 *  - float % is the floored modulo (sign of the divisor);
 *  - math.hypot is CPython's vector_norm (error-free squares, one correction step).
 * pow / exp are StrictMath (fdlibm: the same on the PC and the headset; the PC's C library differs from it in the last
 * bit now and then, which no pack showed); sin / cos too, nudged to the PC's last bits by Trig (libm.txt).
 */
final class Py {
    private Py() {}

    /** sum(items) of floats: Neumaier compensated, as CPython's builtin sum. */
    static double sum(double[] v, int n) {
        double hi = 0.0, lo = 0.0;
        for (int i = 0; i < n; i++) {
            final double x = v[i];
            final double t = hi + x;
            if (Math.abs(hi) >= Math.abs(x)) lo += (hi - t) + x;
            else lo += (x - t) + hi;
            hi = t;
        }
        if (lo != 0.0 && !Double.isInfinite(lo) && !Double.isNaN(lo)) return hi + lo;
        return hi;
    }

    static double sum3(double a, double b, double c) {
        double hi = 0.0, lo = 0.0;
        for (int i = 0; i < 3; i++) {
            final double x = i == 0 ? a : i == 1 ? b : c;
            final double t = hi + x;
            if (Math.abs(hi) >= Math.abs(x)) lo += (hi - t) + x;
            else lo += (x - t) + hi;
            hi = t;
        }
        if (lo != 0.0 && !Double.isInfinite(lo) && !Double.isNaN(lo)) return hi + lo;
        return hi;
    }

    static double sum4(double a, double b, double c, double d) {
        double hi = 0.0, lo = 0.0;
        for (int i = 0; i < 4; i++) {
            final double x = i == 0 ? a : i == 1 ? b : i == 2 ? c : d;
            final double t = hi + x;
            if (Math.abs(hi) >= Math.abs(x)) lo += (hi - t) + x;
            else lo += (x - t) + hi;
            hi = t;
        }
        if (lo != 0.0 && !Double.isInfinite(lo) && !Double.isNaN(lo)) return hi + lo;
        return hi;
    }


    /** round(x, nd) as a key: the decimal it rounds to, times 10^nd (equal keys = equal Python results). */
    static long rkey(double x, int nd) {
        final double y = x * (nd == 5 ? 1e5 : nd == 4 ? 1e4 : Math.pow(10, nd));
        if (Math.abs(y) < 1e9) {
            final double fl = Math.floor(y);
            final double fr = y - fl;
            if (Math.abs(fr - 0.5) > 1e-6) return (long) (fr < 0.5 ? fl : fl + 1.0);
        }
        return new BigDecimal(x).setScale(nd, RoundingMode.HALF_EVEN).unscaledValue().longValue();
    }

    /** Python's float x % m. */
    static double mod(double x, double m) {
        double r = x % m;
        if (r != 0.0) {
            if ((m < 0) != (r < 0)) r += m;
        } else {
            r = Math.copySign(0.0, m);
        }
        return r;
    }


    /** "%.3f" % x (and other places), exact binary value rounded half to even, as CPython. */
    static String fixed(double x, int places) {
        String s = new BigDecimal(x).setScale(places, RoundingMode.HALF_EVEN).toPlainString();
        if (x < 0 || (x == 0 && 1 / x < 0)) {
            if (!s.startsWith("-")) s = "-" + s;  // Python keeps the sign of a negative value rounding to 0
        }
        return s;
    }

    /** float(text) */
    static double num(String s) {
        return Double.parseDouble(s.trim());
    }

    /** nlStringLowerHash (meleepack.lower_hash). */
    static int lowerHash(String s) {
        int h = 0xFFFFFFFF;
        for (byte b : s.toLowerCase(Locale.ROOT).getBytes(StandardCharsets.UTF_8)) h = h + (h << 5) + (b & 0xFF);
        return h;
    }


    static int floor(double x) {
        return (int) Math.floor(x);
    }

    static int ceil(double x) {
        return (int) Math.ceil(x);
    }

    /** math.hypot(x, y): CPython's vector_norm (Dekker products in place of fma: the same exact values). */
    static double hypot(double x, double y) {
        x = Math.abs(x);
        y = Math.abs(y);
        if (Double.isInfinite(x) || Double.isInfinite(y)) return Double.POSITIVE_INFINITY;
        if (Double.isNaN(x) || Double.isNaN(y)) return Double.NaN;
        final double max = Math.max(x, y);
        if (max == 0.0) return max;
        final int maxE = Math.getExponent(max) + 1;  // frexp: max = m * 2^e, 0.5 <= m < 1
        if (maxE < -1023) {
            final double dm = Double.MIN_NORMAL;
            return dm * hypot(x / dm, y / dm);
        }
        final double scale = Math.scalb(1.0, -maxE);
        double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
        for (int i = 0; i < 2; i++) {
            double v = (i == 0 ? x : y) * scale;
            final double ph = v * v, pl = twoProdLo(v, v, ph);
            final double sh = csum + ph, sl = (csum - sh) + ph;  // dl_fast_sum (|csum| >= |ph|)
            csum = sh;
            frac1 += pl;
            frac2 += sl;
        }
        double h = Math.sqrt(csum - 1.0 + (frac1 + frac2));
        final double ph = -h * h, pl = twoProdLo(-h, h, ph);
        final double sh = csum + ph, sl = (csum - sh) + ph;
        csum = sh;
        frac1 += pl;
        frac2 += sl;
        final double xx = csum - 1.0 + (frac1 + frac2);
        h += xx / (2.0 * h);
        return h / scale;
    }

    /** The exact error of a * b (= p): Dekker's split. */
    private static double twoProdLo(double a, double b, double p) {
        final double s = 134217729.0;  // 2^27 + 1
        double t = s * a;
        final double ah = t - (t - a), al = a - ah;
        t = s * b;
        final double bh = t - (t - b), bl = b - bh;
        return ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    }

    /** CPython's set of non-negative ints, only added to: its iteration order (slot order). */
    static final class IntSet {
        private int[] keys = new int[8];
        private boolean[] full = new boolean[8];
        private int mask = 7, fill;

        int size() {
            return fill;
        }

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

        /** The members in Python's iteration order. */
        int[] toArray() {
            final int[] out = new int[fill];
            int n = 0;
            for (int s = 0; s <= mask; s++) if (full[s]) out[n++] = keys[s];
            return out;
        }
    }

    /** Loop subdivision's beta(n), the PC's own values (its pow / cos differ from fdlibm in the last bit). */
    static double loopBeta(int n) {
        if (n >= 1 && n <= BETA.length) return BETA[n - 1];
        final double c = 0.375 + 0.25 * StrictMath.cos(2 * Math.PI / n);
        return (0.625 - c * c) / n;
    }

    private static final String BETA_HEX =
            "0x1.e000000000000p-3 0x1.3800000000000p-2 0x1.8000000000000p-3 0x1.f000000000000p-4 " +
            "0x1.58722191a02d6p-4 0x1.0000000000000p-4 0x1.919cb59d1c909p-5 0x1.483c499904994p-5 " +
            "0x1.14c451633a01ap-5 0x1.de0c5dfe26e56p-6 0x1.a4a25dacb877dp-6 0x1.77a17e1292effp-6 " +
            "0x1.5376be5037e45p-6 0x1.35c915de7fe41p-6 0x1.1cffe3879aa0cp-6 0x1.07fcdabe51748p-6 " +
            "0x1.ebe412c28874fp-7 0x1.cc8fa170783aep-7 0x1.b1188228c29a5p-7 0x1.98d0531ae3488p-7 " +
            "0x1.832f728e4119ep-7 0x1.6fcad7029be4ap-7 0x1.5e4ceb55ba47fp-7 0x1.4e7071e5444bdp-7 " +
            "0x1.3ffccb891b8d6p-7 0x1.32c336fd5a3c6p-7 0x1.269cc0a49b051p-7 0x1.1b68b1626cc39p-7 " +
            "0x1.110b5a5ab42e3p-7 0x1.076d2579c085dp-7 0x1.fcf3b311013ddp-8 0x1.ec400aa2028d4p-8 " +
            "0x1.dca11158dcc95p-8 0x1.cdfc69635fbd8p-8 0x1.c03afbde8be00p-8 0x1.b34879af62125p-8 " +
            "0x1.a712f2e0bea95p-8 0x1.9b8a7fff92de7p-8 0x1.90a0f9f18c2f5p-8 0x1.8649bd85c6e7bp-8 " +
            "0x1.7c79789432d94p-8 0x1.7325fef2cdc72p-8 0x1.6a4625e071658p-8 0x1.61d1a4ca01a00p-8 " +
            "0x1.59c0fa84f7854p-8 0x1.520d56450f2eap-8 0x1.4ab083b4dd6eap-8 0x1.43a4d9b43bbc5p-8 " +
            "0x1.3ce52b554bbe1p-8 0x1.366cbac274371p-8 0x1.30372dc607d3fp-8 0x1.2a4083b7f1a5ap-8 " +
            "0x1.24850c9f52bdep-8 0x1.1f01615dda8ebp-8 0x1.19b25cc1242b1p-8 0x1.1495155bc794fp-8 " +
            "0x1.0fa6d80c4c855p-8 0x1.0ae5231bedba0p-8 0x1.064da1e2429f9p-8 0x1.01de28dd8c563p-8 " +
            "0x1.fb2964634172cp-9 0x1.f2deb502a83f6p-9 0x1.ead8bc2fbf3ecp-9 0x1.e3142cb24388ap-9 ";
    private static final double[] BETA = parseHex(BETA_HEX);

    private static double[] parseHex(String s) {
        final String[] p = s.trim().split(" ");
        final double[] out = new double[p.length];
        for (int i = 0; i < p.length; i++) out[i] = Double.parseDouble(p[i]);
        return out;
    }
}
