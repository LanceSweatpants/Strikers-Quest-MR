package io.github.newcoke.strikers.guests.audio;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * Melee's streamed music (audio/*.hps, " HALPST") to the franchise evolution's EVM1 (tools/outside/hps2evm.py, the
 * same arithmetic): BE u32 rate, u32 channels; per channel 0x38 bytes (+0x10: 16 coefficients); blocks from 0x80:
 * u32 size (all channels), u32 last nibble, u32 next block (-1, or a loop back = the end), per channel ps / hist1 /
 * hist2 / pad, then each channel's data (size / channels) from block + 0x20.
 *
 * EVM1 (little-endian): "EVM1", u32 rate (32000), u32 frames, s16 stereo interleaved.
 */
final class Hps {
    private Hps() {}

    static final int RATE = 32000;

    /** {rate, left, right}: the track from its start until both channels have at least {@code need} samples (or its end). */
    static Object[] decode(byte[] b, long need) throws IOException {
        if (b.length < 0x80 || !new String(b, 0, 8, "ISO-8859-1").equals(" HALPST\0")) throw new IOException("not an HPS");
        final int rate = Bin.u32(b, 8), chans = Bin.u32(b, 12);
        if (chans < 1 || chans > 2) throw new IOException("HPS with " + chans + " channels");
        short[][] coefs = new short[chans][];
        for (int c = 0; c < chans; c++) coefs[c] = Dsp.coefs(b, 0x10 + 0x38 * c + 0x10);
        short[][] out = new short[chans][1 << 16];
        int[] len = new int[chans];
        long off = 0x80;
        Set<Long> seen = new HashSet<>();
        while (off >= 0 && off < b.length && seen.add(off)) {
            if (off + 12 > b.length) throw new IOException("HPS block cut short");
            final int o = (int) off;
            final long size = Bin.u32L(b, o), last = Bin.u32L(b, o + 4), nxt = Bin.u32L(b, o + 8);
            final long per = size / chans;
            final long data = off + 0x20;
            final long nn = last + 1;
            final long count = (nn / 16) * 14 + Math.max(0, (nn % 16) - 2);
            for (int c = 0; c < chans; c++) {
                long h1 = Bin.s16(b, o + 0x0C + 8 * c + 2), h2 = Bin.s16(b, o + 0x0C + 8 * c + 4);
                final long ds = Math.min(b.length, data + c * per), de = Math.min(b.length, data + (c + 1) * per);
                final short[] co = coefs[c];
                int taken = 0;
                for (long fr = ds; fr + 8 <= de; fr += 8) {
                    final int f = (int) fr;
                    final int ps = b[f] & 0xFF;
                    final int scale = 1 << (ps & 0xF);
                    final int idx = (ps >> 4) & 7;
                    final int c1 = co[idx * 2], c2 = co[idx * 2 + 1];
                    for (int i = 0; i < 14; i++) {
                        final int by = b[f + 1 + i / 2] & 0xFF;
                        int v = (i % 2 == 0) ? (by >> 4) : (by & 0xF);
                        if (v >= 8) v -= 16;
                        long s = (((long) v * scale) << 11) + 1024 + c1 * h1 + c2 * h2;
                        s >>= 11;
                        if (s > 32767) s = 32767;
                        else if (s < -32768) s = -32768;
                        h2 = h1;
                        h1 = s;
                        if (taken < count) {  // (the block's samples past its last nibble are dropped)
                            if (len[c] == out[c].length) out[c] = Arrays.copyOf(out[c], out[c].length * 2);
                            out[c][len[c]++] = (short) s;
                            taken++;
                        }
                    }
                }
            }
            if (nxt == 0xFFFFFFFFL || nxt <= off) break;  // the end, or the loop back: stop
            off = nxt;
            boolean enough = true;
            for (int c = 0; c < chans; c++) enough &= len[c] >= need;
            if (enough) break;  // (the rest can't change the samples asked for)
        }
        int n = len[0];
        for (int c = 1; c < chans; c++) n = Math.min(n, len[c]);
        final short[] left = Arrays.copyOf(out[0], n), right = chans == 1 ? left : Arrays.copyOf(out[1], n);
        return new Object[] {rate, left, right};
    }

    /** hps2evm resample: linear, as the PC tool computes it (doubles, truncation toward zero). */
    static int[] resample(int[] x, int src, int dst) {
        if (src == dst) return x.clone();
        final int n = (int) ((double) ((long) x.length * dst) / src);
        int[] r = new int[n];
        for (int k = 0; k < n; k++) {
            final double p = (double) ((long) k * src) / dst;
            final int i = (int) p;
            final double f = p - i;
            final int a = i < x.length ? x[i] : 0;
            final int c = i + 1 < x.length ? x[i + 1] : a;
            r[k] = (int) (a + (c - a) * f);
        }
        return r;
    }

    /**
     * hps2evm evm: [start, start + length) seconds (length &lt; 0: to the end), resampled to 32 kHz, faded in and out
     * (seconds), as EVM1 bytes.
     */
    static byte[] evm(byte[] hps, double start, double length, double fadeIn, double fadeOut) throws IOException {
        final int rate = rateOf(hps);
        final long a = (long) (start * rate);
        final long z = length < 0 ? Long.MAX_VALUE : (long) ((start + length) * rate);
        Object[] d = decode(hps, z);
        final short[] l0 = (short[]) d[1], r0 = (short[]) d[2];
        final int from = (int) Math.min(a, l0.length), to = (int) Math.max(from, Math.min(z, l0.length));
        int[] L = new int[to - from], R = new int[to - from];
        for (int i = from; i < to; i++) {
            L[i - from] = l0[i];
            R[i - from] = r0[i];
        }
        L = resample(L, rate, RATE);
        R = resample(R, rate, RATE);
        final int n = L.length;
        Out o = new Out();
        o.bytes(new byte[] {'E', 'V', 'M', '1'}).le32(RATE).le32(n);
        for (int i = 0; i < n; i++) {
            double g = 1.0;
            if (fadeIn > 0 && i < fadeIn * 32000) g = i / (fadeIn * 32000);
            if (fadeOut > 0 && i > n - fadeOut * 32000) g = Math.min(g, (n - i) / (fadeOut * 32000));
            o.le16((int) (L[i] * g)).le16((int) (R[i] * g));
        }
        return o.toBytes();
    }

    private static int rateOf(byte[] b) throws IOException {
        if (b.length < 0x10) throw new IOException("not an HPS");
        return Bin.u32(b, 8);
    }
}
