package io.github.newcoke.strikers.guests;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** A growing byte buffer for writing the game's files (GPK, GVO, GEV, EVM, GX records ...), either byte order. */
public final class Out {
    private byte[] b = new byte[1 << 16];
    private int n;

    private void room(int k) {
        if (n + k > b.length) b = Arrays.copyOf(b, Math.max(b.length * 2, n + k));
    }

    public int size() {
        return n;
    }

    public Out u8(int v) {
        room(1);
        b[n++] = (byte) v;
        return this;
    }

    public Out bytes(byte[] v) {
        return bytes(v, 0, v.length);
    }

    public Out bytes(byte[] v, int o, int len) {
        room(len);
        System.arraycopy(v, o, b, n, len);
        n += len;
        return this;
    }

    public Out zeros(int k) {
        room(k);
        n += k;  // (the buffer starts zeroed and never shrinks)
        return this;
    }

    /** Pads with zeros to a multiple of a. */
    public Out align(int a) {
        while (n % a != 0) u8(0);
        return this;
    }

    public Out le16(int v) {
        room(2);
        b[n++] = (byte) v;
        b[n++] = (byte) (v >> 8);
        return this;
    }

    public Out le32(int v) {
        room(4);
        b[n++] = (byte) v;
        b[n++] = (byte) (v >> 8);
        b[n++] = (byte) (v >> 16);
        b[n++] = (byte) (v >> 24);
        return this;
    }

    public Out lef32(float v) {
        return le32(Float.floatToRawIntBits(v));
    }

    public Out be16(int v) {
        room(2);
        b[n++] = (byte) (v >> 8);
        b[n++] = (byte) v;
        return this;
    }

    public Out be32(int v) {
        room(4);
        b[n++] = (byte) (v >> 24);
        b[n++] = (byte) (v >> 16);
        b[n++] = (byte) (v >> 8);
        b[n++] = (byte) v;
        return this;
    }

    public Out bef32(float v) {
        return be32(Float.floatToRawIntBits(v));
    }

    /** s padded (or cut) to exactly len bytes, NUL-filled. */
    public Out fixed(String s, int len) {
        byte[] t = s.getBytes(StandardCharsets.ISO_8859_1);
        room(len);
        System.arraycopy(t, 0, b, n, Math.min(len, t.length));
        n += len;
        return this;
    }

    /** Overwrites 4 bytes at o (a size or offset filled in later). */
    public void patchLe32(int o, int v) {
        b[o] = (byte) v;
        b[o + 1] = (byte) (v >> 8);
        b[o + 2] = (byte) (v >> 16);
        b[o + 3] = (byte) (v >> 24);
    }

    public void patchBe32(int o, int v) {
        b[o] = (byte) (v >> 24);
        b[o + 1] = (byte) (v >> 16);
        b[o + 2] = (byte) (v >> 8);
        b[o + 3] = (byte) v;
    }

    public byte[] toBytes() {
        return Arrays.copyOf(b, n);
    }

    /** Written to a .tmp beside it, then renamed, so a half-written file never sits under the real name. */
    public void save(File f) throws IOException {
        save(f, b, n);
    }

    public static void save(File f, byte[] data, int len) throws IOException {
        File dir = f.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) throw new IOException("can't make " + dir);
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) {
            o.write(data, 0, len);
        }
        if (f.exists() && !f.delete()) throw new IOException("can't replace " + f);
        if (!tmp.renameTo(f)) throw new IOException("can't write " + f);
    }

    public static void save(File f, byte[] data) throws IOException {
        save(f, data, data.length);
    }
}
