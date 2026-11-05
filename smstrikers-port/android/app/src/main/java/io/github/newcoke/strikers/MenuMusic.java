package io.github.newcoke.strikers;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaMetadataRetriever;
import android.media.MediaPlayer;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Random;

/**
 * strikers-quest r147 (Lance: "change the franchise menu/home screen/not in game music to some cc0 lofi playlist that
 * we can play on loop"; the lofi loop "has to be the new theme song for the franchise mode. that plays first every time
 * you go into franchise mode, and then its random from there on"): the menu music, everywhere that is not a match.
 *
 * <p>Android's MediaPlayer, on a thread of its own: the decoding is the media service's, so the game thread pays
 * nothing, and it is an output stream of its own beside the game's (SDL / AAudio); the system mixes the two. Two
 * players crossfade (1.5 s) between tracks and at every switch.
 *
 * <ul>
 *   <li>In the game ({@link #forGame}, the ":game" process): it asks the game ~10 times a second
 *       (src/platform/menu_music.cpp): the menus or a match (TransitionTask), quiet (a movie, the evolution's music),
 *       the setting, the franchise hub opening (the theme first), the game's music volume. While it plays, the game's
 *       own front-end music is silenced there (nativeActive), its clicks and voices stay.
 *   <li>On the launcher panel ({@link #forPanel}): plays while the panel is in sight, with a NOW PLAYING card.
 * </ul>
 *
 * <p>The tracks: the APK's assets/music/lofi (tracks.txt, CC0, see assets/music/CREDITS.txt) and his own songs in
 * STRIKERS_HOME/music (made here with a README). The setting: xr_room_options.txt "music N" (the pause row and the
 * launcher's button write it), else strikers.ini menu_music, else BOTH.
 */
final class MenuMusic {
    private static final String TAG = "strikers-music";
    static final String MY_FOLDER = "music";
    private static final String[] MY_EXTENSIONS = {".mp3", ".ogg", ".m4a", ".aac", ".wav", ".flac", ".opus", ".mp4"};
    private static final int XFADE_MS = 1500;       // every switch, and track to track
    private static final int AWAY_FADE_MS = 350;    // out of sight (the headset off, another app)
    private static final int RESUME_MIN_LEFT_MS = 20000;  // back from a match: the same track if this much is left
    private static final int FAST_TICK_MS = 33, POLL_MS = 100, PANEL_TICK_MS = 250;
    // the panel before the game ever said its volume: menu_music.cpp's default (0.45, the game's own menu music's
    // measured loudness) at the game's full Music Volume
    private static final float PANEL_VOLUME = 0.45f;

    /** The launcher panel's NOW PLAYING card, called on the UI thread. */
    interface Listener {
        void onNowPlaying(String title, String artist);
    }

    // --- the game's side (src/platform/menu_music.cpp), only in the game's process
    private static native int nativeState();
    private static native float nativeVolume();
    private static native void nativeSetMode(int mode);
    private static native void nativeActive(boolean active, int mySongs);
    private static native void nativeNowPlaying(String title, String artist);
    private static native void nativeLog(String line);

    private final Context app;
    private final boolean game;
    private final Listener listener;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final HandlerThread thread;
    private final Handler h;
    private final MenuPlaylist list = new MenuPlaylist(new Random());
    private final Runnable tickRun = this::tick;

    // --- the music thread's own
    private final class Deck {
        final MenuPlaylist.Track track;
        MediaPlayer mp;
        boolean prepared, paused;
        int duration = -1;
        float gain, target = 1f;   // the crossfade, 0..1 (heard as equal power)
        float lastSet = -1f;

        Deck(MenuPlaylist.Track t, float gain) {
            track = t;
            this.gain = gain;
        }
    }

