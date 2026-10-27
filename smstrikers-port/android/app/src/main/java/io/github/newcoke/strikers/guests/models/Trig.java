package io.github.newcoke.strikers.guests.models;

import java.util.HashMap;
import java.util.Map;

/**
 * cos / sin as the PC tools got them: the PC's C library (Python's math.cos / sin) and fdlibm (StrictMath) differ in
 * the last bit for about 1 argument in 70, and a last bit can reach a pack (a bind matrix's near-zero entry). The fixed
 * angles our own code uses (gas puffs' turns, the flame's squares, Mankey's yaw) are Python's own values below; the
 * files' joint angles are fdlibm's, nudged by libm.txt where the PC's differ (keyed by a hash of the argument).
 */
final class Trig {
    private Trig() {}

    private static final Map<Long, double[]> KNOWN = new HashMap<>();

    static {
        final String t = "0x0.0p+0 0x1.0000000000000p+0 0x0.0p+0 0x1.3333333333333p-1 0x1.a69263c485b15p-1 0x1.2118d17a54159p-1 "
                + "0x1.4cccccccccccdp+0 0x1.11eb3682a4c5fp-2 0x1.ed577f9c51e4bp-1 0x1.0cccccccccccdp+1 -0x1.027b304989ecap-1 "
                + "0x1.b9f693feb72fbp-1 0x1.6666666666666p+1 -0x1.e26af7757704ap-1 0x1.57072235de5c8p-2 0x1.ccccccccccccdp+1 "
                + "-0x1.cb23eb4d22c59p-1 -0x1.c524143f0d1f1p-2 0x1.199999999999ap+2 -0x1.3ab577c62c2aap-2 -0x1.e7386314528f0p-1 "
                + "-0x1.921fb54442d18p-1 0x1.6a09e667f3bcdp-1 -0x1.6a09e667f3bcdp-1 0x0.0p+0 0x1.0000000000000p+0 0x0.0p+0 "
                + "0x1.657184ae74487p-2 0x1.e11f642522d1cp-1 0x1.5e3a8748a0bf5p-2 0x1.38c35418a5bf6p-1 0x1.a367e59158747p-1 "
                + "0x1.25abcf87c4978p-1 0x1.657184ae74487p-1 0x1.8836fa2cf5039p-1 0x1.491b7523c161cp-1 0x1.eb7c166fdfe3ap-1 "
                + "0x1.25abcf87c4978p-1 0x1.a367e59158747p-1 0x1.0c152382d7365p+0 0x1.0000000000001p-1 0x1.bb67ae8584caap-1 "
                + "0x1.38c35418a5bf6p+0 0x1.5e3a8748a0bf7p-2 0x1.e11f642522d1bp-1 0x1.4f1a6c638d03fp+0 0x1.0907dc1930690p-2 "
                + "0x1.ee8dd4748bf15p-1 0x1.657184ae74487p+0 0x1.63a1a7e0b738cp-3 0x1.f838b8c811c17p-1 0x1.921fb54442d18p+0 "
                + "0x1.1a62633145c07p-54 0x1.0000000000000p+0 0x1.a876cd8f2a161p+0 -0x1.64fd6b8c28107p-4 0x1.fe0d3b41815a2p-1 "
                + "0x1.becde5da115a9p+0 -0x1.63a1a7e0b7388p-3 0x1.f838b8c811c17p-1 0x1.eb7c166fdfe3ap+0 -0x1.5e3a8748a0bf5p-2 "
                + "0x1.e11f642522d1cp-1 0x1.00e9975d63941p+1 -0x1.b0c2d77379851p-2 0x1.d0079302dd768p-1 0x1.0c152382d7365p+1 "
                + "-0x1.ffffffffffffcp-2 0x1.bb67ae8584cabp-1 0x1.226c3bcdbe7aep+1 -0x1.491b7523c161dp-1 0x1.8836fa2cf5039p-1 "
                + "0x1.2d97c7f3321d2p+1 -0x1.6a09e667f3bccp-1 0x1.6a09e667f3bcdp-1 0x1.38c35418a5bf6p+1 -0x1.8836fa2cf5038p-1 "
                + "0x1.491b7523c161ep-1 0x1.4f1a6c638d03fp+1 -0x1.bb67ae8584cabp-1 0x1.fffffffffffffp-2 0x1.5a45f88900a63p+1 "
                + "-0x1.d0079302dd767p-1 0x1.b0c2d77379854p-2 0x1.657184ae74487p+1 -0x1.e11f642522d1bp-1 0x1.5e3a8748a0bf8p-2 "
                + "0x1.7bc89cf95b8d0p+1 -0x1.f838b8c811c17p-1 0x1.63a1a7e0b7387p-3 0x1.86f4291ecf2f4p+1 -0x1.fe0d3b41815a2p-1 "
                + "0x1.64fd6b8c28104p-4 0x1.a876cd8f2a161p+1 -0x1.f838b8c811c17p-1 -0x1.63a1a7e0b738ep-3 0x1.b3a259b49db85p+1 "
                + "-0x1.ee8dd4748bf15p-1 -0x1.0907dc1930691p-2 0x1.d524fe24f89f2p+1 -0x1.bb67ae8584caap-1 -0x1.0000000000001p-1 "
                + "0x1.00e9975d63941p+2 -0x1.491b7523c161ep-1 -0x1.8836fa2cf5038p-1";
        final String[] p = t.split(" ");
        for (int i = 0; i + 2 < p.length; i += 3) {
            final double a = Double.parseDouble(p[i]);
            KNOWN.put(Double.doubleToLongBits(a), new double[] {Double.parseDouble(p[i + 1]), Double.parseDouble(p[i + 2])});
        }
    }

