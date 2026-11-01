package io.github.newcoke.strikers.guests.models;

/**
 * A 3 x 3 SVD (one-sided Jacobi, Hestenes), A = U diag(S) V^T, S descending: for the r130 Vaporeon re-pose (Stand),
 * where only sign-free products of it are used (U V^T, U D V^T, the sum of S), as accurate as numpy's LAPACK.
 */
final class Svd3 {
    private Svd3() {}

    /** {U, {S}, V^T} */
    static double[][][] svd(double[][] a) {
        final double[][] b = Fix.copy(a), v = Fix.eye(3);
        final int[][] pairs = {{0, 1}, {0, 2}, {1, 2}};
        for (int sweep = 0; sweep < 64; sweep++) {
            boolean turned = false;
            for (int[] pq : pairs) {
                final int p = pq[0], q = pq[1];
                double alpha = 0, beta = 0, gamma = 0;
                for (int i = 0; i < 3; i++) {
                    alpha += b[i][p] * b[i][p];
                    beta += b[i][q] * b[i][q];
                    gamma += b[i][p] * b[i][q];
                }
                if (gamma == 0.0 || Math.abs(gamma) <= 1e-17 * Math.sqrt(alpha * beta)) continue;
                turned = true;
                final double zeta = (beta - alpha) / (2 * gamma);
                final double t;
                if (Math.abs(zeta) > 1e150) t = 0.5 / zeta;
                else t = (zeta >= 0 ? 1.0 : -1.0) / (Math.abs(zeta) + Math.sqrt(1 + zeta * zeta));
                final double c = 1 / Math.sqrt(1 + t * t), s = c * t;
                for (int i = 0; i < 3; i++) {
                    final double bp = b[i][p], bq = b[i][q];
                    b[i][p] = c * bp - s * bq;
                    b[i][q] = s * bp + c * bq;
                    final double vp = v[i][p], vq = v[i][q];
                    v[i][p] = c * vp - s * vq;
                    v[i][q] = s * vp + c * vq;
                }
            }
            if (!turned) break;
        }
        final double[] sv = new double[3];
        for (int j = 0; j < 3; j++) sv[j] = Math.sqrt(b[0][j] * b[0][j] + b[1][j] * b[1][j] + b[2][j] * b[2][j]);
        final Integer[] ord = {0, 1, 2};
        java.util.Arrays.sort(ord, (x, y) -> Double.compare(sv[y], sv[x]));
        final double[][] u = new double[3][3], vt = new double[3][3];
        final double[] s = new double[3];
        for (int k = 0; k < 3; k++) {
            final int j = ord[k];
            s[k] = sv[j];
            for (int i = 0; i < 3; i++) {
                u[i][k] = sv[j] > 0 ? b[i][j] / sv[j] : (i == k ? 1.0 : 0.0);
                vt[k][i] = v[i][j];
            }
        }
        return new double[][][] {u, {s}, vt};
    }
}
