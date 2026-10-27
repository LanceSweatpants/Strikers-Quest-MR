package io.github.newcoke.strikers.guests;

import android.content.res.AssetManager;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * strikers-quest 1.14 (Lance: "make the converter"): the launcher's half of the guest converter (Android only).
 * Finds the player's own Melee / Pokemon Colosseum / Mario Strikers Charged files on the headset, runs the
 * converter (Convert) on a thread of its own and remembers what it made them from (guests/converted.txt), so it
 * runs again only when a file or the converter changes.
 */
public final class GuestSetup {
    private GuestSetup() {}

    /** The games guests come from, their names, and what each one gives. */
    public static final String[] IDS = {"GALE01", "GC6E01", "R4QE01"};
    public static final String[] NAMES = {"Super Smash Bros. Melee", "Pokemon Colosseum", "Mario Strikers Charged"};
    public static final String[] GIVES = {
        "17 Melee fighters, Pokemon voices, the evolution music",
        "70 Pokemon (+ shiny), the franchise's evolutions",
        "Bowser, Bowser Jr., Diddy, Petey, Boo, Dry Bones, Monty Mole, Shy Guy",
    };

    private static final String[] EXT = {".iso", ".gcm", ".rvz", ".wia", ".ciso", ".gcz", ".nkit.iso"};
    private static final int MAX_DEPTH = 5;
    private static final int GC_MAGIC = 0xC2339F3D, WII_MAGIC = 0x5D1C9EA3;

    /** What the search found: per game ID its file; and files it can't use, with why. */
    public static final class Found {
        public final Map<String, File> files = new LinkedHashMap<>();
        public final List<String> problems = new ArrayList<>();
    }

    /** Every disc image on shared storage that is one of the guests' games (slow on a full headset: off the UI thread). */
    public static Found scan(File root) {
        Found f = new Found();
        walk(root, 0, f);
        return f;
    }

    private static void walk(File dir, int depth, Found out) {
        File[] entries = dir.listFiles();
        if (entries == null) return;
        Arrays.sort(entries, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
        for (File e : entries) {
            String name = e.getName();
            if (name.startsWith(".")) continue;
            if (e.isDirectory()) {
                if (depth == 0 && name.equalsIgnoreCase("Android")) continue;
                if (depth < MAX_DEPTH) walk(e, depth + 1, out);
                continue;
            }
            String lower = name.toLowerCase(Locale.ROOT);
            boolean image = false;
            for (String x : EXT) image |= lower.endsWith(x);
            if (lower.endsWith(".wbfs")) {  // (only ever a Wii game, and encrypted)
                out.problems.add(name + ": a Wii game in .wbfs. Convert it to .rvz in Dolphin first.");
                continue;
            }
            // (Lance's test: his Melee and Colosseum were the .7z downloads, and the page just said "not found")
            if ((lower.endsWith(".7z") || lower.endsWith(".zip") || lower.endsWith(".rar")) && e.length() >= (100L << 20)
                    && (lower.contains("melee") || lower.contains("colosseum") || lower.contains("charged"))) {
                out.problems.add(name + ": zipped. Unzip it on your PC first, then copy the .iso or .rvz inside to"
                        + " the Quest.");
                continue;
            }
            if (!image) continue;
            String id = idOf(e, out);
            if (id == null) continue;
            for (String want : IDS) if (id.equals(want) && !out.files.containsKey(id)) out.files.put(id, e);
        }
    }

    /** The game ID from the header (plain image, or .rvz / .wia's copy of the disc header); null when not a guest game. */
    private static String idOf(File f, Found out) {
        byte[] h = new byte[0x100];
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            if (r.read(h) < 0x80) return null;
        } catch (IOException | SecurityException e) {
            return null;
        }
        String magic = new String(h, 0, 4, StandardCharsets.ISO_8859_1);
        if (magic.equals("RVZ\u0001") || magic.equals("WIA\u0001")) return ascii(h, 0x58, 6);
        if (Bin.u32(h, 0x1C) == GC_MAGIC) return ascii(h, 0, 6);
        if (Bin.u32(h, 0x18) == WII_MAGIC) {
            String id = ascii(h, 0, 6);
            if (id.equals("R4QE01"))
                out.problems.add(f.getName() + ": Mario Strikers Charged as a plain Wii image is encrypted. Convert it"
                        + " to .rvz in Dolphin (right-click the game, Convert File Format, RVZ).");
            return null;
        }
        return null;
    }

