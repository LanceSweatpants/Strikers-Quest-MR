package io.github.newcoke.strikers.guests.mua;

/**
 * The MUA tools (tools/outside/mua: igb.py, mua2gpk.py, mua_cels.py, build_home.py) do their sums in numpy on the PC,
 * which hands small matrix work to OpenBLAS (its Skylake-X kernels on Lance's PC). To make the same bytes here, the
 * exact arithmetic of those kernels, worked out by comparing them bit for bit (signed zeros too) with exact models:
 * <ul>
 * <li>a matrix product (A @ B, vector @ matrix, np.dot of two vectors): each entry a fused multiply-add chain from
 * +0 in k order (acc = fma(a[k], b[k], acc));</li>
 * <li>np.linalg.inv (LAPACK dgesv on the identity as OpenBLAS runs it for a 4 x 4): getf2's left-looking LU with
 * partial pivoting (fma dot chains, multipliers times 1 / pivot), then the two triangular solves (each update
 * fma(-x, l, c), the diagonal as times 1 / u);</li>
 * <li>np.cross: products then differences, no fusing (numpy's own loops).</li>
 * </ul>
 * fma is exact in software (Boldo and Melquiond's emulation with round-to-odd), the same on the PC and the headset.
 */
public final class Np {
    private Np() {}

    // ------------------------------------------------------------------------------------------------ fma
    /** a * b + c rounded once (IEEE fusedMultiplyAdd), for finite values in the range these models use. */
    public static double fma(double a, double b, double c) {
        if (a == 0.0 || b == 0.0) return a * b + c;  // (exact product: signed zeros as IEEE)
        final double p = a * b;
        if (Double.isInfinite(p) || Double.isNaN(p) || Double.isNaN(c) || Double.isInfinite(c)) return p + c;
        // p + e = a * b exactly (Dekker)
        final double sp = 134217729.0;  // 2^27 + 1
        double t = sp * a;
        final double ah = t - (t - a), al = a - ah;
        t = sp * b;
        final double bh = t - (t - b), bl = b - bh;
        final double e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
        // s + u = p + c exactly (TwoSum)
        final double s = p + c;
        final double bb = s - p;
        final double u = (p - (s - bb)) + (c - bb);
        // v = u + e rounded to odd
        double v = u + e;
        final double vb = v - u;
        final double ve = (u - (v - vb)) + (e - vb);
        if (ve != 0.0 && (Double.doubleToRawLongBits(v) & 1L) == 0L) v = ve > 0 ? Math.nextUp(v) : Math.nextDown(v);
        final double r = s + v;
        return r == 0.0 ? 0.0 : r;
    }

    // ------------------------------------------------------------------------------------------------ products
    /** A (n x m) @ B (m x p), dgemm's fma chains. */
    public static double[][] mm(double[][] a, double[][] b) {
        final int n = a.length, m = b.length, p = b[0].length;
        final double[][] r = new double[n][p];
        for (int i = 0; i < n; i++) {
            final double[] ai = a[i];
            for (int j = 0; j < p; j++) {
                double acc = 0.0;
                for (int k = 0; k < m; k++) acc = fma(ai[k], b[k][j], acc);
                r[i][j] = acc;
            }
        }
        return r;
    }

    /** v (m) @ B (m x p): gemv, an fma chain per entry. */
    public static double[] vm(double[] v, double[][] b) {
        final int m = b.length, p = b[0].length;
        final double[] r = new double[p];
        for (int j = 0; j < p; j++) {
            double acc = 0.0;
            for (int k = 0; k < m; k++) acc = fma(v[k], b[k][j], acc);
            r[j] = acc;
        }
        return r;
    }

    /** v (first n of v) @ the top-left n x n of B (B[:n, :n], a strided slice). */
    public static double[] vm(double[] v, double[][] b, int n) {
        final double[] r = new double[n];
        for (int j = 0; j < n; j++) {
            double acc = 0.0;
            for (int k = 0; k < n; k++) acc = fma(v[k], b[k][j], acc);
            r[j] = acc;
        }
        return r;
    }

    /** np.dot of two vectors (ddot): an fma chain. */
    public static double dot(double[] a, double[] b) {
        double acc = 0.0;
        for (int k = 0; k < a.length; k++) acc = fma(a[k], b[k], acc);
        return acc;
    }

    public static double dot(double a0, double a1, double b0, double b1) {
        return fma(a1, b1, fma(a0, b0, 0.0));
    }

    public static double dot3(double a0, double a1, double a2, double b0, double b1, double b2) {
        return fma(a2, b2, fma(a1, b1, fma(a0, b0, 0.0)));
    }

