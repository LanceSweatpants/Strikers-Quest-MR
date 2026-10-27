package io.github.newcoke.strikers.guests.audio;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;

/**
 * A Mario Strikers Charged resident sound bank (tools/outside/voicex.py NlxBank, only what a clip needs):
 * audio/&lt;bank&gt;.resbun is an NLG chunk tree (u32 id with the alignment exponent in bits 24..30, u32 size); its
 * top chunk's child 0x23703 is the resident sample table (u32 count; per sample 28 bytes: loop, rate, ..., end
 * nibble at +16, start nibble at +20; then per sample a 46-byte DSP block: 16 coefs, gain, ps, hist1, hist2), and
 * audio/&lt;bank&gt;.nlxwb holds the raw DSP-ADPCM those nibble addresses point into.
 */
final class Nlx {
    private final byte[] res;
    private final int table;  // the 0x23703 chunk's data
    private final int count;

    Nlx(byte[] resbun) throws IOException {
        res = resbun;
        final long[] top = chunk(resbun, 0);
        int found = -1;
        for (long s = top[1]; s < top[2]; ) {
            final long[] c = chunk(resbun, (int) s);
            if (c[0] == 0x23703) found = (int) c[1];  // (the last one, as the PC tool's dict keeps)
            s = c[3];
        }
        if (found < 0 || found + 4 > resbun.length) throw new IOException("no resident sample table");
        table = found;
        count = Bin.u32(resbun, table);
        if (count < 0 || table + 4 + 74L * count > resbun.length) throw new IOException("sample table cut short");
    }

    /** {id & 0x80FFFFFF, data start (aligned), data end, next chunk} */
    private static long[] chunk(byte[] b, int o) throws IOException {
        if (o < 0 || o + 8 > b.length) throw new IOException("chunk outside the file");
        final long raw = Bin.u32L(b, o), size = Bin.u32L(b, o + 4);
        final int e = (int) ((raw >> 24) & 0x7F);
        if (e > 30) throw new IOException("chunk alignment 2^" + e);
        final long stop = o + 8 + size;
        final long align = 1L << e;
        final long start = (o + 8 + align - 1) & ~(align - 1);
        return new long[] {raw & 0x80FFFFFFL, start, stop, (stop + 3) & ~3L};
    }

    int count() {
        return count;
    }

    int rate(int sp) throws IOException {
        check(sp);
        return Bin.u32(res, table + 4 + 28 * sp + 4);
    }

    short[] decode(int sp, byte[] wave) throws IOException {
        check(sp);
        final int a = table + 4 + 28 * sp, d = table + 4 + 28 * count + 46 * sp;
        final long end = Bin.u32L(res, a + 16), start = Bin.u32L(res, a + 20);
        return Dsp.decode(wave, 0, start, end, Dsp.coefs(res, d), Bin.u16(res, d + 34) & 0xFF, Bin.s16(res, d + 36),
                Bin.s16(res, d + 38));
    }

    private void check(int sp) throws IOException {
        if (sp < 0 || sp >= count) throw new IOException("bank has no sample " + sp);
    }
}
