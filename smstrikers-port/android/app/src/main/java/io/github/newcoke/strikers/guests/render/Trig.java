package io.github.newcoke.strikers.guests.render;

/** sin / cos for the clips' Z-only turns (fdlibm: the same on every device). */
final class Trig {
    private Trig() {}

    static double sin(double a) {
        return StrictMath.sin(a);
    }

    static double cos(double a) {
        return StrictMath.cos(a);
    }
}
