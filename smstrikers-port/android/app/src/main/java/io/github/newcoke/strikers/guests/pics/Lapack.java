package io.github.newcoke.strikers.guests.pics;

/**
 * The least squares numpy's lstsq runs for the kit fit (LAPACK 3.2.2 dgelsd as numpy's lapack_lite builds it:
 * reference BLAS, double, no fused multiply-add), step for step, so the rounding (it decides some near-flat
 * pixels' colours in kitres.py) comes out the same: Householder QR, bidiagonal reduction, implicit-shift QR SVD
 * (dbdsqr through dlasdq / dlalsd), the solve. Only the path the fit takes is here: rows >= 1.6 x columns,
 * columns <= 25 (dlalsd's small case). Matrices column-major, Fortran's indices kept (1-based through `at`).
 */
final class Lapack {
    private Lapack() {}

    private static final double ZERO = 0.0, ONE = 1.0, TWO = 2.0;
    static final double EPS = 0x1p-53;           // dlamch('E')
    static final double SAFMIN = 0x1p-1022;      // dlamch('S')
    private static final double PREC = 0x1p-52;  // dlamch('P')
    private static final double SAFMN2 = 0x1p-484, SAFMX2 = 0x1p484;  // dlartg's

    // ------------------------------------------------------------------ f2c helpers

    /** f2c's d_sign: |a| with the sign of b (b >= 0, -0 included, is plus). */
    private static double sign(double a, double b) {
        final double x = a >= 0 ? a : -a;
        return b >= 0 ? x : -x;
    }

    private static double max(double a, double b) {
        return a >= b ? a : b;
    }

    private static double min(double a, double b) {
        return a <= b ? a : b;
    }

    // ------------------------------------------------------------------ reference BLAS

    static double dnrm2(int n, double[] x, int ix, int incx) {
        if (n < 1 || incx < 1) return ZERO;
        if (n == 1) return Math.abs(x[ix]);
        double scale = ZERO, ssq = ONE;
        for (int k = 0; k < n; k++) {
            final double v = x[ix + k * incx];
            if (v != ZERO) {
                final double absxi = Math.abs(v);
                if (scale < absxi) {
                    final double d = scale / absxi;
                    ssq = ssq * (d * d) + ONE;
                    scale = absxi;
                } else {
                    final double d = absxi / scale;
                    ssq += d * d;
                }
            }
        }
        return scale * Math.sqrt(ssq);
    }

    private static void dscal(int n, double da, double[] x, int ix, int incx) {
        for (int k = 0; k < n; k++) x[ix + k * incx] = da * x[ix + k * incx];
    }

    /** y := alpha * A' x (beta = 0); A m x n at a[ao], lda. */
    private static void dgemvT(int m, int n, double alpha, double[] a, int ao, int lda, double[] x, int xo, int incx,
                               double[] y, int yo) {
        for (int j = 0; j < n; j++) y[yo + j] = ZERO;
        for (int j = 0; j < n; j++) {
            double temp = ZERO;
            for (int i = 0; i < m; i++) temp += a[ao + i + j * lda] * x[xo + i * incx];
            y[yo + j] += alpha * temp;
        }
    }

    /** y := alpha * A x (beta = 0). */
    private static void dgemvN(int m, int n, double alpha, double[] a, int ao, int lda, double[] x, int xo, int incx,
                               double[] y, int yo) {
        for (int i = 0; i < m; i++) y[yo + i] = ZERO;
        for (int j = 0; j < n; j++) {
            final double xj = x[xo + j * incx];
            if (xj != ZERO) {
                final double temp = alpha * xj;
                for (int i = 0; i < m; i++) y[yo + i] += temp * a[ao + i + j * lda];
            }
        }
    }

    /** A := alpha x y' + A. */
    private static void dger(int m, int n, double alpha, double[] x, int xo, int incx, double[] y, int yo, int incy,
                             double[] a, int ao, int lda) {
        for (int j = 0; j < n; j++) {
            final double yj = y[yo + j * incy];
            if (yj != ZERO) {
                final double temp = alpha * yj;
                for (int i = 0; i < m; i++) a[ao + i + j * lda] += x[xo + i * incx] * temp;
            }
        }
    }

    // ------------------------------------------------------------------ LAPACK auxiliaries

    static double dlapy2(double x, double y) {
        final double xa = Math.abs(x), ya = Math.abs(y);
        final double w = max(xa, ya), z = min(xa, ya);
        if (z == ZERO) return w;
        final double d = z / w;
        return w * Math.sqrt(d * d + ONE);
    }