    private Deck cur, out;
    private float env, envTarget;    // the menus' envelope: 1 in the menus, 0 in a match / quiet / out of sight
    private int envMs = XFADE_MS;
    private boolean foreground, released, nativeOk = true;
    private int mode = -1, scene = 3, franchiseSeen = Integer.MIN_VALUE;
    private float volume = PANEL_VOLUME;
    private long lastTickMs, lastPollMs, giveUpUntilMs;
    private int failures, mySongs = -1;
    private boolean reportedActive, activeKnown;
    private float savedVolume = -1f;
    private final List<MenuPlaylist.Track> lofi = new ArrayList<>();
    private MenuPlaylist.Track lofiTheme;
    private List<MenuPlaylist.Track> mine = new ArrayList<>();
    private MenuPlaylist.Track myTheme;
    private final Map<String, Long> bad = new HashMap<>();
    private final Map<String, String[]> tags = new HashMap<>();
    // STRIKERS_HOME, found on the caller's thread (StrikersHome.homeDir can move the folder the first time it is
    // allowed out of Android/data; only the UI thread asks it)
    private File home;

    private MenuMusic(Context context, boolean game, Listener listener) {
        this.app = context.getApplicationContext();
        this.game = game;
        this.listener = listener;
        final File homeNow = StrikersHome.homeDir(context);
        thread = new HandlerThread("strikers-music");
        thread.start();
        h = new Handler(thread.getLooper());
        h.post(() -> {
            home = homeNow;
            loadLofi();
            ensureFolder();
            scanMine();
            if (!game) {
                mode = readMode(home);
                volume = readVolume(app);
            }
            rebuild();
            lastTickMs = SystemClock.uptimeMillis();
            tick();
        });
    }

    /** The game's own (StrikersActivity, after the native library is loaded). */
    static MenuMusic forGame(Context context) {
        return new MenuMusic(context, true, null);
    }

    /** The launcher panel's (LobbyActivity); `listener` shows its NOW PLAYING card. */
    static MenuMusic forPanel(Context context, Listener listener) {
        return new MenuMusic(context, false, listener);
    }

    /** In sight or not (onResume / onPause): plays only in sight. Coming back reads the setting and his folder again. */
    void setForeground(boolean on) {
        final File homeNow = on ? StrikersHome.homeDir(app) : null;
        h.post(() -> {
            if (released) return;
            foreground = on;
            if (on) {
                if (!homeNow.equals(home)) {
                    home = homeNow;
                    ensureFolder();
                }
                final int m = readMode(home);
                if (game) {
                    // the launcher's button may have changed it while the game was out of sight
                    if (hasModeLine(home) && m != mode) callSetMode(m);
                } else {
                    mode = m;
                    volume = readVolume(app);
                }
                scanMine();
                rebuild();
            }
            envMs = on ? XFADE_MS : AWAY_FADE_MS;
            kick();
        });
    }

    /** The launcher's button changed the setting (already written): now. */
    void reloadSetting() {
        h.post(() -> {
            if (released) return;
            mode = readMode(home);
            scanMine();
            rebuild();
            kick();
        });
    }

    /** Ends it: a short fade, then the players and the thread go. */
    void release() {
        h.post(() -> {
            foreground = false;
            envMs = AWAY_FADE_MS;
            kick();
        });
        h.postDelayed(() -> {
            released = true;
            h.removeCallbacks(tickRun);
            drop(cur);
            drop(out);
            cur = out = null;
            thread.quitSafely();
        }, AWAY_FADE_MS + 150);
    }

    // ------------------------------------------------------------------------------------------- the loop

    private void kick() {
        h.removeCallbacks(tickRun);
        h.post(tickRun);
    }

