package io.github.newcoke.strikers.guests.charged;

import java.math.BigDecimal;

/**
 * a * b + c rounded once (IEEE fused multiply-add), as the PC's numpy / OpenBLAS (Haswell kernels) compute matrix
 * products and dot products: each sum a chain of fused multiply-adds in index order (checked bit for bit against
 * numpy on the PC). Math.fma where the runtime has it (Java 9+, Android API 33+), else the same exact result through
 * BigDecimal (OpenJDK's own fallback for it).
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

    /** The exponents the emulation is exact for (no overflow in the split, no underflow in the error terms). */
    static boolean fastRange(double a, double b, double c) {
        if (a == 0.0 || b == 0.0) return true;
        final int ea = Math.getExponent(a), eb = Math.getExponent(b);
        if (ea < -400 || ea > 400 || eb < -400 || eb > 400) return false;
        if (c == 0.0) return true;
        final int ec = Math.getExponent(c);
        return ec > -800 && ec < 800;
    }

    static double fma(double a, double b, double c) {
        // r132: Android's Math.fma is a slow software routine on the Quest (the r131 headset run sat for minutes on one
        // figure): the exact double-double emulation (mua.Np.fma, Boldo-Melquiond round-to-odd) in the range it holds
        if (fastRange(a, b, c)) return io.github.newcoke.strikers.guests.mua.Np.fma(a, b, c);
        if (NATIVE) return Math.fma(a, b, c);
        if (a == 0.0 || b == 0.0 || c == 0.0 || Double.isNaN(a) || Double.isNaN(b) || Double.isNaN(c)
                || Double.isInfinite(a) || Double.isInfinite(b) || Double.isInfinite(c))
            return a * b + c;  // (no rounding of the product to lose: exact zero products / zero addends)
        return new BigDecimal(a).multiply(new BigDecimal(b)).add(new BigDecimal(c)).doubleValue();
    }
}
