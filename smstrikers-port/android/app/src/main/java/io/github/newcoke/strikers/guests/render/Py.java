package io.github.newcoke.strikers.guests.render;

import java.math.BigDecimal;
import java.math.RoundingMode;

/**
 * The PC tools' (Python 3.15) arithmetic, number for number, so the poses come out bit-identical: doubles
 * throughout, sum() as CPython adds floats (Neumaier-compensated since 3.12), 4 x 4 row-vector matrices as
 * retargetsim.py's lists (mul, inverse), and '%.6f' as Python prints it.
 */
public final class Py {
    private Py() {}

    /** CPython's sum() of floats: the first added to int 0, the rest compensated (Neumaier). */
    public static double sum(double x0, double x1, double x2) {
        double f = 0.0 + x0, c = 0.0, t;
        t = f + x1;
        c += Math.abs(f) >= Math.abs(x1) ? (f - t) + x1 : (x1 - t) + f;
        f = t;
        t = f + x2;
        c += Math.abs(f) >= Math.abs(x2) ? (f - t) + x2 : (x2 - t) + f;
        f = t;
        if (c != 0.0 && !Double.isInfinite(c) && !Double.isNaN(c)) f += c;
        return f;
    }

    public static double sum(double x0, double x1, double x2, double x3) {
        double f = 0.0 + x0, c = 0.0, t;
        t = f + x1;
        c += Math.abs(f) >= Math.abs(x1) ? (f - t) + x1 : (x1 - t) + f;
        f = t;
        t = f + x2;
        c += Math.abs(f) >= Math.abs(x2) ? (f - t) + x2 : (x2 - t) + f;
        f = t;
        t = f + x3;
        c += Math.abs(f) >= Math.abs(x3) ? (f - t) + x3 : (x3 - t) + f;
        f = t;
        if (c != 0.0 && !Double.isInfinite(c) && !Double.isNaN(c)) f += c;
        return f;
    }

    public static double sum(double[] xs) {
        if (xs.length == 0) return 0.0;
        double f = 0.0 + xs[0], c = 0.0;
        for (int i = 1; i < xs.length; i++) {
            final double x = xs[i], t = f + x;
            c += Math.abs(f) >= Math.abs(x) ? (f - t) + x : (x - t) + f;
            f = t;
        }
        if (c != 0.0 && !Double.isInfinite(c) && !Double.isNaN(c)) f += c;
        return f;
    }

    /** 4 x 4 (rows of 4): a * b, each entry sum(a[i][k] * b[k][j]). */
    public static double[][] mul(double[][] a, double[][] b) {
        final double[][] r = new double[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                r[i][j] = sum(a[i][0] * b[0][j], a[i][1] * b[1][j], a[i][2] * b[2][j], a[i][3] * b[3][j]);
        return r;
    }

    public static double[][] mul3(double[][] a, double[][] b) {
        final double[][] r = new double[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r[i][j] = sum(a[i][0] * b[0][j], a[i][1] * b[1][j], a[i][2] * b[2][j]);
        return r;
    }

    public static double[][] transpose3(double[][] m) {
        final double[][] r = new double[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r[i][j] = m[j][i];
        return r;
    }

    public static double[][] ident() {
        return new double[][] {{1.0, 0, 0, 0}, {0, 1.0, 0, 0}, {0, 0, 1.0, 0}, {0, 0, 0, 1.0}};
    }

    /** The quarter turn about Z (retargetsim's rz90 / kBindToClip). */
    public static double[][] rz90() {
        return new double[][] {{0, 1, 0, 0}, {-1, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    }

    public static double[][] copy(double[][] m) {
        final double[][] r = new double[m.length][];
        for (int i = 0; i < m.length; i++) r[i] = m[i].clone();
        return r;
    }

    /** retargetsim.inverse: Gauss-Jordan with partial pivoting, as written there. */
    public static double[][] inverse(double[][] m) {
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
            final double[] rc = new double[8];
            for (int k = 0; k < 8; k++) rc[k] = a[c][k] / pv;
            a[c] = rc;
            for (int r = 0; r < 4; r++) {
                if (r == c) continue;
                final double f = a[r][c];
                final double[] nr = new double[8];
                for (int k = 0; k < 8; k++) nr[k] = a[r][k] - f * rc[k];
                a[r] = nr;
            }
        }
        final double[][] out = new double[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) out[i][j] = a[i][4 + j];
        return out;
    }

    /** retargetsim.quat_rows: the row-vector rotation of (x, y, z, w), normalised first. */
    public static double[][] quatRows(double x, double y, double z, double w) {
        double n = Math.sqrt(x * x + y * y + z * z + w * w);
        if (n == 0.0) n = 1.0;
        x /= n;
        y /= n;
        z /= n;
        w /= n;
        return new double[][] {
            {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0},
            {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0},
            {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0},
            {0.0, 0.0, 0.0, 1.0}};
    }

    /** retargetsim.rot_of: rows 0-2 made unit length (rot[3][3]); out[0] = row 0's length. */
    public static double[][] rotOf(double[][] m, double[] len0) {
        final double[][] r = new double[3][3];
        for (int row = 0; row < 3; row++) {
            final double a = m[row][0], b = m[row][1], c = m[row][2];
            final double ln = Math.sqrt(sum(a * a, b * b, c * c));
            if (row == 0 && len0 != null) len0[0] = ln;
            if (ln > 1e-12) {
                r[row][0] = a / ln;
                r[row][1] = b / ln;
                r[row][2] = c / ln;
            }
        }
        return r;
    }

    /** nlStringLowerHash (shier.lower_hash). */
    public static int lowerHash(String s) {
        int h = 0xFFFFFFFF;
        final byte[] b = s.toLowerCase(java.util.Locale.ROOT).getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for (byte ch : b) h = h + (h << 5) + (ch & 0xFF);
        return h;
    }

    /** Python's '%.6f' (correctly rounded, the sign kept on a negative zero). */
    public static String f6(double v) {
        final boolean neg = v < 0 || (v == 0.0 && 1.0 / v < 0);
        final String s = new BigDecimal(Math.abs(v)).setScale(6, RoundingMode.HALF_EVEN).toPlainString();
        return neg ? "-" + s : s;
    }
}