    private static String ascii(byte[] b, int at, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            int c = b[at + i] & 0xFF;
            sb.append(c >= 0x20 && c < 0x7F ? (char) c : '?');
        }
        return sb.toString();
    }

    // ------------------------------------------------------------------ the run (one at a time)
    public static volatile boolean running;
    public static volatile String line = "";
    public static volatile int done, total;
    public static volatile String result;  // the last run's outcome for the panel, null before one
    public static final List<String> warnings = Collections.synchronizedList(new ArrayList<>());

    /** True when the files found (or the converter) differ from what the guests on the headset were made from. */
    public static boolean needed(File home, Map<String, File> files) {
        return !stamp(files).equals(readStamp(new File(home, "guests/converted.txt")));
    }

    /** Whether any guests were made on this headset at all. */
    public static boolean made(File home) {
        return new File(home, "guests/converted.txt").isFile();
    }

    /**
     * Makes the guests from `files` (game ID -> image) into home/guests, on a thread; `strikersImage` is the game's own
     * disc (kits and poses come from it). onChange runs (on that thread) as it goes.
     */
    public static synchronized boolean start(final AssetManager assets, final File home, final String strikersImage,
            final Map<String, File> files, final Runnable onChange) {
        if (running) return false;
        running = true;
        result = null;
        warnings.clear();
        line = "Starting...";
        done = 0;
        total = 1;
        new Thread(() -> {
            final List<DiscSource> open = new ArrayList<>();
            try {
                Source strikers = strikersImage != null ? openOrWarn(strikersImage, "Super Mario Strikers", open) : null;
                Source melee = files.containsKey("GALE01") ? openOrWarn(files.get("GALE01").getPath(), NAMES[0], open) : null;
                Source colo = files.containsKey("GC6E01") ? openOrWarn(files.get("GC6E01").getPath(), NAMES[1], open) : null;
                Source charged = files.containsKey("R4QE01") ? openOrWarn(files.get("R4QE01").getPath(), NAMES[2], open) : null;
                Job job = new Job(strikers, melee, colo, charged, new AssetData(assets), new File(home, "guests"),
                        new Job.Progress() {
                            @Override
                            public void step(String what, int d, int t) {
                                line = what;
                                done = d;
                                total = Math.max(1, t);
                                if (onChange != null) onChange.run();
                            }

                            @Override
                            public void warn(String what) {
                                warnings.add(what);
                                if (onChange != null) onChange.run();
                            }
                        });
                int made = Convert.run(job);
                writeStamp(new File(home, "guests/converted.txt"), stamp(files));
                result = made + " guest characters ready." + (warnings.isEmpty() ? "" : " " + warnings.size() + " problems (below).");
            } catch (Throwable e) {  // (OutOfMemoryError too: the panel says so instead of the app closing)
                result = "Making the guests stopped: " + e;
                warnings.add(String.valueOf(e));
            } finally {
                for (DiscSource d : open) d.close();
                running = false;
                if (onChange != null) onChange.run();
            }
        }, "strikers-guests").start();
        return true;
    }

    private static Source openOrWarn(String path, String name, List<DiscSource> open) {
        try {
            DiscSource d = DiscSource.open(path);
            open.add(d);
            return d;
        } catch (IOException | UnsatisfiedLinkError e) {
            warnings.add(name + ": " + e.getMessage());
            return null;
        }
    }

    private static String stamp(Map<String, File> files) {
        StringBuilder sb = new StringBuilder("converter " + Convert.VERSION + "\n");
        for (String id : IDS) {
            File f = files.get(id);
            if (f != null) sb.append(id).append(' ').append(f.length()).append(' ').append(f.getName()).append('\n');
        }
        return sb.toString();
    }

    private static String readStamp(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) b.write(buf, 0, n);
            return b.toString("UTF-8");
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeStamp(File f, String s) throws IOException {
        File dir = f.getParentFile();
        if (dir != null) //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
        try (FileOutputStream o = new FileOutputStream(f)) {
            o.write(s.getBytes(StandardCharsets.UTF_8));
        }
    }

    /** Our shipped data (assets/guestdata) out of the APK. */
    static final class AssetData implements Data {
        private final AssetManager assets;

        AssetData(AssetManager assets) {
            this.assets = assets;
        }

        @Override
        public byte[] read(String name) throws IOException {
            try (InputStream in = assets.open("guestdata/" + name)) {
                ByteArrayOutputStream b = new ByteArrayOutputStream();
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) b.write(buf, 0, n);
                return b.toByteArray();
            }
        }

        @Override
        public boolean has(String name) {
            try (InputStream in = assets.open("guestdata/" + name)) {
                return true;
            } catch (IOException e) {
                return false;
            }
        }
    }
}
