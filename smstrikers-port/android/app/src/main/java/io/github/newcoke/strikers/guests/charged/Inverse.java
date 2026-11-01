package io.github.newcoke.strikers.guests.charged;

import java.io.IOException;

/**
 * numpy.linalg.inv of a 4x4 as the PC computes it (numpy -> OpenBLAS dgesv): the left-looking LU of OpenBLAS's getf2
 * (each column: earlier row swaps, the unit-lower solve by dot products, the update by gemv, the first largest pivot,
 * the swap, the column scaled by the pivot's reciprocal), then getrs on the identity (row swaps, the forward solve
 * with L, the backward solve with U multiplying by its diagonals' reciprocals). Dot products and updates are fused
 * multiply-adds as OpenBLAS's Haswell kernels do them: bit for bit the same doubles as numpy on 758 test matrices
 * (every inverse bind of the 12 home figures and 300 random ones).
 */
final class Inverse {
    private Inverse() {}

    /** m: 16 doubles, row-major. */
    static double[] inv(double[] m) throws IOException {
        final int n = 4;
        final double[][] a = new double[n][n];
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) a[i][j] = m[i * n + j];
        final int[] ipiv = new int[n];
        for (int j = 0; j < n; j++) {
            for (int i = 0; i < j; i++) {
                final int ip = ipiv[i];
                if (ip != i) {
                    final double t = a[i][j];
                    a[i][j] = a[ip][j];
                    a[ip][j] = t;
                }
            }
            for (int i = 1; i < j; i++) {
                double s = 0.0;
                for (int k = 0; k < i; k++) s = Fma.fma(a[k][j], a[i][k], s);
                a[i][j] = a[i][j] - s;
            }
            if (j > 0)
                for (int r = j; r < n; r++) {
                    double t = 0.0;
                    for (int k = 0; k < j; k++) t = Fma.fma(a[r][k], a[k][j], t);
                    a[r][j] = a[r][j] - t;
                }
            int jp = j;
            double best = Math.abs(a[j][j]);
            for (int r = j + 1; r < n; r++)
                if (Math.abs(a[r][j]) > best) {
                    best = Math.abs(a[r][j]);
                    jp = r;
                }
            ipiv[j] = jp;
            double t1 = a[jp][j];
            if (t1 == 0.0) throw new IOException("singular bind matrix");
            t1 = 1.0 / t1;
            if (jp != j)
                for (int c = 0; c <= j; c++) {
                    final double t = a[j][c];
                    a[j][c] = a[jp][c];
                    a[jp][c] = t;
                }
            for (int r = j + 1; r < n; r++) a[r][j] = a[r][j] * t1;
        }
        final double[][] B = new double[n][n];
        for (int i = 0; i < n; i++) B[i][i] = 1.0;
        for (int i = 0; i < n; i++) {
            final int ip = ipiv[i];
            if (ip != i) {
                final double[] t = B[i];
                B[i] = B[ip];
                B[ip] = t;
            }
        }
        for (int c = 0; c < n; c++)
            for (int i = 0; i < n; i++) {
                final double bb = B[i][c];
                for (int k = i + 1; k < n; k++) B[k][c] = Fma.fma(-bb, a[k][i], B[k][c]);
            }
        final double[] invD = new double[n];
        for (int i = 0; i < n; i++) invD[i] = 1.0 / a[i][i];
        for (int c = 0; c < n; c++)
            for (int i = n - 1; i >= 0; i--) {
                final double bb = B[i][c] * invD[i];
                B[i][c] = bb;
                for (int k = 0; k < i; k++) B[k][c] = Fma.fma(-bb, a[k][i], B[k][c]);
            }
        final double[] out = new double[n * n];
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) out[i * n + j] = B[i][j];
        return out;
    }
}