    /** dlarfg: alpha at v[ai], x = v[xo..] step incx; returns tau, alpha becomes beta. */
    private static double dlarfg(int n, double[] v, int ai, int xo, int incx) {
        if (n <= 1) return ZERO;
        double xnorm = dnrm2(n - 1, v, xo, incx);
        if (xnorm == ZERO) return ZERO;
        double alpha = v[ai];
        double beta = -sign(dlapy2(alpha, xnorm), alpha);
        final double safmin = SAFMIN / EPS;
        int knt = 0;
        if (Math.abs(beta) < safmin) {
            final double rsafmn = ONE / safmin;
            do {
                knt++;
                dscal(n - 1, rsafmn, v, xo, incx);
                beta = beta * rsafmn;
                alpha = alpha * rsafmn;
            } while (Math.abs(beta) < safmin);
            xnorm = dnrm2(n - 1, v, xo, incx);
            beta = -sign(dlapy2(alpha, xnorm), alpha);
        }
        final double tau = (beta - alpha) / beta;
        dscal(n - 1, ONE / (alpha - beta), v, xo, incx);
        for (int j = 0; j < knt; j++) beta = beta * safmin;
        v[ai] = beta;
        return tau;
    }

    /** dlarf: H = I - tau v v' applied to C (m x n at c[co], ldc) from the left or the right. */
    private static void dlarf(boolean left, int m, int n, double[] v, int vo, int incv, double tau, double[] c, int co,
                              int ldc, double[] work) {
        int lastv = 0, lastc = 0;
        if (tau != ZERO) {
            lastv = left ? m : n;
            int i = incv > 0 ? vo + (lastv - 1) * incv : vo;
            while (lastv > 0 && v[i] == ZERO) {
                lastv--;
                i -= incv;
            }
            lastc = left ? iladlc(lastv, n, c, co, ldc) : iladlr(m, lastv, c, co, ldc);
        }
        if (lastv <= 0) return;
        if (left) {
            dgemvT(lastv, lastc, ONE, c, co, ldc, v, vo, incv, work, 0);
            dger(lastv, lastc, -tau, v, vo, incv, work, 0, 1, c, co, ldc);
        } else {
            dgemvN(lastc, lastv, ONE, c, co, ldc, v, vo, incv, work, 0);
            dger(lastc, lastv, -tau, work, 0, 1, v, vo, incv, c, co, ldc);
        }
    }

    /** The last column of A (m x n) with a non-zero, 0 when none. */
    private static int iladlc(int m, int n, double[] a, int ao, int lda) {
        if (n == 0) return 0;
        if (a[ao + (n - 1) * lda] != ZERO || a[ao + m - 1 + (n - 1) * lda] != ZERO) return n;
        for (int j = n; j >= 1; j--)
            for (int i = 0; i < m; i++)
                if (a[ao + i + (j - 1) * lda] != ZERO) return j;
        return 0;
    }

    /** The last row of A (m x n) with a non-zero, 0 when none. */
    private static int iladlr(int m, int n, double[] a, int ao, int lda) {
        if (m == 0) return 0;
        if (a[ao + m - 1] != ZERO || a[ao + m - 1 + (n - 1) * lda] != ZERO) return m;
        int r = 0;
        for (int j = 0; j < n; j++) {
            int i = m;
            while (i >= 1 && a[ao + i - 1 + j * lda] == ZERO) i--;
            r = Math.max(r, i);
        }
        return r;
    }

    // ------------------------------------------------------------------ factorizations

    /** dgeqr2: A (m x n) = QR, reflectors below the diagonal, tau[0..k). */
    private static void dgeqr2(int m, int n, double[] a, int lda, double[] tau, double[] work) {
        final int k = Math.min(m, n);
        for (int i = 1; i <= k; i++) {
            final int aii = (i - 1) + (i - 1) * lda;
            tau[i - 1] = dlarfg(m - i + 1, a, aii, (Math.min(i + 1, m) - 1) + (i - 1) * lda, 1);
            if (i < n) {
                final double keep = a[aii];
                a[aii] = ONE;
                dlarf(true, m - i + 1, n - i, a, aii, 1, tau[i - 1], a, aii + lda, lda, work);
                a[aii] = keep;
            }
        }
    }

    /** dorm2r 'L', 'T': C := Q' C (Q from k reflectors in A's columns), forwards. */
    private static void dorm2rLT(int m, int n, int k, double[] a, int lda, double[] tau, int to, double[] c, int ldc,
                                 double[] work) {
        for (int i = 1; i <= k; i++) {
            final int aii = (i - 1) + (i - 1) * lda;
            final double keep = a[aii];
            a[aii] = ONE;
            dlarf(true, m - i + 1, n, a, aii, 1, tau[to + i - 1], c, i - 1, ldc, work);
            a[aii] = keep;
        }
    }

    /** dorml2 'L', 'T' on A's rows from (ao): C := P C, the k reflectors backwards. */
    private static void dorml2LT(int m, int n, int k, double[] a, int ao, int lda, double[] tau, int to, double[] c,
                                 int co, int ldc, double[] work) {
        for (int i = k; i >= 1; i--) {
            final int aii = ao + (i - 1) + (i - 1) * lda;
            final double keep = a[aii];
            a[aii] = ONE;
            dlarf(true, m - i + 1, n, a, aii, lda, tau[to + i - 1], c, co + i - 1, ldc, work);
            a[aii] = keep;
        }
    }