    /** np.cross of 3-vectors: products then differences. */
    public static double[] cross(double[] a, double[] b) {
        return new double[] {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    }

    /** np.linalg.norm of a vector: sqrt(dot(x, x)). */
    public static double norm(double[] a) {
        return Math.sqrt(dot(a, a));
    }

    // ------------------------------------------------------------------------------------------------ inverse
    /** np.linalg.inv of a 4 x 4 (row-major numpy matrix), as OpenBLAS's dgesv(A, I) works it. */
    public static double[][] inv(double[][] m) {
        final int n = 4;
        final double[][] a = new double[n][n];
        for (int i = 0; i < n; i++) System.arraycopy(m[i], 0, a[i], 0, n);
        final int[] ipiv = new int[n];
        for (int j = 0; j < n; j++) {
            for (int i = 0; i < j; i++) {
                final int ip = ipiv[i];
                if (ip != i) {
                    final double x = a[i][j];
                    a[i][j] = a[ip][j];
                    a[ip][j] = x;
                }
            }
            for (int i = 1; i < j; i++) {
                double t = 0.0;
                for (int k = 0; k < i; k++) t = fma(a[i][k], a[k][j], t);
                a[i][j] = a[i][j] - t;
            }
            if (j > 0) {
                for (int r = j; r < n; r++) {
                    double t = 0.0;
                    for (int k = 0; k < j; k++) t = fma(a[r][k], a[k][j], t);
                    a[r][j] = a[r][j] - t;
                }
            }
            int jp = j;
            for (int r = j + 1; r < n; r++) if (Math.abs(a[r][j]) > Math.abs(a[jp][j])) jp = r;
            ipiv[j] = jp;
            final double piv = a[jp][j];
            if (jp != j) {
                for (int c = 0; c <= j; c++) {
                    final double x = a[j][c];
                    a[j][c] = a[jp][c];
                    a[jp][c] = x;
                }
            }
            final double rcp = 1.0 / piv;
            for (int r = j + 1; r < n; r++) a[r][j] = a[r][j] * rcp;
        }
        final double[][] b = new double[n][n];
        for (int i = 0; i < n; i++) b[i][i] = 1.0;
        for (int i = 0; i < n; i++) {
            final int ip = ipiv[i];
            if (ip != i) {
                final double[] x = b[i];
                b[i] = b[ip];
                b[ip] = x;
            }
        }
        for (int col = 0; col < n; col++) {
            for (int i = 0; i < n; i++) {
                final double bb = b[i][col];
                for (int k = i + 1; k < n; k++) b[k][col] = fma(-bb, a[k][i], b[k][col]);
            }
            for (int i = n - 1; i >= 0; i--) {
                final double bb = b[i][col] * (1.0 / a[i][i]);
                b[i][col] = bb;
                for (int k = 0; k < i; k++) b[k][col] = fma(-bb, a[k][i], b[k][col]);
            }
        }
        return b;
    }

    // ------------------------------------------------------------------------------------------------ helpers
    public static double[][] eye4() {
        return new double[][] {{1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 0.0, 0.0}, {0.0, 0.0, 1.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
    }

    public static double[][] eye3() {
        return new double[][] {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    }

    public static double[][] copy(double[][] m) {
        final double[][] r = new double[m.length][];
        for (int i = 0; i < m.length; i++) r[i] = m[i].clone();
        return r;
    }

    /** The top-left 3 x 3. */
    public static double[][] rot(double[][] m) {
        return new double[][] {{m[0][0], m[0][1], m[0][2]}, {m[1][0], m[1][1], m[1][2]}, {m[2][0], m[2][1], m[2][2]}};
    }

    /** W[i] @ inv(W[par]) (a bone's pose local to its parent). */
    public static double[][] local(double[][] w, double[][] parent) {
        return mm(w, inv(parent));
    }

    /** CPython's float sum() (Neumaier-compensated for exact floats; an int 0 start). */
    public static double pySum(double[] v, int n) {
        if (n == 0) return 0.0;
        double f = 0.0 + v[0], c = 0.0;
        for (int i = 1; i < n; i++) {
            final double x = v[i], t = f + x;
            c += Math.abs(f) >= Math.abs(x) ? (f - t) + x : (x - t) + f;
            f = t;
        }
        if (c != 0.0 && !Double.isInfinite(c) && !Double.isNaN(c)) f += c;
        return f;
    }

    /** sum() over numpy float64 scalars: no compensation (they aren't exact Python floats), 0 + x0 + x1 ... */
    public static double npSum(double[] v, int n) {
        double f = 0.0;
        for (int i = 0; i < n; i++) f = i == 0 ? 0 + v[0] : f + v[i];
        return f;
    }
}
