package io.github.newcoke.strikers.guests.mua;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * math.sin / cos / atan2 as the PC tools got them (the PC's C library). Java's StrictMath (fdlibm) gives the same
 * value but for the last bit now and then; where the PC's differs for an argument these files hand it, our table
 * (guestdata/mua/libm.txt: function, the argument's bits, the PC's result's bits - numbers only) says so.
 */
public final class Libm {
    private Libm() {}

    private static final Map<String, Long> FIX = new HashMap<>();
    /** PC data tool: every (function, arguments) asked for, as "fn a [b]" in hex bits; null = off. */
    public static volatile Set<String> record;

    public static synchronized void load(String text) {
        FIX.clear();
        if (text == null) return;
        for (String line : text.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] f = l.split("\\s+");
            // fn argbits [argbits2] resultbits
            final String key = f.length == 4 ? f[0] + " " + f[1] + " " + f[2] : f[0] + " " + f[1];
            FIX.put(key, Long.parseUnsignedLong(f[f.length - 1], 16));
        }
    }

    /** r135: more lines on top of the loaded table (guestdata/mua/libm_npc.txt, the NPC guests'; load() clears it). */
    public static synchronized void add(String text) {
        if (text == null) return;
        for (String line : text.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] f = l.split("\\s+");
            final String key = f.length == 4 ? f[0] + " " + f[1] + " " + f[2] : f[0] + " " + f[1];
            FIX.put(key, Long.parseUnsignedLong(f[f.length - 1], 16));
        }
    }

    private static double fixed(String key, double v) {
        final Set<String> rec = record;
        if (rec != null) synchronized (rec) {
            rec.add(key + " " + hex(v));  // (with fdlibm's own result: the data tool keeps where the PC's differs)
        }
        final Long b = FIX.get(key);
        return b != null ? Double.longBitsToDouble(b) : v;
    }

    private static String hex(double x) {
        return Long.toHexString(Double.doubleToRawLongBits(x));
    }

    public static double sin(double x) {
        return fixed("sin " + hex(x), StrictMath.sin(x));
    }

    public static double cos(double x) {
        return fixed("cos " + hex(x), StrictMath.cos(x));
    }

    public static double atan2(double y, double x) {
        return fixed("atan2 " + hex(y) + " " + hex(x), StrictMath.atan2(y, x));
    }

    /** math.radians */
    public static double radians(double deg) {
        return deg * (Math.PI / 180.0);
    }
}
