package io.github.newcoke.strikers.guests.audio;

import java.io.IOException;
import java.util.Arrays;

/**
 * GameCube / Wii DSP-ADPCM (tools/outside/voicex.py dsp_decode, the same arithmetic): 8-byte frames, a header byte
 * (predictor index << 4 | scale) and 14 sample nibbles. Nibble addresses are absolute from {@code base} in the file
 * (Melee .ssm: its data area; Charged .nlxwb: the file's start), frame headers included, end inclusive.
 */
final class Dsp {
    private Dsp() {}

    static short[] decode(byte[] d, int base, long startNibble, long endNibble, short[] coefs, int ps, int hist1, int hist2)
            throws IOException {
        if (startNibble < 0 || endNibble < startNibble - 1 || base + (endNibble >> 1) >= d.length)
            throw new IOException("DSP nibbles " + startNibble + ".." + endNibble + " outside the file");
        int start = (int) startNibble, end = (int) endNibble;
        short[] out = new short[Math.max(0, end - start + 1)];
        int k = 0;
        long h1 = hist1, h2 = hist2;
        int n = start;
        // start may point past a frame header (nibble % 16 == 2): read that header
        if (n % 16 >= 2) ps = d[base + (n / 16) * 8] & 0xFF;
        int c1 = 0, c2 = 0, scale = 0, framePs = -1;
        while (n <= end) {
            if (n % 16 == 0) {
                ps = d[base + n / 2] & 0xFF;
                n += 2;
                continue;
            }
            if (ps != framePs) {
                framePs = ps;
                scale = 1 << (ps & 0xF);
                final int idx = (ps >> 4) & 7;
                c1 = coefs[idx * 2];
                c2 = coefs[idx * 2 + 1];
            }
            final int b = d[base + (n >> 1)] & 0xFF;
            int v = (n & 1) != 0 ? (b & 0xF) : (b >> 4);
            if (v >= 8) v -= 16;
            long s = (((long) v * scale) << 11) + 1024 + c1 * h1 + c2 * h2;
            s >>= 11;
            if (s > 32767) s = 32767;
            else if (s < -32768) s = -32768;
            out[k++] = (short) s;
            h2 = h1;
            h1 = s;
            n++;
        }
        return k == out.length ? out : Arrays.copyOf(out, k);
    }

    /** Coefficients: 16 big-endian s16 at o. */
    static short[] coefs(byte[] b, int o) {
        short[] c = new short[16];
        for (int i = 0; i < 16; i++) c[i] = (short) (((b[o + 2 * i] & 0xFF) << 8) | (b[o + 2 * i + 1] & 0xFF));
        return c;
    }
}