    private void tick() {
        if (released) return;
        final long now = SystemClock.uptimeMillis();
        final int dt = (int) Math.max(0, Math.min(250, now - lastTickMs));
        lastTickMs = now;
        if (game && nativeOk && now - lastPollMs >= POLL_MS - 5) {
            lastPollMs = now;
            poll();
        }
        final boolean wanted = foreground && nativeOk && scene == 0 && mode != MenuPlaylist.OFF && !list.isEmpty()
                && now >= giveUpUntilMs;
        reportActive(nativeOk && mode != MenuPlaylist.OFF && !list.isEmpty() && now >= giveUpUntilMs);
        envTarget = wanted ? 1f : 0f;
        if (wanted) {
            if (cur == null) {
                startNext(null, true);
            } else if (cur.paused) {
                resumeOrNext();
            } else if (cur.track != null && !list.contains(cur.track) && out == null) {
                startNext(null, false);  // the setting changed: this one isn't in it any more
            }
        }
        // the envelope (equal power when heard)
        env = step(env, envTarget, dt, envMs);
        if (env <= 0f && envTarget <= 0f) {
            // silent: hold the track where it is, drop the one fading out
            if (cur != null && cur.prepared && !cur.paused) {
                try {
                    cur.mp.pause();
                } catch (IllegalStateException e) {
                    drop(cur);
                    cur = null;
                }
                if (cur != null) cur.paused = true;
            }
            if (out != null) {
                drop(out);
                out = null;
            }
            if (mode == MenuPlaylist.OFF && cur != null) {
                drop(cur);
                cur = null;
            }
        }
        // the crossfade, from the moment the new one is ready (so the old one doesn't fade into a gap)
        if (cur != null && cur.prepared) cur.gain = step(cur.gain, cur.target, dt, XFADE_MS);
        if (out != null && (cur == null || cur.prepared)) {
            out.gain = step(out.gain, 0f, dt, XFADE_MS);
            if (out.gain <= 0f) {
                drop(out);
                out = null;
            }
        }
        // the end of a track: the next one crossfades in over its last 1.5 s
        if (wanted && cur != null && cur.prepared && !cur.paused && out == null && cur.duration > 0) {
            int pos = -1;
            try {
                pos = cur.mp.getCurrentPosition();
            } catch (IllegalStateException e) {
                // gone: the error listener handles it
            }
            if (pos >= 0 && pos >= cur.duration - XFADE_MS - 150) startNext(null, false);
        }
        apply(cur);
        apply(out);
        final boolean moving = env != envTarget || (cur != null && cur.gain != cur.target) || out != null;
        h.removeCallbacks(tickRun);
        h.postDelayed(tickRun, moving ? FAST_TICK_MS : (game ? POLL_MS : PANEL_TICK_MS));
    }

    private static float step(float v, float target, int dtMs, int overMs) {
        final float d = overMs > 0 ? (float) dtMs / overMs : 1f;
        if (v < target) return Math.min(target, v + d);
        if (v > target) return Math.max(target, v - d);
        return v;
    }

    private static float heard(float x) {
        return (float) Math.sin(0.5 * Math.PI * Math.max(0f, Math.min(1f, x)));
    }

    private void apply(Deck d) {
        if (d == null || d.mp == null || !d.prepared) return;
        final float v = volume * heard(env) * heard(d.gain);
        if (Math.abs(v - d.lastSet) < 0.002f && !(v == 0f && d.lastSet != 0f)) return;
        d.lastSet = v;
        try {
            d.mp.setVolume(v, v);
        } catch (IllegalStateException e) {
            // released under us
        }
    }

    // ------------------------------------------------------------------------------------------- the game's side

    private void poll() {
        final int s;
        final float v;
        try {
            s = nativeState();
            v = nativeVolume();
        } catch (UnsatisfiedLinkError e) {
            nativeOk = false;
            Log.w(TAG, "no menu music in this build's native side", e);
            return;
        }
        final int newScene = s & 3, newMode = (s >> 2) & 3, fr = s >>> 4;
        if (newMode != mode) {
            mode = newMode;
            rebuild();
        }
        if (newScene != scene) {
            final boolean backToMenus = newScene == 0 && scene != 3;
            scene = newScene;
            envMs = XFADE_MS;
            if (backToMenus) {
                scanMine();  // songs he put in the folder meanwhile
                rebuild();
            }
        }
        if (Math.abs(v - volume) > 0.001f) volume = v;
        if (Math.abs(volume - savedVolume) > 0.01f) {
            savedVolume = volume;
            writeVolume(app, volume);  // for the launcher panel, which has no game to ask
        }
        if (franchiseSeen == Integer.MIN_VALUE) {
            franchiseSeen = fr;
        } else if (fr != franchiseSeen) {
            franchiseSeen = fr;
            franchiseEntered();
        }
    }

