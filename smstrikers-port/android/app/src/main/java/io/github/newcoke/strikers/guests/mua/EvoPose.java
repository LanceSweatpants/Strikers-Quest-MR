package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

/**
 * build_costumes.gev_from_pack: a hero pack's own_idle clip as a GEV1 loop of world matrices (xr_evo: the select
 * screen's standing figure and the 3D costume unlock), Python's own sums (compensated) over the pack's floats.
 */
final class EvoPose {
    private EvoPose() {}

    /** The GEV1 file, or null when the pack has no own_idle. */
    static byte[] gev(byte[] b) {
        final HeroCels.PackRead pr = new HeroCels.PackRead(b);
        final int nn = pr.nn;
        int at = -1, keys = 0;
        for (int i = 0; i < pr.clipNames.size(); i++) {
            if (pr.clipNames.get(i).equals("own_idle")) {
                at = pr.clipOff.get(i) + 52;
                keys = HeroCels.PackRead.le32(b, pr.clipOff.get(i) + 48);
            }
        }
        if (at < 0) return null;
        final Out o = new Out();
        o.bytes("GEV1".getBytes(java.nio.charset.StandardCharsets.ISO_8859_1)).le32(nn).le32(keys).lef32(30.0f);
        final double[][][] world = new double[nn][][];
        for (int k = 0; k < keys; k++) {
            for (int i = 0; i < nn; i++) {
                final int par = pr.parents[i];
                if (par < 0 || par >= i) {
                    world[i] = new double[][] {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
                } else {
                    final int q0 = at + (k * nn + i) * 28;
                    final double[] q = new double[7];
                    for (int j = 0; j < 7; j++) q[j] = Float.intBitsToFloat(HeroCels.PackRead.le32(b, q0 + 4 * j));
                    world[i] = mul(local(q), world[par]);
                }
            }
            for (double[][] m : world)
                for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) o.lef32((float) m[r][c]);
        }
        return o.toBytes();
    }

    private static double[][] local(double[] q) {
        final double x = q[0], y = q[1], z = q[2], w = q[3];
        return new double[][] {
            {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0},
            {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0},
            {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0},
            {q[4], q[5], q[6], 1}};
    }

    /** [[sum(a[i][k] * c[k][j] for k in range(4)) ...]]: CPython's sum (the first added to 0, compensated). */
    private static double[][] mul(double[][] a, double[][] c) {
        final double[][] r = new double[4][4];
        final double[] t = new double[4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                for (int k = 0; k < 4; k++) t[k] = a[i][k] * c[k][j];
                r[i][j] = Np.pySum(t, 4);
            }
        return r;
    }
}
