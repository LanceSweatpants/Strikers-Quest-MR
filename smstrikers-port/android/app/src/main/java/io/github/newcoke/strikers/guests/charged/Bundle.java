package io.github.newcoke.strikers.guests.charged;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.GxTex;

import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Charged texture bundles (tools/outside/chargedres.py): a front-end BundleFile (.res / .dmn: u32 32, u32 count,
 * u32 directory / 32, u32 data / 32; entries u32 hash, u32 offset / 32, u32 size) or a PTLG bundle (.rlt). Each
 * record = GXTextureHeader (levels, eGXTextureFormat, u16 w @0x0E, u16 h @0x10, u32 palette entries @0x14) + pixels
 * (+ the CI8 palette after every mip level). Entries are keyed by nlStringLowerHash of the texture's path.
 */
final class Bundle {
    private final byte[] b;
    private final int[] hash;
    private final long[] from, to;  // each record = b[from:to] (Python slice, clamped)

    private Bundle(byte[] b, int[] hash, long[] from, long[] to) {
        this.b = b;
        this.hash = hash;
        this.from = from;
        this.to = to;
    }

    static Bundle read(byte[] b) throws IOException {
        final boolean ptlg = b.length >= 4 && b[0] == 'P' && b[1] == 'T' && b[2] == 'L' && b[3] == 'G';
        if (!ptlg && Bin.u32(b, 0) != 32) throw new IOException("not a BundleFile");
        final int n = count(Bin.u32L(b, 4), b.length);
        final int[] h = new int[n];
        final long[] f = new long[n], t = new long[n];
        if (ptlg) {
            final long base = 16 + 16L * n;
            for (int i = 0; i < n; i++) {
                final int e = 16 + 16 * i;
                h[i] = Bin.u32(b, e);
                f[i] = base + Bin.u32L(b, e + 4);
                t[i] = f[i] + Bin.u32L(b, e + 8);
                Bin.u32(b, e + 12);
            }
        } else {
            final long dir = Bin.u32L(b, 8) * 32;
            for (int i = 0; i < n; i++) {
                final int e = (int) (dir + 12L * i);
                h[i] = Bin.u32(b, e);
                f[i] = Bin.u32L(b, e + 4) * 32;
                t[i] = f[i] + Bin.u32L(b, e + 8);
            }
        }
        return new Bundle(b, h, f, t);
    }

    private static int count(long n, int len) throws IOException {
        if (n > len / 12) throw new IOException("bundle: " + n + " entries");
        return (int) n;
    }

    /** chargedhash.h: h = -1; h += h &lt;&lt; 5; h += c, over the UTF-8 bytes, A-Z lowered when lower. */
    static int hash(String s, boolean lower) {
        int v = 0xFFFFFFFF;
        for (byte x : s.getBytes(StandardCharsets.UTF_8)) {
            int c = x & 0xFF;
            if (lower && c >= 65 && c <= 90) c += 32;
            v = v + (v << 5) + c;
        }
        return v;
    }

    /** A decoded picture. */
    static final class Pic {
        final int w, h;
        final byte[] rgba;

        Pic(int w, int h, byte[] rgba) {
            this.w = w;
            this.h = h;
            this.rgba = rgba;
        }
    }

    /**
     * build_portraits.py ResIndex.img(name): the entry of the path's lower-case hash, else of its plain hash; of
     * several with one hash, the last that chargedres decoded (the dump's index keeps the last). Null if none.
     */
    Pic find(String path) {
        Pic p = lastDecoded(hash(path, true));
        if (p == null) p = lastDecoded(hash(path, false));
        return p;
    }

    private Pic lastDecoded(int hv) {
        for (int i = hash.length - 1; i >= 0; i--) {
            if (hash[i] != hv) continue;
            final byte[] rec = Chunks.slice(b, from[i], to[i]);
            if (rec.length < 0x20) continue;
            final Pic p = decode(rec);
            if (p != null) return p;
        }
        return null;
    }

    /** eGXTextureFormat -> GX format (6 = A8 read as I8, then white). */
    private static final int[] ENGINE_FMT = {GxTex.RGB565, GxTex.RGB5A3, GxTex.CMPR, GxTex.RGBA8, GxTex.I8, GxTex.I4,
            GxTex.I8, GxTex.IA8, GxTex.C8};

    /** chargedres.py decode; null where it fails (an unknown format, data or palette too short). */
    static Pic decode(byte[] t) {
        try {
            final long levels = Bin.u32L(t, 0), efmt = Bin.u32L(t, 4);
            final int w = Bin.u16(t, 0x0E), h = Bin.u16(t, 0x10);
            final long npal = Bin.u32L(t, 0x14);
            if (efmt >= ENGINE_FMT.length) return null;
            final int fmt = ENGINE_FMT[(int) efmt];
            int[] pal = null;
            if (fmt == GxTex.C8) {
                long total = 0;
                int lw = w, lh = h;
                final long nl = Math.min(Math.max(levels, 1), 64);
                for (long k = 0; k < nl; k++) {
                    total += GxTex.sizeOf(fmt, lw, lh);
                    lw = Math.max(lw / 2, 1);
                    lh = Math.max(lh / 2, 1);
                }
                final long dataLen = t.length - 0x20;
                final long from = Math.min(total, dataLen), to = Math.min(total + npal * 2, dataLen);
                final int count = (int) Math.max(0, (to - from) / 2);
                pal = GxTex.palette(t, (int) (0x20 + from), count, 2);
            }
            final byte[] rgba = GxTex.decode(t, 0x20, fmt, w, h, pal);
            if (efmt == 6) for (int i = 0; i < rgba.length; i += 4) rgba[i] = rgba[i + 1] = rgba[i + 2] = (byte) 255;
            return new Pic(w, h, rgba);
        } catch (RuntimeException e) {  // (Python: the entry is FAILED in the dump's index)
            return null;
        }
    }
}