    private void callSetMode(int m) {
        try {
            nativeSetMode(m);
        } catch (UnsatisfiedLinkError e) {
            nativeOk = false;
        }
    }

    private void reportActive(boolean active) {
        if (!game || !nativeOk) return;
        if (activeKnown && active == reportedActive) return;
        activeKnown = true;
        reportedActive = active;
        try {
            nativeActive(active, mySongs);
        } catch (UnsatisfiedLinkError e) {
            nativeOk = false;
        }
    }

    private void say(String line) {
        Log.i(TAG, line);
        if (!game || !nativeOk) return;
        try {
            nativeLog(line);
        } catch (UnsatisfiedLinkError e) {
            nativeOk = false;
        }
    }

    /** The hub opened from the main menu: the theme, crossfaded in (unless it is already the one playing). */
    private void franchiseEntered() {
        final MenuPlaylist.Track t = list.theme();
        if (t == null || mode == MenuPlaylist.OFF) {
            say("franchise entered: no theme in this setting (" + MenuPlaylist.MODE_NAMES[Math.max(0, mode)] + ")");
            return;
        }
        if (cur != null && t.equals(cur.track) && !cur.paused) {
            say("franchise entered: the theme is already playing");
            list.franchiseTheme();
            return;
        }
        startNext(list.franchiseTheme(), cur == null);
    }

    // ------------------------------------------------------------------------------------------- the players

    /** Crossfades to `t` (the playlist's next when null); `fromSilence`: the envelope fades it in, no crossfade. */
    private void startNext(MenuPlaylist.Track t, boolean fromSilence) {
        if (t == null) t = pick();
        if (t == null) return;
        if (cur != null) {
            if (cur.paused || !cur.prepared || fromSilence) {
                drop(cur);
            } else {
                if (out != null) drop(out);
                out = cur;
            }
        }
        cur = new Deck(t, fromSilence || env <= 0f ? 1f : 0f);
        open(cur);
    }

    /** The next track not known to fail. */
    private MenuPlaylist.Track pick() {
        final long now = SystemClock.uptimeMillis();
        for (int i = 0; i <= list.size(); i++) {
            final MenuPlaylist.Track t = list.next();
            if (t == null) return null;
            final Long until = bad.get(t.source);
            if (until == null || until < now) return t;
        }
        return null;
    }

    private void resumeOrNext() {
        int left = -1;
        try {
            left = cur.duration - cur.mp.getCurrentPosition();
        } catch (IllegalStateException e) {
            // gone
        }
        if (left >= RESUME_MIN_LEFT_MS && list.contains(cur.track)) {
            try {
                cur.mp.start();
                cur.paused = false;
                return;
            } catch (IllegalStateException e) {
                // a fresh one below
            }
        }
        drop(cur);
        cur = null;
        startNext(null, true);
    }

