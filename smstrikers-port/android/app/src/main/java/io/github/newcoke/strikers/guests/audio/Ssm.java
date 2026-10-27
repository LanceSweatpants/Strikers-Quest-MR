package io.github.newcoke.strikers.guests.audio;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;

/**
 * A Melee .ssm sound bank (HAL; tools/outside/voicex.py Ssm): 0x00 header size, 0x04 data size, 0x08 sound count,
 * 0x0C start index; from 0x10 per sound: channel count, rate, then per channel a 0x40-byte DSP header (loop u16,
 * format u16, loop start, end nibble (inclusive), start nibble, 16 coefs, gain, ps, hist1, hist2 ...). Nibble
 * addresses are from the data area, align32(header size + 0x10).
 */
final class Ssm {
    private final byte[] b;
    private final int dataOff;
    private final int[] soundAt;  // each sound's header offset

    Ssm(byte[] b) throws IOException {
        this.b = b;
        if (b.length < 0x10) throw new IOException("not an .ssm");
        final long hsize = Bin.u32L(b, 0);
        final int count = Bin.u32(b, 8);
        if (count < 0 || count > 100000 || hsize > b.length) throw new IOException("not an .ssm");
        dataOff = (int) ((hsize + 0x10 + 0x1F) & ~0x1FL);
        soundAt = new int[count];
        int o = 0x10;
        for (int i = 0; i < count; i++) {
            if (o + 8 > b.length) throw new IOException(".ssm header cut short");
            soundAt[i] = o;
            final int nch = Bin.u32(b, o);
            if (nch < 0 || nch > 8) throw new IOException(".ssm sound " + i + ": " + nch + " channels");
            o += 8 + 0x40 * nch;
        }
    }

    int count() {
        return soundAt.length;
    }

    /** The sound's rate, without decoding it. */
    int rate(int local) throws IOException {
        check(local);
        return Bin.u32(b, soundAt[local] + 4);
    }

    /** Every channel of the sound, as the game has it. */
    short[][] decode(int local) throws IOException {
        check(local);
        int o = soundAt[local];
        final int nch = Bin.u32(b, o);
        o += 8;
        short[][] pcm = new short[nch][];
        for (int c = 0; c < nch; c++, o += 0x40) {
            final long end = Bin.u32L(b, o + 8), start = Bin.u32L(b, o + 12);
            pcm[c] = Dsp.decode(b, dataOff, start, end, Dsp.coefs(b, o + 0x10), Bin.u16(b, o + 0x32) & 0xFF,
                    Bin.s16(b, o + 0x34), Bin.s16(b, o + 0x36));
        }
        return pcm;
    }

    private void check(int local) throws IOException {
        if (local < 0 || local >= soundAt.length) throw new IOException(".ssm has no sound " + local);
    }
}
