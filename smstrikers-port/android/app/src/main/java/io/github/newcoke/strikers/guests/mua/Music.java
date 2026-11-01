package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * tools/outside/mua/mua_evo_music.py: the heroes' costume-unlock music (guests/evo/music_hero_build.evm,
 * music_hero_win.evm) from MUA's global stream bank sounds/eng/x/_/x_voice.zss (ZSNDGCUB, mono DSP-ADPCM at 22050
 * Hz): a cut of a stream, band-limited (FFT) resampled to 32 kHz, linear fades, the gain that matches the Pokemon
 * pair's loudness (their two measured numbers are ours: guestdata/mua/music.txt), a 2 ms look-ahead limiter, both
 * channels the same. numpy's sums (pairwise) and orders kept; the FFT is our own (double precision, exact twiddles).
 */
final class Music {
    private Music() {}

    static final String BANK = "sounds/eng/x/_/x_voice.zss";
    static final int OUT_RATE = 32000;

    /** One cut (music.txt): out name, file index, start, length (NaN = to the end), fades, loops, reference peak / RMS (dB). */
    static final class Cut {
        String name;
        int file;
        double start, length, fadeIn, fadeOut, refPeakDb, refRmsDb;
        boolean loops;
    }

    static List<Cut> cuts(String text) throws IOException {
        final List<Cut> out = new ArrayList<>();
        for (String line : text.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] f = l.split("\\s+");
            if (f.length < 9) throw new IOException("mua/music.txt: " + l);
            final Cut c = new Cut();
            c.name = f[0];
            c.file = Integer.parseInt(f[1]);
            c.start = Double.parseDouble(f[2]);
            c.length = f[3].equals("-") ? Double.NaN : Double.parseDouble(f[3]);
            c.fadeIn = Double.parseDouble(f[4]);
            c.fadeOut = Double.parseDouble(f[5]);
            c.loops = f[6].equals("1");
            c.refPeakDb = Double.parseDouble(f[7]);
            c.refRmsDb = Double.parseDouble(f[8]);
            out.add(c);
        }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ ZSND
    static int be32(byte[] b, int o) {
        return ((b[o] & 0xFF) << 24) | ((b[o + 1] & 0xFF) << 16) | ((b[o + 2] & 0xFF) << 8) | (b[o + 3] & 0xFF);
    }

    static int s16(byte[] b, int o) {
        return (short) (((b[o] & 0xFF) << 8) | (b[o + 1] & 0xFF));
    }

