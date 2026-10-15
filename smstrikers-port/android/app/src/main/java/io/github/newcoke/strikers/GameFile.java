package io.github.newcoke.strikers;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Environment;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * strikers-quest r41: the game file the player picked in the launcher panel, the search for disc
 * images anywhere on the headset, and a quick look at each one before the game opens it.
 *
 * <p>Before r41 the game only looked in a few folders (StrikersHome.PUBLIC_FOLDERS) and, with
 * nothing there or an image it could not read, closed with its message box hidden under the
 * immersive view (alpha 1: "it just closes"). The look here mirrors port_disc_open's header checks
 * (src/platform/disc.c), so the panel can say what is wrong before the game starts.
 */
final class GameFile {
    // The launcher panel's preferences (LobbyActivity.PREFS). The game reads the pick in its own
    // process (StrikersHome.prepare), so it is written with commit(), not apply().
    private static final String PREFS = "lobby";
    private static final String KEY = "gameFile";

    // The formats the panel offers (Lance, r41): plain images and Dolphin's two compressed ones.
    private static final String[] IMAGE_EXT = {".iso", ".rvz", ".wia"};
    private static final String[] ARCHIVE_EXT = {".zip", ".7z", ".rar"};
    // Smaller archives are not a GameCube disc (the smallest Strikers image is ~600 MB, a zip of it
    // is still hundreds); they would only crowd the list.
    private static final long ARCHIVE_MIN = 100L << 20;
    private static final int MAX_DEPTH = 5;

    private static final int GC_MAGIC = 0xC2339F3D;
    private static final int WII_MAGIC = 0x5D1C9EA3;

    private GameFile() {}

    /** One file found on the headset, with what the quick look made of it. */
    static final class Found {
        final File file;
        final boolean usable;
        final String label;    // "Super Mario Strikers (USA)", or what it is instead
        final String problem;  // null when usable; one plain sentence otherwise

        Found(File file, boolean usable, String label, String problem) {
            this.file = file;
            this.usable = usable;
            this.label = label;
            this.problem = problem;
        }
    }