    /** dgebd2, m >= n: A = Q B P', d / e the bidiagonal, tauq / taup. */
    private static void dgebd2(int m, int n, double[] a, int lda, double[] d, double[] e, double[] tauq, double[] taup,
                               double[] work) {
        for (int i = 1; i <= n; i++) {
            final int aii = (i - 1) + (i - 1) * lda;
            tauq[i - 1] = dlarfg(m - i + 1, a, aii, (Math.min(i + 1, m) - 1) + (i - 1) * lda, 1);
            d[i - 1] = a[aii];
            a[aii] = ONE;
            if (i < n) dlarf(true, m - i + 1, n - i, a, aii, 1, tauq[i - 1], a, aii + lda, lda, work);
            a[aii] = d[i - 1];
            if (i < n) {
                final int ai1 = (i - 1) + i * lda;
                taup[i - 1] = dlarfg(n - i, a, ai1, (i - 1) + (Math.min(i + 2, n) - 1) * lda, lda);
                e[i - 1] = a[ai1];
                a[ai1] = ONE;
                dlarf(false, m - i, n - i, a, ai1, lda, taup[i - 1], a, i + i * lda, lda, work);
                a[ai1] = e[i - 1];
            } else {
                taup[i - 1] = ZERO;
            }
        }
    }

    // ------------------------------------------------------------------ the bidiagonal SVD

    /** dlartg: {cs, sn, r}. */
    private static void dlartg(double f, double g, double[] out) {
        if (g == ZERO) {
            out[0] = ONE;
            out[1] = ZERO;
            out[2] = f;
            return;
        }
        if (f == ZERO) {
            out[0] = ZERO;
            out[1] = ONE;
            out[2] = g;
            return;
        }
        double f1 = f, g1 = g, cs, sn, r;
        final double scale = max(Math.abs(f1), Math.abs(g1));
        if (scale >= SAFMX2) {
            int count = 0;
            double sc;
            do {
                count++;
                f1 = f1 * SAFMN2;
                g1 = g1 * SAFMN2;
                sc = max(Math.abs(f1), Math.abs(g1));
            } while (sc >= SAFMX2);
            r = Math.sqrt(f1 * f1 + g1 * g1);
            cs = f1 / r;
            sn = g1 / r;
            for (int i = 0; i < count; i++) r = r * SAFMX2;
        } else if (scale <= SAFMN2) {
            int count = 0;
            double sc;
            do {
                count++;
                f1 = f1 * SAFMX2;
                g1 = g1 * SAFMX2;
                sc = max(Math.abs(f1), Math.abs(g1));
            } while (sc <= SAFMN2);
            r = Math.sqrt(f1 * f1 + g1 * g1);
            cs = f1 / r;
            sn = g1 / r;
            for (int i = 0; i < count; i++) r = r * SAFMN2;
        } else {
            r = Math.sqrt(f1 * f1 + g1 * g1);
            cs = f1 / r;
            sn = g1 / r;
        }
        if (Math.abs(f) > Math.abs(g) && cs < ZERO) {
            cs = -cs;
            sn = -sn;
            r = -r;
        }
        out[0] = cs;
        out[1] = sn;
        out[2] = r;
    }

    /** dlas2: {ssmin, ssmax} of [[f, g], [0, h]]. */
    private static void dlas2(double f, double g, double h, double[] out) {
        final double fa = Math.abs(f), ga = Math.abs(g), ha = Math.abs(h);
        final double fhmn = min(fa, ha), fhmx = max(fa, ha);
        double ssmin, ssmax;
        if (fhmn == ZERO) {
            ssmin = ZERO;
            if (fhmx == ZERO) {
                ssmax = ga;
            } else {
                final double q = min(fhmx, ga) / max(fhmx, ga);
                ssmax = max(fhmx, ga) * Math.sqrt(q * q + ONE);
            }
        } else {
            if (ga < fhmx) {
                final double as = ONE + fhmn / fhmx;
                final double at = (fhmx - fhmn) / fhmx;
                final double q = ga / fhmx;
                final double au = q * q;
                final double c = TWO / (Math.sqrt(as * as + au) + Math.sqrt(at * at + au));
                ssmin = fhmn * c;
                ssmax = fhmx / c;
            } else {
                final double au = fhmx / ga;
                if (au == ZERO) {
                    ssmin = fhmn * fhmx / ga;
                    ssmax = ga;
                } else {
                    final double as = ONE + fhmn / fhmx;
                    final double at = (fhmx - fhmn) / fhmx;
                    final double p = as * au, q = at * au;
                    final double c = ONE / (Math.sqrt(p * p + ONE) + Math.sqrt(q * q + ONE));
                    ssmin = fhmn * c * au;
                    ssmin = ssmin + ssmin;
                    ssmax = ga / (c + c);
                }
            }
        }
        out[0] = ssmin;
        out[1] = ssmax;
    }

