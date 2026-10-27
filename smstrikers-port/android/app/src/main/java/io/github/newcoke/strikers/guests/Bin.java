package io.github.newcoke.strikers.guests;

/** Reading numbers out of game files: big-endian (GameCube / Wii) and little-endian. */
public final class Bin {
    private Bin() {}

    public static int u8(byte[] b, int o) {
        return b[o] & 0xFF;
    }

    public static int s8(byte[] b, int o) {
        return b[o];
    }

    public static int u16(byte[] b, int o) {
        return ((b[o] & 0xFF) << 8) | (b[o + 1] & 0xFF);
    }

    public static int s16(byte[] b, int o) {
        return (short) u16(b, o);
    }

    /** As a Java int (callers mask with & 0xFFFFFFFFL when the top bit matters). */
    public static int u32(byte[] b, int o) {
        return ((b[o] & 0xFF) << 24) | ((b[o + 1] & 0xFF) << 16) | ((b[o + 2] & 0xFF) << 8) | (b[o + 3] & 0xFF);
    }

    public static long u32L(byte[] b, int o) {
        return u32(b, o) & 0xFFFFFFFFL;
    }

    public static long u64(byte[] b, int o) {
        return ((long) u32(b, o) << 32) | u32L(b, o + 4);
    }

    public static float f32(byte[] b, int o) {
        return Float.intBitsToFloat(u32(b, o));
    }

    public static int le16(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8);
    }

    public static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    public static float lef32(byte[] b, int o) {
        return Float.intBitsToFloat(le32(b, o));
    }

    /** A NUL-terminated ASCII string at o (at most max bytes). */
    public static String cstr(byte[] b, int o, int max) {
        int e = o;
        while (e < b.length && e < o + max && b[e] != 0) e++;
        return new String(b, o, e - o, java.nio.charset.StandardCharsets.ISO_8859_1);
    }

    public static String cstr(byte[] b, int o) {
        return cstr(b, o, b.length - o);
    }

    public static byte[] slice(byte[] b, int o, int n) {
        byte[] r = new byte[n];
        System.arraycopy(b, o, r, 0, n);
        return r;
    }
}
