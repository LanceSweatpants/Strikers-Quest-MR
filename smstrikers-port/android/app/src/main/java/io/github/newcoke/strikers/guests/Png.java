package io.github.newcoke.strikers.guests;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.zip.CRC32;
import java.util.zip.DataFormatException;
import java.util.zip.Deflater;
import java.util.zip.Inflater;

/** RGBA PNGs: written (8-bit RGBA, filter 0, as gxtex.py's write_png) and read back (any 8-bit colour type). */
public final class Png {
    private Png() {}

    public static final class Image {
        public final int w, h;
        public final byte[] rgba;

        public Image(int w, int h, byte[] rgba) {
            this.w = w;
            this.h = h;
            this.rgba = rgba;
        }
    }

    public static byte[] encode(byte[] rgba, int w, int h) {
        final byte[] raw = new byte[(w * 4 + 1) * h];
        for (int y = 0; y < h; y++) System.arraycopy(rgba, y * w * 4, raw, y * (w * 4 + 1) + 1, w * 4);
        final Deflater d = new Deflater(6);
        d.setInput(raw);
        d.finish();
        final ByteArrayOutputStream z = new ByteArrayOutputStream(raw.length / 2 + 64);
        final byte[] buf = new byte[1 << 16];
        while (!d.finished()) z.write(buf, 0, d.deflate(buf));
        d.end();
        final Out o = new Out();
        o.bytes(new byte[] {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'});
        final Out ihdr = new Out().be32(w).be32(h).u8(8).u8(6).u8(0).u8(0).u8(0);
        chunk(o, "IHDR", ihdr.toBytes());
        chunk(o, "IDAT", z.toByteArray());
        chunk(o, "IEND", new byte[0]);
        return o.toBytes();
    }

    public static void write(File f, byte[] rgba, int w, int h) throws IOException {
        Out.save(f, encode(rgba, w, h));
    }

    private static void chunk(Out o, String type, byte[] data) {
        final byte[] t = type.getBytes(StandardCharsets.ISO_8859_1);
        o.be32(data.length).bytes(t).bytes(data);
        final CRC32 crc = new CRC32();
        crc.update(t);
        crc.update(data);
        o.be32((int) crc.getValue());
    }

    /** Any non-interlaced 8-bit PNG (grey, grey + alpha, RGB, RGBA, palette) as RGBA. */
    public static Image decode(byte[] png) throws IOException {
        if (png.length < 8 || (png[0] & 0xFF) != 0x89 || png[1] != 'P') throw new IOException("not a PNG");
        int p = 8, w = 0, h = 0, depth = 0, type = 0, interlace = 0;
        byte[] plte = null, trns = null;
        final ByteArrayOutputStream idat = new ByteArrayOutputStream();
        while (p + 8 <= png.length) {
            final int len = Bin.u32(png, p);
            final String t = new String(png, p + 4, 4, StandardCharsets.ISO_8859_1);
            final int d = p + 8;
            if (t.equals("IHDR")) {
                w = Bin.u32(png, d);
                h = Bin.u32(png, d + 4);
                depth = png[d + 8] & 0xFF;
                type = png[d + 9] & 0xFF;
                interlace = png[d + 12] & 0xFF;
            } else if (t.equals("PLTE")) {
                plte = Bin.slice(png, d, len);
            } else if (t.equals("tRNS")) {
                trns = Bin.slice(png, d, len);
            } else if (t.equals("IDAT")) {
                idat.write(png, d, len);
            } else if (t.equals("IEND")) {
                break;
            }
            p = d + len + 4;
        }
        if (depth != 8 || interlace != 0) throw new IOException("PNG: only 8-bit, non-interlaced");
        final int ch = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : -1;
        if (ch < 0) throw new IOException("PNG: colour type " + type);
        final int stride = w * ch;
        final byte[] raw = new byte[(stride + 1) * h];
        final Inflater inf = new Inflater();
        inf.setInput(idat.toByteArray());
        try {
            int got = 0;
            while (got < raw.length && !inf.finished()) {
                final int k = inf.inflate(raw, got, raw.length - got);
                if (k == 0 && (inf.needsInput() || inf.needsDictionary())) break;
                got += k;
            }
        } catch (DataFormatException e) {
            throw new IOException("PNG: " + e.getMessage());
        } finally {
            inf.end();
        }
        final byte[] px = new byte[stride * h];
        final byte[] prev = new byte[stride];
        final byte[] cur = new byte[stride];
        for (int y = 0; y < h; y++) {
            final int f = raw[y * (stride + 1)] & 0xFF;
            System.arraycopy(raw, y * (stride + 1) + 1, cur, 0, stride);
            for (int i = 0; i < stride; i++) {
                final int a = i >= ch ? cur[i - ch] & 0xFF : 0, b = prev[i] & 0xFF, c = i >= ch ? prev[i - ch] & 0xFF : 0;
                int v = cur[i] & 0xFF;
                switch (f) {
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) >> 1; break;
                    case 4: {
                        final int pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
                        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                        break;
                    }
                    default: break;
                }
                cur[i] = (byte) v;
            }
            System.arraycopy(cur, 0, px, y * stride, stride);
            System.arraycopy(cur, 0, prev, 0, stride);
        }
        final byte[] rgba = new byte[w * h * 4];
        for (int i = 0; i < w * h; i++) {
            int r, g, b, a = 255;
            switch (type) {
                case 0: r = g = b = px[i] & 0xFF; break;
                case 4: r = g = b = px[i * 2] & 0xFF; a = px[i * 2 + 1] & 0xFF; break;
                case 2: r = px[i * 3] & 0xFF; g = px[i * 3 + 1] & 0xFF; b = px[i * 3 + 2] & 0xFF; break;
                case 3: {
                    final int k = px[i] & 0xFF;
                    r = plte[k * 3] & 0xFF;
                    g = plte[k * 3 + 1] & 0xFF;
                    b = plte[k * 3 + 2] & 0xFF;
                    if (trns != null && k < trns.length) a = trns[k] & 0xFF;
                    break;
                }
                default: r = px[i * 4] & 0xFF; g = px[i * 4 + 1] & 0xFF; b = px[i * 4 + 2] & 0xFF; a = px[i * 4 + 3] & 0xFF;
            }
            rgba[i * 4] = (byte) r;
            rgba[i * 4 + 1] = (byte) g;
            rgba[i * 4 + 2] = (byte) b;
            rgba[i * 4 + 3] = (byte) a;
        }
        return new Image(w, h, rgba);
    }
}