    /** dlasv2: {ssmin, ssmax, snr, csr, snl, csl} of [[f, g], [0, h]]. */
    private static void dlasv2(double f, double g, double h, double[] out) {
        double ft = f, fa = Math.abs(ft), ht = h, ha = Math.abs(h);
        int pmax = 1;
        final boolean swap = ha > fa;
        if (swap) {
            pmax = 3;
            double temp = ft;
            ft = ht;
            ht = temp;
            temp = fa;
            fa = ha;
            ha = temp;
        }
        final double gt = g, ga = Math.abs(gt);
        double ssmin, ssmax, clt, crt, slt, srt;
        if (ga == ZERO) {
            ssmin = ha;
            ssmax = fa;
            clt = ONE;
            crt = ONE;
            slt = ZERO;
            srt = ZERO;
        } else {
            boolean gasmal = true;
            ssmin = ssmax = clt = crt = slt = srt = ZERO;
            if (ga > fa) {
                pmax = 2;
                if (fa / ga < EPS) {
                    gasmal = false;
                    ssmax = ga;
                    if (ha > ONE) ssmin = fa / (ga / ha);
                    else ssmin = fa / ga * ha;
                    clt = ONE;
                    slt = ht / gt;
                    srt = ONE;
                    crt = ft / gt;
                }
            }
            if (gasmal) {
                final double dd = fa - ha;
                double l = dd == fa ? ONE : dd / fa;
                final double mm0 = gt / ft;
                double t = TWO - l;
                final double mm = mm0 * mm0, tt = t * t;
                final double s = Math.sqrt(tt + mm);
                final double r = l == ZERO ? Math.abs(mm0) : Math.sqrt(l * l + mm);
                final double aa = 0.5 * (s + r);
                ssmin = ha / aa;
                ssmax = fa * aa;
                if (mm == ZERO) {
                    if (l == ZERO) t = sign(TWO, ft) * sign(ONE, gt);
                    else t = gt / sign(dd, ft) + mm0 / t;
                } else {
                    t = (mm0 / (s + t) + mm0 / (r + l)) * (ONE + aa);
                }
                l = Math.sqrt(t * t + 4.0);
                crt = TWO / l;
                srt = t / l;
                clt = (crt + srt * mm0) / aa;
                slt = ht / ft * srt / aa;
            }
        }
        double csl, snl, csr, snr;
        if (swap) {
            csl = srt;
            snl = crt;
            csr = slt;
            snr = clt;
        } else {
            csl = clt;
            snl = slt;
            csr = crt;
            snr = srt;
        }
        double tsign = ONE;
        if (pmax == 1) tsign = sign(ONE, csr) * sign(ONE, csl) * sign(ONE, f);
        if (pmax == 2) tsign = sign(ONE, snr) * sign(ONE, csl) * sign(ONE, g);
        if (pmax == 3) tsign = sign(ONE, snr) * sign(ONE, snl) * sign(ONE, h);
        ssmax = sign(ssmax, tsign);
        ssmin = sign(ssmin, tsign * sign(ONE, f) * sign(ONE, h));
        out[0] = ssmin;
        out[1] = ssmax;
        out[2] = snr;
        out[3] = csr;
        out[4] = snl;
        out[5] = csl;
    }

    /** dlasr 'L', 'V', direction 'F' (forward) or 'B': plane rotations on rows of A (m x n at ao). */
    private static void dlasrLV(boolean forward, int m, int n, double[] c, int cOff, double[] s, int sOff, double[] a,
                                int ao, int lda) {
        if (forward) {
            for (int j = 1; j <= m - 1; j++) rotRows(c[cOff + j - 1], s[sOff + j - 1], j, n, a, ao, lda);
        } else {
            for (int j = m - 1; j >= 1; j--) rotRows(c[cOff + j - 1], s[sOff + j - 1], j, n, a, ao, lda);
        }
    }

    private static void rotRows(double ctemp, double stemp, int j, int n, double[] a, int ao, int lda) {
        if (ctemp == ONE && stemp == ZERO) return;
        for (int i = 0; i < n; i++) {
            final int pj = ao + (j - 1) + i * lda, pj1 = pj + 1;
            final double temp = a[pj1];
            a[pj1] = ctemp * temp - stemp * a[pj];
            a[pj] = stemp * temp + ctemp * a[pj];
        }
    }

    /** drot on rows x, y of A (n columns). */
    private static void drotRows(int n, double[] a, int ao, int lda, int x, int y, double c, double s) {
        for (int i = 0; i < n; i++) {
            final int px = ao + (x - 1) + i * lda, py = ao + (y - 1) + i * lda;
            final double dtemp = c * a[px] + s * a[py];
            a[py] = c * a[py] - s * a[px];
            a[px] = dtemp;
        }
    }

    private static void swapRows(int n, double[] a, int ao, int lda, int x, int y) {
        for (int i = 0; i < n; i++) {
            final int px = ao + (x - 1) + i * lda, py = ao + (y - 1) + i * lda;
            final double t = a[px];
            a[px] = a[py];
            a[py] = t;
        }
    }

