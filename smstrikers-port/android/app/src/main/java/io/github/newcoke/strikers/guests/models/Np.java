package io.github.newcoke.strikers.guests.models;

/**
 * numpy's float64 matrix sums as the PC's numpy (OpenBLAS) works them, for the r127 / r130 post-build pack fixes
 * (gpk_reparent.py, gpk_repose.py, gpk_rebind.py, hariyama_fix.py, vap_stand.py): 4 x 4 products (dgemm: one fused
 * multiply-add per term, in order), vector x matrix, and np.linalg.inv (LAPACK dgesv on the identity: OpenBLAS's
 * left-looking getf2 with partial pivoting, then the two triangular solves with the diagonal's reciprocal). The packs
 * keep float32, so the last float64 bit rarely shows; checked against numpy on every matrix the fixes use.
 */
final class Np {
    private Np() {}

    /** a @ b (row-major; dgemm: the accumulator from +0, one fused multiply-add per term, in order). */
    static double[][] mm(double[][] a, double[][] b) {
        final int n = a.length, k = b.length, m = b[0].length;
        final double[][] r = new double[n][m];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++) {
                double s = 0.0;
                for (int q = 0; q < k; q++) s = Fma.fma(a[i][q], b[q][j], s);
                r[i][j] = s;
            }
        return r;
    }

    /**
     * v @ m (row vector times a C-ordered matrix: numpy's dgemv_n): one fused multiply-add per term from +0, in order;
     * r135: for 4 terms numpy sums them in the order 1, 0, 2, 3 (found against numpy on 3,000 random 4 x 4s, every
     * one; the r127 / r130 checks used rigid bind matrices, where both orders agree: those packs keep their bytes).
     */
    static double[] vm(double[] v, double[][] m) {
        final int k = v.length, c = m[0].length;
        final double[] r = new double[c];
        for (int j = 0; j < c; j++) {
            double s = 0.0;
            if (k == 4) {
                s = Fma.fma(v[1], m[1][j], s);
                s = Fma.fma(v[0], m[0][j], s);
                s = Fma.fma(v[2], m[2][j], s);
                s = Fma.fma(v[3], m[3][j], s);
            } else {
                for (int q = 0; q < k; q++) s = Fma.fma(v[q], m[q][j], s);
            }
            r[j] = s;
        }
        return r;
    }

    /**
     * v @ c.T, c C-ordered (numpy on a transposed view: OpenBLAS's dgemv_t, a dot product per output; found against
     * numpy (NpTest): length 3 = fma(v0, c0, v1 c1) then fma(v2, c2, .); length 4 = the 4 products rounded, summed as
     * (p0 + p2) + (p1 + p3)).
     */
    static double[] vmT(double[] v, double[][] c) {
        final double[] r = new double[c.length];
        for (int j = 0; j < c.length; j++) {
            final double[] a = c[j];
            if (v.length == 3) {
                r[j] = 0.0 + Fma.fma(v[2], a[2], Fma.fma(v[0], a[0], v[1] * a[1]));
            } else if (v.length == 4) {
                final double p0 = v[0] * a[0], p1 = v[1] * a[1], p2 = v[2] * a[2], p3 = v[3] * a[3];
                r[j] = 0.0 + ((p0 + p2) + (p1 + p3));
            } else {
                throw new IllegalArgumentException("vmT " + v.length);
            }
        }
        return r;
    }

    /** a @ c.T, c C-ordered. */
    static double[][] mmT(double[][] a, double[][] c) {
        final double[][] r = new double[a.length][c.length];
        for (int i = 0; i < a.length; i++) for (int j = 0; j < c.length; j++) r[i][j] = dot(a[i], c[j]);
        return r;
    }

    /** a @ b of two vectors (ddot: one fused multiply-add per term, in order, from +0). */
    static double dot(double[] a, double[] b) {
        double s = 0.0;
        for (int i = 0; i < a.length; i++) s = Fma.fma(a[i], b[i], s);
        return s;
    }

    /** np.linalg.norm(v) = sqrt(v @ v). */
    static double norm(double[] v) {
        return Math.sqrt(dot(v, v));
    }

    /** np.cross of two 3-vectors (each product rounded, then the difference). */
    static double[] cross(double[] a, double[] b) {
        return new double[] {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    }

    /** np.linalg.inv(a) (n x n). */
    static double[][] inv(double[][] a0) {
        final int n = a0.length;
        // LAPACK sees the matrix column-major: col[j][i] = a[i][j]
        final double[][] a = new double[n][n];
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) a[j][i] = a0[i][j];
        final int[] ipiv = new int[n];
        for (int j = 0; j < n; j++) {
            final double[] b = a[j];
            final int jm = Math.min(j, n);
            for (int i = 0; i < jm; i++) {
                final int ip = ipiv[i];
                if (ip != i) {
                    final double t = b[i];
                    b[i] = b[ip];
                    b[ip] = t;
                }
            }
            for (int i = 1; i < jm; i++) {
                double s = 0;
                for (int q = 0; q < i; q++) s = Fma.fma(a[q][i], b[q], s);
                b[i] -= s;
            }
            // gemv: b[j:] -= A[j:, :j] @ b[:j]
            for (int i = j; i < n; i++) {
                double s = 0;
                for (int q = 0; q < j; q++) s = Fma.fma(a[q][i], b[q], s);
                b[i] -= s;
            }
            int jp = j;
            double best = Math.abs(b[j]);
            for (int i = j + 1; i < n; i++) {
                if (Math.abs(b[i]) > best) {
                    best = Math.abs(b[i]);
                    jp = i;
                }
            }
            ipiv[j] = jp;
            final double piv = b[jp];
            if (piv != 0.0) {
                final double r = 1.0 / piv;
                if (jp != j) {
                    for (int c = 0; c <= j; c++) {
                        final double t = a[c][j];
                        a[c][j] = a[c][jp];
                        a[c][jp] = t;
                    }
                }
                for (int i = j + 1; i < n; i++) b[i] *= r;
            }
        }
        // B = I with the row swaps, then L (unit) forward, U backward (x = c * (1 / u_ii)), column by column
        final double[][] x = new double[n][n];  // x[col][row]
        for (int col = 0; col < n; col++) {
            final double[] c = new double[n];
            c[col] = 1.0;
            for (int i = 0; i < n; i++) {
                final int ip = ipiv[i];
                if (ip != i) {
                    final double t = c[i];
                    c[i] = c[ip];
                    c[ip] = t;
                }
            }
            for (int i = 0; i < n; i++) {
                final double bb = c[i];
                for (int k = i + 1; k < n; k++) c[k] = Fma.fma(-bb, a[i][k], c[k]);
            }
            for (int i = n - 1; i >= 0; i--) {
                final double bb = c[i] * (1.0 / a[i][i]);
                c[i] = bb;
                for (int k = 0; k < i; k++) c[k] = Fma.fma(-bb, a[i][k], c[k]);
            }
            x[col] = c;
        }
        final double[][] r = new double[n][n];
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) r[i][j] = x[j][i];
        return r;
    }
}