    /** zsnd.parse + zsnd_music.decode for a mono stream: {rate} and its samples. */
    static int[] decode(byte[] b, int index, int[] rateOut) throws IOException {
        if (b.length < 0x34 || !new String(b, 0, 8, java.nio.charset.StandardCharsets.ISO_8859_1).equals("ZSNDGCUB"))
            throw new IOException("x_voice.zss: not a ZSNDGCUB bank");
        final int nfiles = be32(b, 0x10 + 24), froff = be32(b, 0x10 + 24 + 8);
        if (index >= nfiles) throw new IOException("x_voice.zss: no stream " + index);
        final int off = be32(b, froff + 12 * index), size = be32(b, froff + 12 * index + 4);
        rateOut[0] = be32(b, off + 8);
        // channels: back-to-back DSP headers (this one is mono)
        int chans = 1;
        while (chans < 8) {
            final int h = off + 0x60 * chans;
            boolean same = true;
            for (int k = 0; k < 4 && same; k++) same = b[h + 8 + k] == b[off + 8 + k];
            for (int k = 0; k < 8 && same; k++) same = b[h + 0x0C + k] == b[off + 0x0C + k];
            for (int k = 0; k < 4 && same; k++) same = b[h + 0x18 + k] == b[off + 0x18 + k];
            if (same) chans++;
            else break;
        }
        if (chans != 1) throw new IOException("x_voice.zss " + index + ": " + chans + " channels");
        final int ns = be32(b, off);
        final int[] coefs = new int[16];
        for (int k = 0; k < 16; k++) coefs[k] = s16(b, off + 0x1C + 2 * k);
        int h1 = s16(b, off + 0x40), h2 = s16(b, off + 0x42);
        final int data = off + 0x60, end = off + size;
        final int[] out = new int[ns];
        int n = 0;
        for (int fr = 0; fr < (ns + 13) / 14; fr++) {
            final int p = data + fr * 8;
            if (p + 8 > end) break;
            final int ps = b[p] & 0xFF;
            final int scale = 1 << (ps & 0xF);
            final int ci = (ps >> 4) & 7;
            final int c1 = coefs[ci * 2], c2 = coefs[ci * 2 + 1];
            for (int i = 0; i < 14; i++) {
                final int by = b[p + 1 + (i >> 1)] & 0xFF;
                int nib = (i & 1) == 0 ? by >> 4 : by & 0xF;
                if (nib >= 8) nib -= 16;
                int s = (((nib * scale) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11;
                if (s > 32767) s = 32767;
                else if (s < -32768) s = -32768;
                h2 = h1;
                h1 = s;
                if (n < ns) out[n++] = s;
            }
        }
        return java.util.Arrays.copyOf(out, n);
    }

    // ------------------------------------------------------------------------------------------------ the cut
    static byte[] evm(byte[] bank, Cut c) throws IOException {
        final int[] rr = new int[1];
        final int[] pcm = decode(bank, c.file, rr);
        final int rate = rr[0];
        final double total = pcm.length / (double) rate;
        final double length = Double.isNaN(c.length) ? total - c.start : c.length;
        final int pad = (int) (0.25 * rate);
        final int ns = pcm.length;
        final double[] ext = new double[ns + 2 * pad];
        for (int i = 0; i < pad; i++) {
            ext[i] = c.loops ? pcm[ns - pad + i] : 0.0;
            ext[pad + ns + i] = c.loops ? pcm[i] : 0.0;
        }
        for (int i = 0; i < ns; i++) ext[pad + i] = pcm[i];
        final int a = pad + (int) Math.rint(c.start * rate) - pad;
        final int z = pad + (int) Math.rint((c.start + length) * rate) + pad;
        final double[] seg = resample(java.util.Arrays.copyOfRange(ext, a, z), rate, OUT_RATE);
        final int o = (int) Math.rint(pad * OUT_RATE / (double) rate);
        final int n = (int) Math.rint(length * OUT_RATE);
        final double[] y = java.util.Arrays.copyOfRange(seg, o, o + n);
        // fades (linear)
        final double[] g = new double[n];
        java.util.Arrays.fill(g, 1.0);
        if (c.fadeIn > 0) {
            final int k = (int) (c.fadeIn * OUT_RATE);
            for (int i = 0; i < k; i++) g[i] = Math.min(g[i], i / (double) k);
        }
        if (c.fadeOut > 0) {
            final int k = (int) (c.fadeOut * OUT_RATE);
            for (int i = 0; i < k; i++) g[n - k + i] = Math.min(g[n - k + i], (k - i) / (double) k);
        }
        for (int i = 0; i < n; i++) y[i] = y[i] * g[i];
        // loudness: the reference's RMS, its peak the ceiling
        double gainDb = 0.0;
        double[] zz = y;
        for (int it = 0; it < 3; it++) {
            gainDb += c.refRmsDb - rmsDb(zz);
            final double gain = StrictMath.pow(10, gainDb / 20.0);
            final double[] yy = new double[n];
            for (int i = 0; i < n; i++) yy[i] = y[i] * gain;
            zz = limit(yy, c.refPeakDb, OUT_RATE);
        }
        final Out ev = new Out();
        ev.bytes("EVM1".getBytes(java.nio.charset.StandardCharsets.ISO_8859_1)).le32(OUT_RATE).le32(n);
        for (int i = 0; i < n; i++) {
            double v = Math.rint(zz[i]);
            if (v < -32768) v = -32768;
            if (v > 32767) v = 32767;
            ev.le16((int) v).le16((int) v);
        }
        return ev.toBytes();
    }

    /** measure()'s RMS in dBFS of a mono signal stacked as both channels: sqrt(mean(z^2)) (numpy's pairwise sum). */
    static double rmsDb(double[] z) {
        final double[] p = new double[z.length];
        for (int i = 0; i < z.length; i++) p[i] = z[i] * z[i];
        final double rms = Math.sqrt(pairwise(p, 0, p.length) / p.length);
        return 20 * StrictMath.log10(Math.max(rms, 1e-9) / 32768.0);
    }

    /** numpy's pairwise_sum (8 accumulators up to 128, halves above). */
    static double pairwise(double[] a, int lo, int n) {
        if (n < 8) {
            double res = 0.0;
            for (int i = 0; i < n; i++) res += a[lo + i];
            return res;
        }
        if (n <= 128) {
            final double[] r = new double[8];
            System.arraycopy(a, lo, r, 0, 8);
            int i = 8;
            for (; i < n - (n % 8); i += 8) for (int j = 0; j < 8; j++) r[j] += a[lo + i + j];
            double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
            for (; i < n; i++) res += a[lo + i];
            return res;
        }
        int n2 = n / 2;
        n2 -= n2 % 8;
        return pairwise(a, lo, n2) + pairwise(a, lo + n2, n - n2);
    }

    /** The look-ahead limiter (2 ms, 50 ms release) for the few transients over the ceiling. */
    static double[] limit(double[] y, double ceilingDb, int rate) {
        final double thr = 32768.0 * StrictMath.pow(10, ceilingDb / 20.0);
        final int n = y.length;
        final double[] a = new double[n];
        double amax = 0;
        for (int i = 0; i < n; i++) {
            a[i] = Math.abs(y[i]);
            if (i == 0 || a[i] > amax) amax = a[i];
        }
        if (amax <= thr) return y;
        final int w = Math.max(1, (int) (0.002 * rate));
        final double[] padded = new double[n + 2 * w];
        java.util.Arrays.fill(padded, 1.0);
        for (int i = 0; i < n; i++) padded[w + i] = Math.min(1.0, thr / Math.max(a[i], 1e-9));
        final int win = 2 * w + 1;
        final int ng = padded.length - win + 1;  // = n
        final double[] gmin = new double[ng];
        for (int i = 0; i < ng; i++) {
            double m = padded[i];
            for (int k = 1; k < win; k++) if (padded[i + k] < m) m = padded[i + k];
            gmin[i] = m;
        }
        final double[] c = new double[ng + 2 * w + 1];
        c[0] = 0.0;
        for (int i = 0; i < ng + 2 * w; i++) {
            final double v = i < w || i >= w + ng ? 1.0 : gmin[i - w];
            c[i + 1] = c[i] + v;
        }
        final double r = StrictMath.exp(-1.0 / (0.05 * rate));
        final double[] out = new double[n];
        double prev = 1.0;
        for (int i = 0; i < n; i++) {
            final double gavg = (c[i + win] - c[i]) / win;
            final double cand = 1.0 - (1.0 - prev) * r;
            prev = gavg <= cand ? gavg : cand;
            out[i] = y[i] * prev;
        }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ FFT
    /** resample(): band-limited, rfft -> zero-padded spectrum -> irfft(m) * (m / n). */
    static double[] resample(double[] x, int src, int dst) {
        final int n = x.length;
        final int m = (int) Math.rint((long) n * dst / (double) src);
        final double[] re = x.clone(), im = new double[n];
        fft(re, im, false);
        im[0] = 0.0;  // (numpy's real FFT: exactly real at 0 and at n / 2)
        if (n % 2 == 0) im[n / 2] = 0.0;
        final int kx = n / 2 + 1, ky = m / 2 + 1, k = Math.min(kx, ky);
        // the full spectrum of length m (Hermitian)
        final double[] zr = new double[m], zi = new double[m];
        for (int i = 0; i < k; i++) {
            zr[i] = re[i];
            zi[i] = im[i];
        }
        zi[0] = 0.0;
        if (m % 2 == 0 && k == ky) zi[m / 2] = 0.0;
        for (int i = 1; i < (m + 1) / 2; i++) {
            zr[m - i] = zr[i];
            zi[m - i] = -zi[i];
        }
        fft(zr, zi, true);
        final double[] out = new double[m];
        final double scale = m / (double) n, fct = 1.0 / m;
        for (int i = 0; i < m; i++) out[i] = zr[i] * fct * scale;
        return out;
    }

    /** In-place complex DFT of any length (Bluestein over a power-of-two FFT); inverse: e^(+i...), no 1/n. */
    static void fft(double[] re, double[] im, boolean inverse) {
        final int n = re.length;
        if (n <= 1) return;
        if ((n & (n - 1)) == 0) {
            radix2(re, im, inverse);
            return;
        }
        int nn = 1;
        while (nn < 2 * n - 1) nn <<= 1;
        // chirp w_k = exp(-+ i pi k^2 / n), k^2 taken mod 2n for an exact small angle
        final double[] wr = new double[n], wi = new double[n];
        final double sgn = inverse ? 1.0 : -1.0;
        for (int k = 0; k < n; k++) {
            final long kk = ((long) k * k) % (2L * n);
            final double ang = Math.PI * kk / n;
            wr[k] = StrictMath.cos(ang);
            wi[k] = sgn * StrictMath.sin(ang);
        }
        final double[] ar = new double[nn], ai = new double[nn], br = new double[nn], bi = new double[nn];
        for (int k = 0; k < n; k++) {
            ar[k] = re[k] * wr[k] - im[k] * wi[k];
            ai[k] = re[k] * wi[k] + im[k] * wr[k];
        }
        br[0] = wr[0];
        bi[0] = -wi[0];
        for (int k = 1; k < n; k++) {
            br[k] = wr[k];
            bi[k] = -wi[k];
            br[nn - k] = wr[k];
            bi[nn - k] = -wi[k];
        }
        radix2(ar, ai, false);
        radix2(br, bi, false);
        for (int k = 0; k < nn; k++) {
            final double r = ar[k] * br[k] - ai[k] * bi[k];
            final double i = ar[k] * bi[k] + ai[k] * br[k];
            ar[k] = r;
            ai[k] = i;
        }
        radix2(ar, ai, true);
        for (int k = 0; k < n; k++) {
            final double r = ar[k] / nn, i = ai[k] / nn;
            re[k] = r * wr[k] - i * wi[k];
            im[k] = r * wi[k] + i * wr[k];
        }
    }

    private static void radix2(double[] re, double[] im, boolean inverse) {
        final int n = re.length;
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                double t = re[i];
                re[i] = re[j];
                re[j] = t;
                t = im[i];
                im[i] = im[j];
                im[j] = t;
            }
        }
        final double sgn = inverse ? 1.0 : -1.0;
        for (int len = 2; len <= n; len <<= 1) {
            final int half = len >> 1;
            final double[] cr = new double[half], ci = new double[half];
            for (int k = 0; k < half; k++) {
                final double ang = 2 * Math.PI * k / len;
                cr[k] = StrictMath.cos(ang);
                ci[k] = sgn * StrictMath.sin(ang);
            }
            for (int i = 0; i < n; i += len) {
                for (int k = 0; k < half; k++) {
                    final int u = i + k, v = u + half;
                    final double xr = re[v] * cr[k] - im[v] * ci[k];
                    final double xi = re[v] * ci[k] + im[v] * cr[k];
                    re[v] = re[u] - xr;
                    im[v] = im[u] - xi;
                    re[u] += xr;
                    im[u] += xi;
                }
            }
        }
    }
}