    /**
     * dbdsqr 'U' with VT (n x ncvt at vt, ldvt), no U, C (n x ncc at c[co], ldc): d[1..n], e[1..n-1] (index 0
     * unused). Returns info (0 = converged).
     */
    private static int dbdsqr(int n, int ncvt, int ncc, double[] d, double[] e, double[] vt, int ldvt, double[] c,
                              int co, int ldc) {
        if (n == 0) return 0;
        if (n > 1) {
            final int nm1 = n - 1, nm12 = nm1 + nm1, nm13 = nm12 + nm1;
            final double[] work = new double[4 * n + 1];  // 1-based
            final double[] rot = new double[6];
            int idir = 0;
            final double eps = EPS, unfl = SAFMIN;
            final double tolmul = max(10.0, min(100.0, StrictMath.pow(eps, -0.125)));
            final double tol = tolmul * eps;
            double smax = ZERO;
            for (int i = 1; i <= n; i++) smax = max(smax, Math.abs(d[i]));
            for (int i = 1; i <= n - 1; i++) smax = max(smax, Math.abs(e[i]));
            double sminl = ZERO;
            double thresh;
            // relative accuracy (tol >= 0)
            double sminoa = Math.abs(d[1]);
            if (sminoa != ZERO) {
                double mu = sminoa;
                for (int i = 2; i <= n; i++) {
                    mu = Math.abs(d[i]) * (mu / (mu + Math.abs(e[i - 1])));
                    sminoa = min(sminoa, mu);
                    if (sminoa == ZERO) break;
                }
            }
            sminoa = sminoa / Math.sqrt((double) n);
            thresh = max(tol * sminoa, 6 * n * n * unfl);
            final int maxit = 6 * n * n;
            int iter = 0, oldll = -1, oldm = -1;
            int m = n;
            mainLoop:
            while (true) {
                if (m <= 1) break;
                if (iter > maxit) return 1;
                smax = Math.abs(d[m]);
                double smin = smax;
                int ll = 0;
                boolean split = false;
                for (int lll = 1; lll <= m - 1; lll++) {
                    ll = m - lll;
                    final double abss = Math.abs(d[ll]), abse = Math.abs(e[ll]);
                    if (abse <= thresh) {
                        split = true;
                        break;
                    }
                    smin = min(smin, abss);
                    smax = max(smax, max(abss, abse));
                }
                if (split) {
                    e[ll] = ZERO;
                    if (ll == m - 1) {
                        m = m - 1;
                        continue;
                    }
                } else {
                    ll = 0;
                }
                ll = ll + 1;
                if (ll == m - 1) {
                    dlasv2(d[m - 1], e[m - 1], d[m], rot);
                    final double sigmn = rot[0], sigmx = rot[1], sinr = rot[2], cosr = rot[3], sinl = rot[4], cosl = rot[5];
                    d[m - 1] = sigmx;
                    e[m - 1] = ZERO;
                    d[m] = sigmn;
                    if (ncvt > 0) drotRows(ncvt, vt, 0, ldvt, m - 1, m, cosr, sinr);
                    if (ncc > 0) drotRows(ncc, c, co, ldc, m - 1, m, cosl, sinl);
                    m = m - 2;
                    continue;
                }
                if (ll > oldm || m < oldll) {
                    if (Math.abs(d[ll]) >= Math.abs(d[m])) idir = 1;
                    else idir = 2;
                }
                if (idir == 1) {
                    if (Math.abs(e[m - 1]) <= Math.abs(tol) * Math.abs(d[m])) {
                        e[m - 1] = ZERO;
                        continue;
                    }
                    double mu = Math.abs(d[ll]);
                    sminl = mu;
                    for (int lll = ll; lll <= m - 1; lll++) {
                        if (Math.abs(e[lll]) <= tol * mu) {
                            e[lll] = ZERO;
                            continue mainLoop;
                        }
                        mu = Math.abs(d[lll + 1]) * (mu / (mu + Math.abs(e[lll])));
                        sminl = min(sminl, mu);
                    }
                } else {
                    if (Math.abs(e[ll]) <= Math.abs(tol) * Math.abs(d[ll])) {
                        e[ll] = ZERO;
                        continue;
                    }
                    double mu = Math.abs(d[m]);
                    sminl = mu;
                    for (int lll = m - 1; lll >= ll; lll--) {
                        if (Math.abs(e[lll]) <= tol * mu) {
                            e[lll] = ZERO;
                            continue mainLoop;
                        }
                        mu = Math.abs(d[lll]) * (mu / (mu + Math.abs(e[lll])));
                        sminl = min(sminl, mu);
                    }
                }
                oldll = ll;
                oldm = m;
                double shift;
                if (n * tol * (sminl / smax) <= max(eps, 0.01 * tol)) {
                    shift = ZERO;
                } else {
                    double sll;
                    if (idir == 1) {
                        sll = Math.abs(d[ll]);
                        dlas2(d[m - 1], e[m - 1], d[m], rot);
                    } else {
                        sll = Math.abs(d[m]);
                        dlas2(d[ll], e[ll], d[ll + 1], rot);
                    }
                    shift = rot[0];
                    if (sll > ZERO) {
                        final double q = shift / sll;
                        if (q * q < eps) shift = ZERO;
                    }
                }
                iter = iter + m - ll;
                if (shift == ZERO) {
                    if (idir == 1) {
                        double cs = ONE, oldcs = ONE, sn = ZERO, oldsn = ZERO, r;
                        for (int i = ll; i <= m - 1; i++) {
                            dlartg(d[i] * cs, e[i], rot);
                            cs = rot[0];
                            sn = rot[1];
                            r = rot[2];
                            if (i > ll) e[i - 1] = oldsn * r;
                            dlartg(oldcs * r, d[i + 1] * sn, rot);
                            oldcs = rot[0];
                            oldsn = rot[1];
                            d[i] = rot[2];
                            work[i - ll + 1] = cs;
                            work[i - ll + 1 + nm1] = sn;
                            work[i - ll + 1 + nm12] = oldcs;
                            work[i - ll + 1 + nm13] = oldsn;
                        }
                        final double h = d[m] * cs;
                        d[m] = h * oldcs;
                        e[m - 1] = h * oldsn;
                        if (ncvt > 0) dlasrLV(true, m - ll + 1, ncvt, work, 1, work, n, vt, ll - 1, ldvt);
                        if (ncc > 0) dlasrLV(true, m - ll + 1, ncc, work, nm12 + 1, work, nm13 + 1, c, co + ll - 1, ldc);
                        if (Math.abs(e[m - 1]) <= thresh) e[m - 1] = ZERO;
                    } else {
                        double cs = ONE, oldcs = ONE, sn = ZERO, oldsn = ZERO, r;
                        for (int i = m; i >= ll + 1; i--) {
                            dlartg(d[i] * cs, e[i - 1], rot);
                            cs = rot[0];
                            sn = rot[1];
                            r = rot[2];
                            if (i < m) e[i] = oldsn * r;
                            dlartg(oldcs * r, d[i - 1] * sn, rot);
                            oldcs = rot[0];
                            oldsn = rot[1];
                            d[i] = rot[2];
                            work[i - ll] = cs;
                            work[i - ll + nm1] = -sn;
                            work[i - ll + nm12] = oldcs;
                            work[i - ll + nm13] = -oldsn;
                        }
                        final double h = d[ll] * cs;
                        d[ll] = h * oldcs;
                        e[ll] = h * oldsn;
                        if (ncvt > 0) dlasrLV(false, m - ll + 1, ncvt, work, nm12 + 1, work, nm13 + 1, vt, ll - 1, ldvt);
                        if (ncc > 0) dlasrLV(false, m - ll + 1, ncc, work, 1, work, n, c, co + ll - 1, ldc);
                        if (Math.abs(e[ll]) <= thresh) e[ll] = ZERO;
                    }
                } else {
                    if (idir == 1) {
                        double f = (Math.abs(d[ll]) - shift) * (sign(ONE, d[ll]) + shift / d[ll]);
                        double g = e[ll];
                        for (int i = ll; i <= m - 1; i++) {
                            dlartg(f, g, rot);
                            final double cosr = rot[0], sinr = rot[1];
                            if (i > ll) e[i - 1] = rot[2];
                            f = cosr * d[i] + sinr * e[i];
                            e[i] = cosr * e[i] - sinr * d[i];
                            g = sinr * d[i + 1];
                            d[i + 1] = cosr * d[i + 1];
                            dlartg(f, g, rot);
                            final double cosl = rot[0], sinl = rot[1];
                            d[i] = rot[2];
                            f = cosl * e[i] + sinl * d[i + 1];
                            d[i + 1] = cosl * d[i + 1] - sinl * e[i];
                            if (i < m - 1) {
                                g = sinl * e[i + 1];
                                e[i + 1] = cosl * e[i + 1];
                            }
                            work[i - ll + 1] = cosr;
                            work[i - ll + 1 + nm1] = sinr;
                            work[i - ll + 1 + nm12] = cosl;
                            work[i - ll + 1 + nm13] = sinl;
                        }
                        e[m - 1] = f;
                        if (ncvt > 0) dlasrLV(true, m - ll + 1, ncvt, work, 1, work, n, vt, ll - 1, ldvt);
                        if (ncc > 0) dlasrLV(true, m - ll + 1, ncc, work, nm12 + 1, work, nm13 + 1, c, co + ll - 1, ldc);
                        if (Math.abs(e[m - 1]) <= thresh) e[m - 1] = ZERO;
                    } else {
                        double f = (Math.abs(d[m]) - shift) * (sign(ONE, d[m]) + shift / d[m]);
                        double g = e[m - 1];
                        for (int i = m; i >= ll + 1; i--) {
                            dlartg(f, g, rot);
                            final double cosr = rot[0], sinr = rot[1];
                            if (i < m) e[i] = rot[2];
                            f = cosr * d[i] + sinr * e[i - 1];
                            e[i - 1] = cosr * e[i - 1] - sinr * d[i];
                            g = sinr * d[i - 1];
                            d[i - 1] = cosr * d[i - 1];
                            dlartg(f, g, rot);
                            final double cosl = rot[0], sinl = rot[1];
                            d[i] = rot[2];
                            f = cosl * e[i - 1] + sinl * d[i - 1];
                            d[i - 1] = cosl * d[i - 1] - sinl * e[i - 1];
                            if (i > ll + 1) {
                                g = sinl * e[i - 2];
                                e[i - 2] = cosl * e[i - 2];
                            }
                            work[i - ll] = cosr;
                            work[i - ll + nm1] = -sinr;
                            work[i - ll + nm12] = cosl;
                            work[i - ll + nm13] = -sinl;
                        }
                        e[ll] = f;
                        if (Math.abs(e[ll]) <= thresh) e[ll] = ZERO;
                        if (ncvt > 0) dlasrLV(false, m - ll + 1, ncvt, work, nm12 + 1, work, nm13 + 1, vt, ll - 1, ldvt);
                        if (ncc > 0) dlasrLV(false, m - ll + 1, ncc, work, 1, work, n, c, co + ll - 1, ldc);
                    }
                }
            }
        }
        // all converged: make them positive, then sort decreasing
        for (int i = 1; i <= n; i++) {
            if (d[i] < ZERO) {
                d[i] = -d[i];
                if (ncvt > 0) for (int j = 0; j < ncvt; j++) vt[(i - 1) + j * ldvt] = -1.0 * vt[(i - 1) + j * ldvt];
            }
        }
        for (int i = 1; i <= n - 1; i++) {
            int isub = 1;
            double smin = d[1];
            for (int j = 2; j <= n + 1 - i; j++) {
                if (d[j] <= smin) {
                    isub = j;
                    smin = d[j];
                }
            }
            if (isub != n + 1 - i) {
                d[isub] = d[n + 1 - i];
                d[n + 1 - i] = smin;
                if (ncvt > 0) swapRows(ncvt, vt, 0, ldvt, isub, n + 1 - i);
                if (ncc > 0) swapRows(ncc, c, co, ldc, isub, n + 1 - i);
            }
        }
        return 0;
    }