    private void open(final Deck d) {
        final MediaPlayer mp = new MediaPlayer();
        d.mp = mp;
        try {
            mp.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build());
            setSource(mp, d.track);
            mp.setVolume(0f, 0f);
            mp.setOnPreparedListener(p -> {
                if (d.mp != p) return;
                d.prepared = true;
                d.duration = p.getDuration();
                d.lastSet = -1f;
                apply(d);
                p.start();
                failures = 0;
                announce(d.track);
                kick();
            });
            mp.setOnCompletionListener(p -> {
                if (d.mp != p) return;
                if (d == cur) {
                    drop(cur);
                    cur = null;  // (no crossfade: a very short track) the next tick starts the next
                    kick();
                } else if (d == out) {
                    drop(out);
                    out = null;
                }
            });
            mp.setOnErrorListener((p, what, extra) -> {
                failed(d, "error " + what + "/" + extra);
                return true;
            });
            mp.prepareAsync();
        } catch (IOException | RuntimeException e) {
            failed(d, e.toString());
        }
    }

    private void setSource(MediaPlayer mp, MenuPlaylist.Track t) throws IOException {
        if (!t.source.startsWith("asset:")) {
            mp.setDataSource(t.source);
            return;
        }
        final String path = t.source.substring("asset:".length());
        try (AssetFileDescriptor afd = app.getAssets().openFd(path)) {
            mp.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
        } catch (FileNotFoundException e) {
            // stored compressed in the APK (no file offset to play from): a copy in the cache, once
            final File copy = new File(new File(app.getCacheDir(), "music"), new File(path).getName());
            if (!copy.isFile()) {
                File parent = copy.getParentFile();
                if (parent != null) //noinspection ResultOfMethodCallIgnored
                    parent.mkdirs();
                final File part = new File(copy.getPath() + ".part");
                try (InputStream in = app.getAssets().open(path); OutputStream o = new FileOutputStream(part)) {
                    final byte[] b = new byte[64 * 1024];
                    int n;
                    while ((n = in.read(b)) > 0) o.write(b, 0, n);
                }
                if (!part.renameTo(copy)) throw new IOException("could not keep " + copy);
            }
            mp.setDataSource(copy.getAbsolutePath());
        }
    }

    private void failed(Deck d, String why) {
        say("could not play " + d.track.source + " (" + why + "), the next one");
        bad.put(d.track.source, SystemClock.uptimeMillis() + 10 * 60 * 1000L);
        if (d == out) {
            drop(out);
            out = null;
            return;
        }
        if (d != cur) {
            drop(d);
            return;
        }
        drop(cur);
        cur = null;
        if (++failures >= Math.max(3, list.size())) {
            failures = 0;
            giveUpUntilMs = SystemClock.uptimeMillis() + 60_000;
            say("nothing plays: the game's own menu music for a minute");
        }
        kick();
    }

    private void drop(Deck d) {
        if (d == null || d.mp == null) return;
        final MediaPlayer mp = d.mp;
        d.mp = null;
        d.prepared = false;
        try {
            mp.reset();
        } catch (RuntimeException e) {
            // already gone
        }
        mp.release();
    }

    private void announce(MenuPlaylist.Track t) {
        String title = t.title, artist = t.artist;
        if (t.mine) {
            String[] tag = tags.get(t.source);
            if (tag == null) {
                tag = readTags(t.source);
                tags.put(t.source, tag);
            }
            if (tag[0] != null && !tag[0].trim().isEmpty()) title = tag[0].trim();
            if (tag[1] != null && !tag[1].trim().isEmpty()) artist = tag[1].trim();
        }
        final String ft = title, fa = artist;
        if (game) {
            if (!nativeOk) return;
            try {
                nativeNowPlaying(ft, fa);
            } catch (UnsatisfiedLinkError e) {
                nativeOk = false;
            }
        } else {
            Log.i(TAG, "now playing: " + ft + " - " + fa);
            if (listener != null) ui.post(() -> listener.onNowPlaying(ft, fa));
        }
    }

    private static String[] readTags(String path) {
        final MediaMetadataRetriever r = new MediaMetadataRetriever();
        try {
            r.setDataSource(path);
            return new String[] {r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE),
                    r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST)};
        } catch (RuntimeException e) {
            return new String[] {null, null};
        } finally {
            try {
                r.release();
            } catch (IOException | RuntimeException e) {
                // nothing to do
            }
        }
    }

    // ------------------------------------------------------------------------------------------- the tracks

    /** assets/music/lofi/tracks.txt: file \t title \t artist \t album \t "theme" for the franchise theme. */
    private void loadLofi() {
        lofi.clear();
        lofiTheme = null;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                app.getAssets().open("music/lofi/tracks.txt"), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                final String[] f = line.split("\t");
                if (f.length < 3) continue;
                final MenuPlaylist.Track t = new MenuPlaylist.Track("asset:music/lofi/" + f[0].trim(), f[1].trim(),
                        f[2].trim(), false);
                if (f.length >= 5 && "theme".equalsIgnoreCase(f[4].trim())) lofiTheme = t;
                else lofi.add(t);
            }
        } catch (IOException e) {
            Log.w(TAG, "no lofi playlist in the APK", e);
        }
    }

    /** STRIKERS_HOME/music: his own songs. */
    static File myFolder(File home) {
        return new File(home, MY_FOLDER);
    }

    /** STRIKERS_HOME/music, with its README, the first time. */
    private void ensureFolder() {
        try {
            final File dir = myFolder(home);
            if (!dir.isDirectory() && !dir.mkdirs()) return;
            final File readme = new File(dir, "README.txt");
            if (readme.exists()) return;
            final String text = "MY MUSIC - Quest 3 Strikers\r\n"
                    + "\r\n"
                    + "Drop your own .mp3 / .ogg / .m4a here (also .aac .wav .flac .opus).\r\n"
                    + "They play in the menus, the franchise hub and on the launcher panel, never in a match.\r\n"
                    + "\r\n"
                    + "MENU MUSIC setting (the pause menu's settings, last row, or the launcher's Menu music button):\r\n"
                    + "  BOTH     - the lofi playlist + your songs, shuffled (the default)\r\n"
                    + "  MY MUSIC - only your songs (the lofi plays while this folder is empty)\r\n"
                    + "  LOFI     - only the lofi playlist\r\n"
                    + "  OFF      - the game's own menu music\r\n"
                    + "\r\n"
                    + "Name a song theme.mp3 (or theme.ogg / theme.m4a ...) and it is your franchise theme: it plays\r\n"
                    + "first every time you go into franchise mode (else the lofi theme does).\r\n"
                    + "Sub-folders are read too. Titles and artists come from the files' own tags.\r\n";
            try (OutputStream o = new FileOutputStream(readme)) {
                o.write(text.getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "no music folder", e);
        }
    }

    private void scanMine() {
        final List<File> found = new ArrayList<>();
        collect(myFolder(home), 0, found);
        found.sort((a, b) -> a.getPath().compareToIgnoreCase(b.getPath()));
        final List<MenuPlaylist.Track> songs = new ArrayList<>();
        MenuPlaylist.Track theme = null;
        for (File f : found) {
            final String name = f.getName();
            final int dot = name.lastIndexOf('.');
            final String base = dot > 0 ? name.substring(0, dot) : name;
            final MenuPlaylist.Track t = new MenuPlaylist.Track(f.getAbsolutePath(),
                    base.replace('_', ' ').trim(), "My music", true);
            songs.add(t);
            if (theme == null && base.equalsIgnoreCase("theme")) theme = t;
        }
        if (songs.size() != mySongs) say("my music: " + songs.size() + " song" + (songs.size() == 1 ? "" : "s")
                + " in " + myFolder(home) + (theme != null ? " (theme: " + theme.source + ")" : ""));
        mine = songs;
        myTheme = theme;
        if (mySongs != songs.size()) {
            mySongs = songs.size();
            activeKnown = false;  // the game's pause row shows the count
        }
    }

    private static void collect(File dir, int depth, List<File> out) {
        final File[] files = dir.listFiles();
        if (files == null) return;
        for (File f : files) {
            if (out.size() >= 1000) return;
            if (f.isDirectory()) {
                if (depth < 3 && !f.getName().startsWith(".")) collect(f, depth + 1, out);
                continue;
            }
            final String n = f.getName().toLowerCase(Locale.ROOT);
            if (n.startsWith(".")) continue;
            for (String ext : MY_EXTENSIONS) {
                if (n.endsWith(ext) && f.length() > 1024) {
                    out.add(f);
                    break;
                }
            }
        }
    }

    private void rebuild() {
        final int m = mode < 0 ? MenuPlaylist.BOTH : mode;
        list.set(m, lofi, lofiTheme, mine, myTheme);
        if (m == MenuPlaylist.MINE && list.fellBack()) say("MY MUSIC picked, the folder is empty: the lofi plays");
    }

    // ------------------------------------------------------------------------------------------- the setting

    private static File optionsFile(File home) {
        return new File(home, "xr_room_options.txt");
    }

    /** xr_room_options.txt "music N" (the pause row / the launcher's button), else strikers.ini menu_music, else BOTH. */
    static int readMode(File home) {
        final int m = modeLine(home);
        if (m >= 0) return m;
        final File ini = new File(home, "strikers.ini");
        for (String line : readLines(ini)) {
            final String l = line.trim();
            if (l.startsWith("#") || l.startsWith(";")) continue;
            final int eq = l.indexOf('=');
            if (eq <= 0) continue;
            final String key = l.substring(0, eq).trim().toLowerCase(Locale.ROOT);
            if (!key.equals("menu_music") && !key.equals("strikers_menu_music")) continue;
            String v = l.substring(eq + 1).trim().toLowerCase(Locale.ROOT).replace("\"", "").replace("'", "");
            if (v.equals("lofi") || v.equals("0")) return MenuPlaylist.LOFI;
            if (v.equals("mine") || v.equals("my music") || v.equals("my_music") || v.equals("mymusic") || v.equals("1"))
                return MenuPlaylist.MINE;
            if (v.equals("off") || v.equals("game") || v.equals("3")) return MenuPlaylist.OFF;
            return MenuPlaylist.BOTH;
        }
        return MenuPlaylist.BOTH;
    }

    static boolean hasModeLine(File home) {
        return modeLine(home) >= 0;
    }

    private static int modeLine(File home) {
        for (String line : readLines(optionsFile(home))) {
            final String[] f = line.trim().split("\\s+");
            if (f.length == 2 && f[0].equals("music")) {
                try {
                    final int v = Integer.parseInt(f[1]);
                    return v >= 0 && v < MenuPlaylist.MODES ? v : MenuPlaylist.BOTH;
                } catch (NumberFormatException e) {
                    return -1;
                }
            }
        }
        return -1;
    }

    /** The launcher's button: "music N" in xr_room_options.txt, the file's other lines kept. */
    static void writeMode(File home, int mode) {
        final File f = optionsFile(home);
        final List<String> lines = readLines(f);
        final StringBuilder sb = new StringBuilder();
        boolean done = false;
        for (String line : lines) {
            if (line.trim().isEmpty()) continue;
            if (line.trim().startsWith("music ")) {
                if (!done) sb.append("music ").append(mode).append('\n');
                done = true;
            } else {
                sb.append(line.trim()).append('\n');
            }
        }
        if (!done) sb.append("music ").append(mode).append('\n');
        try (OutputStream o = new FileOutputStream(f)) {
            o.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "could not write " + f, e);
        }
    }

    static int mySongCount(File home) {
        final List<File> found = new ArrayList<>();
        collect(myFolder(home), 0, found);
        return found.size();
    }

    private static File volumeFile(Context context) {
        return new File(context.getFilesDir(), "menu-music-volume.txt");
    }

    /** The game's music volume x menu_music_volume, as the game last said it (the panel has no game to ask). */
    private static float readVolume(Context context) {
        final List<String> l = readLines(volumeFile(context));
        if (l.isEmpty()) return PANEL_VOLUME;
        try {
            return Math.max(0f, Math.min(1f, Float.parseFloat(l.get(0).trim())));
        } catch (NumberFormatException e) {
            return PANEL_VOLUME;
        }
    }

    private static void writeVolume(Context context, float v) {
        try (OutputStream o = new FileOutputStream(volumeFile(context))) {
            o.write(String.format(Locale.ROOT, "%.3f\n", v).getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            // the panel keeps its default
        }
    }

    private static List<String> readLines(File f) {
        final List<String> out = new ArrayList<>();
        if (!f.isFile()) return out;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null && out.size() < 400) out.add(line);
        } catch (IOException e) {
            // as if there were none
        }
        return out;
    }
}
