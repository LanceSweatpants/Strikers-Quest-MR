package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Source;

import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * MUA (Wii) keeps its packages in one ZIP on the disc, z/assetsfb.wad (600 MB, deflate): read a piece at a time
 * through the disc (its central directory, then each wanted entry's own bytes), never the whole file at once.
 */
public final class Wad {
    public static final String PATH = "z/assetsfb.wad";

    private static final class Entry {
        final long local;
        final int method, csize, usize;

        Entry(long local, int method, int csize, int usize) {
            this.local = local;
            this.method = method;
            this.csize = csize;
            this.usize = usize;
        }
    }

    private final Source src;
    private final String path;
    private final Map<String, Entry> entries = new LinkedHashMap<>();

    public Wad(Source src) throws IOException {
        this(src, PATH);
    }

    public Wad(Source src, String path) throws IOException {
        this.src = src;
        this.path = path;
        final long size = src.size(path);
        final int tail = (int) Math.min(size, 65536 + 22);
        final byte[] t = src.read(path, size - tail, tail);
        int eocd = -1;
        for (int i = tail - 22; i >= 0; i--) {
            if (le32(t, i) == 0x06054b50) {
                eocd = i;
                break;
            }
        }
        if (eocd < 0) throw new IOException(path + ": not a ZIP (no end record)");
        final int count = le16(t, eocd + 10);
        final long cdSize = le32(t, eocd + 12) & 0xFFFFFFFFL, cdOff = le32(t, eocd + 16) & 0xFFFFFFFFL;
        final byte[] cd = src.read(path, cdOff, (int) cdSize);
        int p = 0;
        for (int k = 0; k < count; k++) {
            if (le32(cd, p) != 0x02014b50) throw new IOException(path + ": bad ZIP directory");
            final int method = le16(cd, p + 10);
            final int csize = le32(cd, p + 20), usize = le32(cd, p + 24);
            final int nlen = le16(cd, p + 28), xlen = le16(cd, p + 30), clen = le16(cd, p + 32);
            final long local = le32(cd, p + 42) & 0xFFFFFFFFL;
            final String name = new String(cd, p + 46, nlen, StandardCharsets.UTF_8);
            entries.put(name, new Entry(local, method, csize, usize));
            p += 46 + nlen + xlen + clen;
        }
    }

    public boolean has(String name) {
        return entries.containsKey(name);
    }

    public Iterable<String> names() {
        return entries.keySet();
    }

    /** One entry, whole. */
    public byte[] read(String name) throws IOException {
        final Entry e = entries.get(name);
        if (e == null) throw new FileNotFoundException(path + ": " + name);
        final byte[] lh = src.read(path, e.local, 30);
        if (le32(lh, 0) != 0x04034b50) throw new IOException(path + ": bad entry " + name);
        final long data = e.local + 30 + le16(lh, 26) + le16(lh, 28);
        final byte[] raw = src.read(path, data, e.csize);
        if (e.method == 0) return raw;
        if (e.method != 8) throw new IOException(path + ": " + name + ": compression " + e.method);
        final Inflater inf = new Inflater(true);
        try {
            inf.setInput(raw);
            final byte[] out = new byte[e.usize];
            int n = 0;
            while (n < out.length) {
                final int k = inf.inflate(out, n, out.length - n);
                if (k == 0) {
                    if (inf.finished() || inf.needsDictionary()) break;
                    if (inf.needsInput()) {
                        // (raw deflate wants one spare byte at the end on some inflaters)
                        inf.setInput(new byte[1]);
                        continue;
                    }
                }
                n += k;
            }
            if (n != out.length) throw new IOException(path + ": " + name + ": short (" + n + " of " + out.length + ")");
            return out;
        } catch (DataFormatException ex) {
            throw new IOException(path + ": " + name + ": " + ex.getMessage());
        } finally {
            inf.end();
        }
    }

    static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    static int le16(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8);
    }

    // ------------------------------------------------------------------------------------------------ .fb
    /** A .fb package (fbpack.py): named entries back to back (name[128], type[64], u32 size, data). */
    public static final class Fb {
        public final byte[] d;
        /** name -> {type, offset, size}, in file order (a later entry of the same name replaces it, as the dict). */
        final Map<String, Object[]> ents = new LinkedHashMap<>();

        public Fb(byte[] d) throws IOException {
            this.d = d;
            int p = 0;
            final int n = d.length;
            while (p + 196 <= n) {
                final String name = cstr(d, p, 128), type = cstr(d, p + 128, 64);
                final int size = le32(d, p + 192);
                if (name.isEmpty() || p + 196L + size > n) throw new IOException("bad .fb entry at " + p);
                ents.put(name, new Object[] {type, p + 196, size});
                p += 196 + size;
            }
            if (p != n) throw new IOException(".fb: trailing bytes");
        }

        public Iterable<String> names() {
            return ents.keySet();
        }

        public boolean has(String name) {
            return ents.containsKey(name);
        }

        public String type(String name) {
            final Object[] e = ents.get(name);
            return e == null ? null : (String) e[0];
        }

        public byte[] data(String name) {
            final Object[] e = ents.get(name);
            if (e == null) return null;
            final int off = (Integer) e[1], size = (Integer) e[2];
            final byte[] b = new byte[size];
            System.arraycopy(d, off, b, 0, size);
            return b;
        }

        public int size(String name) {
            final Object[] e = ents.get(name);
            return e == null ? -1 : (Integer) e[2];
        }

        private static String cstr(byte[] b, int o, int max) {
            int e = o;
            while (e < o + max && b[e] != 0) e++;
            return new String(b, o, e - o, StandardCharsets.ISO_8859_1);
        }
    }

    /** packages/generated/characters/<fb>_<costume>.fb */
    public Fb character(String fb, String costume) throws IOException {
        return new Fb(read("packages/generated/characters/" + fb + "_" + costume + ".fb"));
    }

    static byte[] readAll(java.io.InputStream in) throws IOException {
        final ByteArrayOutputStream b = new ByteArrayOutputStream();
        final byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) b.write(buf, 0, n);
        return b.toByteArray();
    }
}