    /** math.radians */
    static double radians(double deg) {
        return deg * (Math.PI / 180.0);
    }

    static double cos(double x) {
        if (x == 0.0) return 1.0;
        final double[] k = KNOWN.get(Double.doubleToLongBits(x));
        if (k != null) return k[0];
        if (record != null) record.add(Double.doubleToLongBits(x));
        return fix(StrictMath.cos(x), FIX_COS, x);
    }

    static double sin(double x) {
        if (x == 0.0) return x;  // sin(+-0) = +-0
        final double[] k = KNOWN.get(Double.doubleToLongBits(x));
        if (k != null) return k[1];
        if (record != null) record.add(Double.doubleToLongBits(x));
        return fix(StrictMath.sin(x), FIX_SIN, x);
    }

    /** (the PC data tool: every argument that reaches fdlibm) */
    static volatile java.util.Set<Long> record;

    // ------------------------------------------------------------------------------------------------ libm.txt
    /*
     * The PC's C library (Python's math.cos / sin) and fdlibm round the last bit differently for about 1 argument in
     * 70. libm.txt lists, for the joint angles in the roster's files where they differ, the hash of the argument and
     * which way the PC rounded (a number of ulps): no angles, just which last bits to nudge.
     */
    private static volatile Map<Integer, Integer> FIX_COS = new HashMap<>(), FIX_SIN = new HashMap<>();

    static int hash(double x) {
        final long b = Double.doubleToLongBits(x);
        return (int) ((b * 0x9E3779B97F4A7C15L) >>> 32);
    }

    private static double fix(double v, Map<Integer, Integer> m, double x) {
        if (m.isEmpty()) return v;
        final Integer d = m.get(hash(x));
        if (d == null) return v;
        for (int i = 0; i < Math.abs(d); i++) v = d > 0 ? Math.nextUp(v) : Math.nextDown(v);
        return v;
    }

    /** libm.txt: "c|s <hash hex> <ulps>" lines. */
    static synchronized void load(String text) {
        if (text.equals(loaded)) return;
        final Map<Integer, Integer> c = new HashMap<>(), s = new HashMap<>();
        for (String line : text.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] f = l.split("\\s+");
            final int h = (int) Long.parseLong(f[1], 16);
            final int d = Integer.parseInt(f[2]);
            (f[0].equals("c") ? c : s).put(h, d);
        }
        FIX_COS = c;  // (whole maps swapped in: a conversion running on another thread never sees half a table)
        FIX_SIN = s;
        loaded = text;
    }

    private static String loaded;

}