    /** The picked file, or null when none was picked or it is gone. */
    static String chosen(Context context) {
        String path = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, "");
        return !path.isEmpty() && new File(path).isFile() ? path : null;
    }

    static void choose(Context context, String path) {
        SharedPreferences.Editor e = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
        e.putString(KEY, path);
        e.commit();
    }

    /**
     * Every disc image (and large archive) on shared storage, usable ones first. Needs all files
     * access; a slow walk on a full headset, so off the UI thread. `context` gives the labels and
     * problems their words, in the panel's language.
     */
    static List<Found> scan(Context context) {
        List<Found> found = new ArrayList<>();
        walk(context, Environment.getExternalStorageDirectory(), 0, found);
        List<Found> sorted = new ArrayList<>();
        for (Found f : found) if (f.usable) sorted.add(f);
        for (Found f : found) if (!f.usable) sorted.add(f);
        return sorted;
    }

    private static void walk(Context context, File dir, int depth, List<Found> out) {
        File[] entries = dir.listFiles();
        if (entries == null) return;
        List<File> sorted = new ArrayList<>();
        Collections.addAll(sorted, entries);
        Collections.sort(sorted, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
        for (File entry : sorted) {
            String name = entry.getName();
            if (name.startsWith(".")) continue;
            if (entry.isDirectory()) {
                // Android/data and obb are other apps' and unreadable anyway.
                if (depth == 0 && name.equalsIgnoreCase("Android")) continue;
                if (depth < MAX_DEPTH) walk(context, entry, depth + 1, out);
                continue;
            }
            String lower = name.toLowerCase(Locale.ROOT);
            if (endsWithAny(lower, IMAGE_EXT)) {
                out.add(check(context, entry));
            } else if (endsWithAny(lower, ARCHIVE_EXT) && entry.length() >= ARCHIVE_MIN) {
                out.add(new Found(entry, false, context.getString(R.string.game_zipped),
                        context.getString(R.string.game_zipped_why)));
            }
        }
    }

    /** What a disc image is, from its header. Mirrors port_disc_open in src/platform/disc.c. */
    static Found check(Context context, File file) {
        byte[] h = new byte[0x220];
        String id;
        try (RandomAccessFile r = new RandomAccessFile(file, "r")) {
            int n = r.read(h);
            if (n < 0x80) {
                return new Found(file, false, context.getString(R.string.game_not_image),
                        context.getString(R.string.game_too_small));
            }
            boolean rvz = startsWith(h, "RVZ\u0001");
            if (rvz || startsWith(h, "WIA\u0001")) {
                // Header 1 is 0x48 bytes; header 2 follows: disc type, compression, level, chunk
                // size, then a copy of the disc's first 0x80 bytes. r41: every compression
                // Dolphin makes reads (.rvz: none, bzip2, LZMA, LZMA2, zstd; .wia: none, purge,
                // bzip2, LZMA, LZMA2).
                int discType = be32(h, 0x48);
                int compression = be32(h, 0x4C);
                if (discType != 1 || be32(h, 0x58 + 0x1C) != GC_MAGIC) {
                    return new Found(file, false, context.getString(R.string.game_not_gamecube),
                            context.getString(R.string.game_wii_or_other));
                }
                boolean known = compression >= 0 && compression <= 5 && compression != (rvz ? 1 : 5);
                if (!known) {
                    return new Found(file, false, context.getString(R.string.game_unreadable),
                            context.getString(R.string.game_unknown_compression));
                }
                id = ascii(h, 0x58, 6);
            } else if (be32(h, 0x1C) == GC_MAGIC) {
                id = ascii(h, 0, 6);
            } else if (be32(h, 0x18) == WII_MAGIC) {
                return new Found(file, false, context.getString(R.string.game_not_gamecube),
                        context.getString(R.string.game_wii));
            } else if (file.getName().toLowerCase(Locale.ROOT).matches(".*\\.(ciso|gcz)")) {
                // Still read when found in Download (StrikersHome), though the list no longer
                // offers them; compressed, so the game checks them itself when it opens.
                return new Found(file, true, context.getString(R.string.game_file), null);
            } else {
                return new Found(file, false, context.getString(R.string.game_not_image),
                        context.getString(R.string.game_wrong_format));
            }
        } catch (IOException | SecurityException e) {
            return new Found(file, false, context.getString(R.string.game_unreadable),
                    context.getString(R.string.game_not_allowed));
        }
        if (!id.startsWith("G4Q")) {
            return new Found(file, false, context.getString(R.string.game_different, id),
                    context.getString(R.string.game_code_g4q));
        }
        return new Found(file, true, context.getString(R.string.game_strikers, region(context, id.charAt(3))), null);
    }

    static String region(Context context, char c) {
        switch (c) {
            case 'E': return context.getString(R.string.region_usa);
            case 'P': return context.getString(R.string.region_europe);
            case 'J': return context.getString(R.string.region_japan);
            default: return context.getString(R.string.region_other, String.valueOf(c));
        }
    }

    /** "Download/strikers.iso" for a file on shared storage, the full path otherwise. */
    static String shortPath(File file) {
        String root = Environment.getExternalStorageDirectory().getAbsolutePath() + "/";
        String path = file.getAbsolutePath();
        return path.startsWith(root) ? path.substring(root.length()) : path;
    }

    static String size(long bytes) {
        if (bytes >= (1L << 30)) return String.format(Locale.US, "%.1f GB", bytes / (double) (1L << 30));
        return String.format(Locale.US, "%d MB", bytes >> 20);
    }

    private static boolean endsWithAny(String name, String[] exts) {
        for (String e : exts) if (name.endsWith(e)) return true;
        return false;
    }

    private static boolean startsWith(byte[] h, String magic) {
        byte[] m = magic.getBytes(StandardCharsets.ISO_8859_1);
        for (int i = 0; i < m.length; i++) if (h[i] != m[i]) return false;
        return true;
    }

    private static int be32(byte[] b, int at) {
        return ((b[at] & 0xFF) << 24) | ((b[at + 1] & 0xFF) << 16) | ((b[at + 2] & 0xFF) << 8) | (b[at + 3] & 0xFF);
    }

    private static String ascii(byte[] b, int at, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            int c = b[at + i] & 0xFF;
            sb.append(c >= 0x20 && c < 0x7F ? (char) c : '?');
        }
        return sb.toString();
    }
}
