package io.github.newcoke.strikers.guests;

import java.io.Closeable;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The headset's Source: a game's disc image read by the game's own reader (src/platform/disc.c, through
 * src/platform/guest_disc_jni.c in libguestdisc.so): .iso, .gcm, .ciso, .gcz, .rvz, .wia, NKit; a Wii .rvz / .wia as
 * its game partition. Paths are looked up without case (disc tables differ: "PlFxNr.dat", "audio/us/fox.ssm").
 */
public final class DiscSource implements Source, Closeable {
    static {
        System.loadLibrary("guestdisc");
    }

    private static native long nOpen(String path, String[] err);

    private static native String nId(long h);

    private static native String[] nPaths(long h);

    private static native byte[] nRead(long h, int index);

    private static native void nClose(long h);

    private long h;
    private final String id;
    private final Map<String, Integer> index = new HashMap<>();

    private DiscSource(long h) {
        this.h = h;
        this.id = nId(h);
        String[] paths = nPaths(h);
        for (int i = 0; i < paths.length; i++) index.put(paths[i].toLowerCase(Locale.ROOT), i);
    }

    /** The image at path, or an IOException with the reader's own explanation. */
    public static DiscSource open(String path) throws IOException {
        String[] err = new String[1];
        long h = nOpen(path, err);
        if (h == 0) throw new IOException(err[0] != null ? err[0] : "can't open " + path);
        return new DiscSource(h);
    }

    @Override
    public String id() {
        return id;
    }

    @Override
    public boolean has(String path) {
        return index.containsKey(path.toLowerCase(Locale.ROOT));
    }

    @Override
    public synchronized byte[] read(String path) throws IOException {
        Integer i = index.get(path.toLowerCase(Locale.ROOT));
        if (i == null) throw new FileNotFoundException(id + ": " + path);
        if (h == 0) throw new IOException("closed");
        byte[] b = nRead(h, i);
        if (b == null) throw new IOException(id + ": couldn't read " + path);
        return b;
    }

    @Override
    public synchronized void close() {
        if (h != 0) {
            nClose(h);
            h = 0;
        }
    }
}
