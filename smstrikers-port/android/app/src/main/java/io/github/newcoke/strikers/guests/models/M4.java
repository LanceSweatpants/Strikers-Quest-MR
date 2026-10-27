package io.github.newcoke.strikers.guests.models;

/** 4 x 4 matrices as melee2obj.py / meleepack.py work them (column vectors, Python's sum for the products). */
final class M4 {
    private M4() {}

    static double[][] ident() {
        return new double[][] {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    }

    static double[][] mul(double[][] a, double[][] b) {
        final double[][] r = new double[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                r[i][j] = Py.sum4(a[i][0] * b[0][j], a[i][1] * b[1][j], a[i][2] * b[2][j], a[i][3] * b[3][j]);
        return r;
    }

    /** hsd.mat_vec: (m * (v, w)).xyz, left to right. */
    static double[] vec(double[][] m, double[] v, double w) {
        final double[] r = new double[3];
        for (int i = 0; i < 3; i++) r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] + m[i][3] * w;
        return r;
    }

    static double[] vec(double[][] m, double[] v) {
        return vec(m, v, 1.0);
    }

    /** hsd.srt: scale, rotation (R = Rz * Ry * Rx), translation. */
    static double[][] srt(double[] s, double[] r, double[] t) {
        final double cx = Trig.cos(r[0]), sx = Trig.sin(r[0]);
        final double cy = Trig.cos(r[1]), sy = Trig.sin(r[1]);
        final double cz = Trig.cos(r[2]), sz = Trig.sin(r[2]);
        final double r00 = cy * cz, r01 = sx * sy * cz - cx * sz, r02 = cx * sy * cz + sx * sz;
        final double r10 = cy * sz, r11 = sx * sy * sz + cx * cz, r12 = cx * sy * sz - sx * cz;
        final double r20 = -sy, r21 = sx * cy, r22 = cx * cy;
        return new double[][] {
            {r00 * s[0], r01 * s[1], r02 * s[2], t[0]},
            {r10 * s[0], r11 * s[1], r12 * s[2], t[1]},
            {r20 * s[0], r21 * s[1], r22 * s[2], t[2]},
            {0, 0, 0, 1}};
    }

    /** meleepack.inv4: Gauss-Jordan with partial pivoting, exactly as the Python. */
    static double[][] inv(double[][] m) {
        final double[][] a = new double[4][8];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) a[i][j] = m[i][j];
            for (int j = 0; j < 4; j++) a[i][4 + j] = i == j ? 1.0 : 0.0;
        }
        for (int c = 0; c < 4; c++) {
            int p = c;
            double best = Math.abs(a[c][c]);
            for (int r = c + 1; r < 4; r++) {
                final double v = Math.abs(a[r][c]);
                if (v > best) {
                    best = v;
                    p = r;
                }
            }
            final double[] tmp = a[c];
            a[c] = a[p];
            a[p] = tmp;
            double pv = a[c][c];
            if (pv == 0.0) pv = 1e-12;
            for (int k = 0; k < 8; k++) a[c][k] = a[c][k] / pv;
            for (int r = 0; r < 4; r++) {
                if (r == c) continue;
                final double f = a[r][c];
                for (int k = 0; k < 8; k++) a[r][k] = a[r][k] - f * a[c][k];
            }
        }
        final double[][] out = new double[4][4];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out[i][j] = a[i][4 + j];
        return out;
    }

    static double[][] copy(double[][] m) {
        final double[][] r = new double[4][];
        for (int i = 0; i < 4; i++) r[i] = m[i].clone();
        return r;
    }

    /** meleepack.near */
    static boolean near(double[][] a, double[][] b) {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) if (!(Math.abs(a[r][c] - b[r][c]) < 1e-4)) return false;
        return true;
    }
}
