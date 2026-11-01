package io.github.newcoke.strikers.guests.models;

import java.math.BigInteger;

/**
 * a * b + c rounded once (IEEE fused multiply-add), as the PC's numpy / OpenBLAS kernels sum products (Np). Math.fma
 * where the runtime has it (Java 9+, Android API 33+), else the exact sum in integers rounded to the nearest double
 * (ties to even) here: the same bits either way.
 */
final class Fma {
    private Fma() {}

    private static final boolean NATIVE = probe();

    private static boolean probe() {
        try {
            return Math.fma(0.1, 10.0, -1.0) == 5.551115123125783E-17;
        } catch (Throwable t) {  // NoSuchMethodError on older Android
            return false;
        }
    }

    static boolean fastRange(double a, double b, double c) {
        if (a == 0.0 || b == 0.0) return true;
        final int ea = Math.getExponent(a), eb = Math.getExponent(b);
        if (ea < -400 || ea > 400 || eb < -400 || eb > 400) return false;
        if (c == 0.0) return true;
        final int ec = Math.getExponent(c);
        return ec > -800 && ec < 800;
    }

    static double fma(double a, double b, double c) {
        // r132: Android's Math.fma is a slow software routine on the Quest: the exact double-double emulation
        // (mua.Np.fma, Boldo-Melquiond round-to-odd) in the range it holds, else the exact integer sum below
        if (fastRange(a, b, c)) return io.github.newcoke.strikers.guests.mua.Np.fma(a, b, c);
        if (NATIVE) return Math.fma(a, b, c);
        return exact(a, b, c);
    }

    /** (also the PC check of the fallback against Math.fma) */
    static double exact(double a, double b, double c) {
        if (Double.isNaN(a) || Double.isNaN(b) || Double.isNaN(c) || Double.isInfinite(a) || Double.isInfinite(b)
                || a == 0.0 || b == 0.0)
            return a * b + c;  // (an exact zero product, or no finite product: the plain sum is the fused one)
        if (Double.isInfinite(c)) return c;  // (a finite product never cancels it)
        final long ba = Double.doubleToRawLongBits(a), bb = Double.doubleToRawLongBits(b);
        BigInteger m = BigInteger.valueOf(mant(ba)).multiply(BigInteger.valueOf(mant(bb)));
        int e = expo(ba) + expo(bb);
        if ((ba < 0) != (bb < 0)) m = m.negate();
        if (c != 0.0) {
            final long bc = Double.doubleToRawLongBits(c);
            BigInteger mc = BigInteger.valueOf(mant(bc));
            if (bc < 0) mc = mc.negate();
            final int ec = expo(bc);
            final int e0 = Math.min(e, ec);
            m = m.shiftLeft(e - e0).add(mc.shiftLeft(ec - e0));
            e = e0;
        }
        if (m.signum() == 0) return 0.0;  // (an exact cancellation rounds to +0)
        return round(m, e);
    }

    private static long mant(long bits) {
        final int ex = (int) ((bits >>> 52) & 0x7FF);
        final long f = bits & 0xFFFFFFFFFFFFFL;
        return ex == 0 ? f : f | (1L << 52);
    }

    private static int expo(long bits) {
        final int ex = (int) ((bits >>> 52) & 0x7FF);
        return (ex == 0 ? 1 : ex) - 1075;
    }

    /** m * 2^e to the nearest double, ties to even. */
    private static double round(BigInteger m, int e) {
        final boolean neg = m.signum() < 0;
        BigInteger q = m.abs();
        final int bl = q.bitLength();
        final int lead = e + bl - 1;
        int shift = bl - 53;
        if (lead < -1022) shift += -1022 - lead;  // subnormal: fewer bits kept
        if (shift > 0) {
            final BigInteger kept = q.shiftRight(shift);
            final BigInteger rem = q.subtract(kept.shiftLeft(shift));
            final int cmp = rem.compareTo(BigInteger.ONE.shiftLeft(shift - 1));
            q = (cmp > 0 || (cmp == 0 && kept.testBit(0))) ? kept.add(BigInteger.ONE) : kept;
            e += shift;
        }
        final double r = Math.scalb(q.doubleValue(), e);
        return neg ? -r : r;
    }
}