    /** dlasdq 'U', sqre 0, no U: dbdsqr, then (as LAPACK 3.2.2) the singular values sorted ascending. */
    private static int dlasdq(int n, int ncvt, int ncc, double[] d, double[] e, double[] vt, int ldvt, double[] c,
                              int co, int ldc) {
        final int info = dbdsqr(n, ncvt, ncc, d, e, vt, ldvt, c, co, ldc);
        if (info != 0) return info;
        for (int i = 1; i <= n; i++) {
            int isub = i;
            double smin = d[i];
            for (int j = i + 1; j <= n; j++) {
                if (d[j] < smin) {
                    isub = j;
                    smin = d[j];
                }
            }
            if (isub != i) {
                d[isub] = d[i];
                d[i] = smin;
                if (ncvt > 0) swapRows(ncvt, vt, 0, ldvt, isub, i);
                if (ncc > 0) swapRows(ncc, c, co, ldc, isub, i);
            }
        }
        return info;
    }

    /** dlascl 'G' multiplier from cfrom to cto (one step for ordinary sizes, as LAPACK's loop). */
    private static double[] sclMuls(double cfrom, double cto) {
        final double smlnum = SAFMIN, bignum = ONE / smlnum;
        double cfromc = cfrom, ctoc = cto;
        double[] muls = new double[0];
        boolean done;
        do {
            final double cfrom1 = cfromc * smlnum, cto1 = ctoc / bignum;
            double mul;
            if (Math.abs(cfrom1) > Math.abs(ctoc) && ctoc != ZERO) {
                mul = smlnum;
                done = false;
                cfromc = cfrom1;
            } else if (Math.abs(cto1) > Math.abs(cfromc)) {
                mul = bignum;
                done = false;
                ctoc = cto1;
            } else {
                mul = ctoc / cfromc;
                done = true;
            }
            muls = java.util.Arrays.copyOf(muls, muls.length + 1);
            muls[muls.length - 1] = mul;
        } while (!done);
        return muls;
    }

