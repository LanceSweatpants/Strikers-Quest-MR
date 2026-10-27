package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.GxTex;

import java.io.IOException;
import java.util.Arrays;
import java.util.BitSet;
import java.util.HashMap;
import java.util.Map;

/**
 * A Melee HSD archive's pictures (tools/outside/hsdtex.py find(), the same rules): image descriptors (data ptr,
 * w, h, GX format) and palettes (TLUT descriptors) found through the relocation table, paired by the TObjs and
 * TexAnims that use them (a TexAnim's pairing wins over a TObj's).
 */
final class Hsd {
    private static final int D = 0x20;

    final byte[] b;
    final int dsize;
    private final BitSet rel = new BitSet();
    private final int[] relSorted;
    final Map<Integer, int[]> imgs = new HashMap<>();   // descriptor -> {data, w, h, fmt}
    final Map<Integer, int[]> tluts = new HashMap<>();  // descriptor -> {data, palette format, count}
    final Map<Integer, Integer> palFor = new HashMap<>();
    final Map<Integer, int[]> texAnims = new HashMap<>(); // TexAnim -> its frames' image descriptors

    Hsd(byte[] b) throws IOException {
        this.b = b;
        if (b.length < D) throw new IOException("not an HSD archive");
        dsize = Bin.u32(b, 4);
        final int nrel = Bin.u32(b, 8);
        if (dsize < 0 || nrel < 0 || D + (long) dsize + 4L * nrel > b.length) throw new IOException("HSD: bad header");
        int[] r = new int[nrel];
        for (int i = 0; i < nrel; i++) {
            r[i] = Bin.u32(b, D + dsize + 4 * i);
            if (r[i] >= 0) rel.set(r[i]);
        }
        Arrays.sort(r);
        int n = 0;
        for (int i = 0; i < nrel; i++) if (r[i] >= 0 && (n == 0 || r[n - 1] != r[i])) r[n++] = r[i];
        relSorted = Arrays.copyOf(r, n);
        find();
    }

    private long u32(int o) {
        return Bin.u32L(b, D + o);
    }

    private int u16(int o) {
        return Bin.u16(b, D + o);
    }

    /** The pointer stored at o, or -1 when o is not a relocated slot. */
    private long ptr(long o) {
        return o >= 0 && o < Integer.MAX_VALUE && rel.get((int) o) ? u32((int) o) : -1;
    }

    private static boolean gxFormat(long f) {
        return f == 0 || f == 1 || f == 2 || f == 3 || f == 4 || f == 5 || f == 6 || f == 8 || f == 9 || f == 14;
    }

    private boolean isImg(long o) {
        return o >= 0 && o < Integer.MAX_VALUE && imgs.containsKey((int) o);
    }

    private boolean isTlut(long o) {
        return o >= 0 && o < Integer.MAX_VALUE && tluts.containsKey((int) o);
    }

    private void find() {
        for (int o : relSorted) {
            if ((long) o + 0x10 > dsize) continue;
            final long p = u32(o);
            if (p >= dsize) continue;
            final int w = u16(o + 4), hh = u16(o + 6);
            final long fmt = u32(o + 8), mip = u32(o + 12);
            if (1 <= w && w <= 1024 && 1 <= hh && hh <= 1024 && gxFormat(fmt) && (mip == 0 || mip == 1) && p % 32 == 0) {
                if (p + GxTex.sizeOf((int) fmt, w, hh) <= dsize) imgs.put(o, new int[] {(int) p, w, hh, (int) fmt});
            }
            final long pf = u32(o + 4);
            final int n = u16(o + 12);
            if ((pf == 0 || pf == 1 || pf == 2) && 1 <= n && n <= 16384 && p + n * 2L <= dsize)
                tluts.put(o, new int[] {(int) p, (int) pf, n});
        }
        for (int o : relSorted) {
            // TObj: image desc ptr at 0x4C, tlut desc ptr at 0x50
            if (o >= 0x4C && (long) (o - 0x4C) + 0x5C <= dsize) {
                final int base = o - 0x4C;
                final long ip = u32(o);
                if (isImg(ip)) {
                    final long tp = ptr(base + 0x50);
                    if (isTlut(tp) && !palFor.containsKey((int) ip)) palFor.put((int) ip, (int) tp);
                }
            }
            // TexAnim: 0x0C image table, 0x10 tlut table, u16 counts at 0x14 / 0x16
            if (o >= 0x0C && (long) (o - 0x0C) + 0x18 <= dsize) {
                final int base = o - 0x0C;
                final long it = u32(o);
                final int ni = u16(base + 0x14), nt = u16(base + 0x16);
                if (0 < ni && ni < 2000 && it + ni * 4L <= dsize && nt <= ni) {
                    final int[] ent = new int[ni];
                    boolean all = true;
                    for (int k = 0; k < ni && all; k++) {
                        final long e = ptr(it + 4L * k);
                        if (!isImg(e)) all = false;
                        else ent[k] = (int) e;
                    }
                    if (all) {
                        final long tt = nt != 0 ? ptr(base + 0x10) : -1;
                        long[] tl = new long[0];
                        if (tt >= 0 && tt + nt * 4L <= dsize) {
                            tl = new long[nt];
                            for (int k = 0; k < nt; k++) tl[k] = ptr(tt + 4L * k);
                        }
                        texAnims.put(base, ent);
                        for (int k = 0; k < ni; k++) {
                            if (tl.length > 0) {
                                final long t = k < tl.length ? tl[k] : tl[tl.length - 1];
                                if (isTlut(t)) palFor.put(ent[k], (int) t);
                            }
                        }
                    }
                }
            }
        }
    }

    /** The image descriptor of a TexAnim's frame. */
    int frame(int texAnim, int k) throws IOException {
        int[] ent = texAnims.get(texAnim);
        if (ent == null || k < 0 || k >= ent.length) throw new IOException("HSD: no TexAnim frame " + Integer.toHexString(texAnim) + "[" + k + "]");
        return ent[k];
    }

    /** The picture of an image descriptor, as hsdtex.py writes it (RGBA, with its palette when it has one). */
    byte[] decode(int desc, int[] wh) throws IOException {
        final int[] im = imgs.get(desc);
        if (im == null) throw new IOException("HSD: no image descriptor at " + Integer.toHexString(desc));
        final int p = im[0], w = im[1], h = im[2], fmt = im[3];
        int[] pal = null;
        final Integer tp = palFor.get(desc);
        if (tp != null) {
            final int[] t = tluts.get(tp);
            pal = GxTex.palette(b, D + t[0], t[2], t[1]);
        } else if (fmt == GxTex.C4 || fmt == GxTex.C8) {
            throw new IOException("HSD: image " + Integer.toHexString(desc) + " has no palette");
        }
        wh[0] = w;
        wh[1] = h;
        try {
            return GxTex.decode(b, D + p, fmt, w, h, pal);
        } catch (ArrayIndexOutOfBoundsException e) {
            throw new IOException("HSD: image " + Integer.toHexString(desc) + " doesn't decode");
        }
    }
}