    private static void dlascl(double cfrom, double cto, int m, int n, double[] a, int ao, int lda) {
        for (double mul : sclMuls(cfrom, cto))
            for (int j = 0; j < n; j++)
                for (int i = 0; i < m; i++) a[ao + i + j * lda] = a[ao + i + j * lda] * mul;
    }

    /** dlalsd 'U', n <= smlsiz: solves the bidiagonal least squares into B's first n rows. Returns the rank. */
    private static int dlalsd(int n, int nrhs, double[] d, double[] e, double[] b, int ldb, double rcond) {
        final double rcnd = rcond <= ZERO || rcond >= ONE ? EPS : rcond;
        int rank = 0;
        // d[1..n], e[1..n-1] here (1-based)
        double orgnrm = Math.abs(d[n]);
        for (int i = 1; i <= n - 1; i++) {
            orgnrm = max(orgnrm, Math.abs(d[i]));
            orgnrm = max(orgnrm, Math.abs(e[i]));
        }
        if (orgnrm == ZERO) {
            for (int j = 0; j < nrhs; j++)
                for (int i = 0; i < n; i++) b[i + j * ldb] = ZERO;
            return 0;
        }
        dlascl(orgnrm, ONE, n, 1, d, 1, n);
        dlascl(orgnrm, ONE, n - 1, 1, e, 1, n - 1);
        final double[] vt = new double[n * n];
        for (int i = 0; i < n; i++) vt[i + i * n] = ONE;
        if (dlasdq(n, n, nrhs, d, e, vt, n, b, 0, ldb) != 0) throw new IllegalStateException("dbdsqr: no convergence");
        int imax = 1;
        for (int i = 2; i <= n; i++) if (Math.abs(d[i]) > Math.abs(d[imax])) imax = i;
        final double tol = rcnd * Math.abs(d[imax]);
        for (int i = 1; i <= n; i++) {
            if (d[i] <= tol) {
                for (int j = 0; j < nrhs; j++) b[(i - 1) + j * ldb] = ZERO;
            } else {
                dlascl(d[i], ONE, 1, nrhs, b, i - 1, ldb);
                rank++;
            }
        }
        // B := VT' B (dgemm 'T', 'N', alpha 1, beta 0)
        final double[] col = new double[n];
        for (int j = 0; j < nrhs; j++) {
            for (int i = 0; i < n; i++) {
                double temp = ZERO;
                for (int l = 0; l < n; l++) temp += vt[l + i * n] * b[l + j * ldb];
                col[i] = ONE * temp;
            }
            for (int i = 0; i < n; i++) b[i + j * ldb] = col[i];
        }
        dlascl(ONE, orgnrm, n, 1, d, 1, n);
        // (dlasrt 'D' on d: only the singular values' order)
        final double[] s = java.util.Arrays.copyOfRange(d, 1, n + 1);
        java.util.Arrays.sort(s);
        for (int i = 0; i < n; i++) d[i + 1] = s[n - 1 - i];
        dlascl(orgnrm, ONE, n, nrhs, b, 0, ldb);
        return rank;
    }

    /**
     * dgelsd for m >= n, m >= int(1.6 n), n <= 25: A (m x n, column-major, overwritten), B (m x nrhs, column-major,
     * ldb = m; the solution in its first n rows), s = the singular values (n). Returns the rank.
     */
    static int dgelsd(int m, int n, int nrhs, double[] a, double[] b, double[] s, double rcond) {
        if (m < n || m < (int) ((float) Math.min(m, n) * 1.6f) || n > 25) throw new IllegalArgumentException("dgelsd: not the kit fit's shape");
        final int lda = m, ldb = m;
        final double smlnum = SAFMIN / PREC, bignum = ONE / smlnum;
        double anrm = ZERO;
        for (int i = 0; i < m * n; i++) anrm = max(anrm, Math.abs(a[i]));
        if (anrm == ZERO) {
            java.util.Arrays.fill(b, 0, m * nrhs, ZERO);
            java.util.Arrays.fill(s, 0, n, ZERO);
            return 0;
        }
        if (anrm < smlnum || anrm > bignum) throw new IllegalArgumentException("dgelsd: A needs scaling");
        double bnrm = ZERO;
        for (int i = 0; i < m * nrhs; i++) bnrm = max(bnrm, Math.abs(b[i]));
        if (bnrm == ZERO) {
            java.util.Arrays.fill(b, 0, m * nrhs, ZERO);
            java.util.Arrays.fill(s, 0, n, ZERO);
            return 0;
        }
        if (bnrm < smlnum || bnrm > bignum) throw new IllegalArgumentException("dgelsd: B needs scaling");
        final double[] work = new double[Math.max(m, Math.max(n, nrhs)) + 1];
        // Path 1a: A = QR, B := Q' B, R's lower part zeroed
        final double[] tau = new double[n];
        dgeqr2(m, n, a, lda, tau, work);
        dorm2rLT(m, nrhs, n, a, lda, tau, 0, b, ldb, work);
        for (int j = 1; j <= n - 1; j++)
            for (int i = j + 1; i <= n; i++) a[(i - 1) + (j - 1) * lda] = ZERO;
        // R = Q B P', B := Q' B
        final double[] d = new double[n + 1], e = new double[n + 1];
        final double[] dd = new double[n], ee = new double[n];
        final double[] tauq = new double[n], taup = new double[n];
        dgebd2(n, n, a, lda, dd, ee, tauq, taup, work);
        System.arraycopy(dd, 0, d, 1, n);
        System.arraycopy(ee, 0, e, 1, n);
        dorm2rLT(n, nrhs, n, a, lda, tauq, 0, b, ldb, work);
        final int rank = dlalsd(n, nrhs, d, e, b, ldb, rcond);
        System.arraycopy(d, 1, s, 0, n);
        // B := P B (dormbr 'P', 'L', 'N' -> dormlq 'L', 'T' on rows 2..n with A(1, 2))
        if (n > 1) dorml2LT(n - 1, nrhs, n - 1, a, lda, lda, taup, 0, b, 1, ldb, work);
        return rank;
    }
}
