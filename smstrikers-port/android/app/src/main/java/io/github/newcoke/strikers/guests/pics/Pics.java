package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Png;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;

/**
 * strikers-quest 1.14: the guests' 2D pictures, made on the headset from the player's own discs, byte for byte
 * as the PC tools made them (tools/outside: build_portraits.py's Melee portraits, loadres.py, iconres.py,
 * kitres.py):
 * <ul>
 * <li>{@link #portraits}: guests/portraits/<pack>.png for the roster's Melee fighters (MnSlChr.usd's select
 * portraits, pixels unaltered; Melee Pokemon roster lines: TyPoke trophy art) and strikers_<name>.png (the
 * Strikers characters' own loading / cup pictures, for the select menu).</li>
 * <li>{@link #run} (after every part's portraits exist): guests/icons/<pack>[_r].gtx (score-bar banners),
 * guests/loading/<pack>_<l|r>[_bg|_white].gtx (loading pictures), guests/kits/<family>_<pack>.gtx (the
 * sidekicks' and keeper's kits in the captain's colours).</li>
 * </ul>
 * Our own tables: guestdata/pics/pics.txt; the roster and colours: guestdata roster.txt / colors.txt (or the
 * ones already in guests/).
 */
public final class Pics {
    private Pics() {}

    static final String LOADING_RES = "art/fe/LoadingScreensUI.Res";
    static final String ICONS_RES = "art/fe/CaptainIconsUI.Res";

    /** A roster line: pack, role, plays as, name, game (tools/outside/roster.py). */
    static final class Guest {
        final String pack, role, game;

        Guest(String pack, String role, String game) {
            this.pack = pack;
            this.role = role;
            this.game = game;
        }
    }

    // ------------------------------------------------------------------ entry points

    /**
     * hostref.txt, Melee portraits, strikers_*.png (early: other parts' pictures and run() read guests/portraits,
     * the posed portrait renders hostref.txt).
     */
    public static void portraits(Job job) {
        hostref(job);
        final Tables t;
        try {
            t = Tables.load(job.data);
        } catch (IOException e) {
            job.progress.warn("Pictures: " + e.getMessage());
            return;
        }
        final List<Guest> roster = roster(job);
        // Melee
        final List<Guest> melee = new ArrayList<>();
        for (Guest g : roster)
            if ((g.game.equals("Melee") || g.game.equals("Melee Pokemon")) && t.melee.containsKey(g.pack)) melee.add(g);
        final int total = melee.size() + t.strikers.size();
        int done = 0;
        if (job.melee == null) {
            if (!melee.isEmpty()) job.progress.warn("Pictures: no Melee files, no Melee portraits");
        } else {
            final Map<String, Hsd> files = new HashMap<>();
            for (Guest g : melee) {
                job.progress.step("Portrait: " + g.pack, done++, total);
                try {
                    Tables.MeleePic p = t.melee.get(g.pack);
                    Hsd h = files.get(p.file);
                    if (h == null) {
                        h = new Hsd(job.melee.read(p.file));
                        files.put(p.file, h);
                    }
                    final int[] wh = new int[2];
                    final byte[] rgba = h.decode(p.texAnim >= 0 ? h.frame(p.texAnim, p.frame) : p.frame, wh);
                    Png.write(job.out("portraits/" + g.pack + ".png"), rgba, wh[0], wh[1]);
                } catch (IOException | RuntimeException e) {
                    job.progress.warn("Portrait " + g.pack + ": " + e);
                }
            }
        }
        // the Strikers characters' own
        if (job.strikers == null) {
            job.progress.warn("Pictures: no Strikers disc");
            return;
        }
        final Map<String, List<Res.Rec>> bundles = new HashMap<>();
        for (Map.Entry<String, Tables.StrikersPic> e : t.strikers.entrySet()) {
            job.progress.step("Portrait: strikers_" + e.getKey(), done++, total);
            try {
                final Tables.StrikersPic p = e.getValue();
                List<Res.Rec> recs = bundles.get(p.bundle);
                if (recs == null) {
                    recs = Res.records(job.strikers.read(p.bundle));
                    bundles.put(p.bundle, recs);
                }
                final Res.Rec r = Res.find(recs, p.hash);
                if (r == null) throw new IOException("no picture " + Integer.toHexString(p.hash) + " in " + p.bundle);
                final int[] wh = new int[2];
                final byte[] rgba = Res.decodeCi8(r.b, wh);
                Png.write(job.out("portraits/strikers_" + e.getKey() + ".png"), rgba, wh[0], wh[1]);
            } catch (IOException | RuntimeException ex) {
                job.progress.warn("Portrait strikers_" + e.getKey() + ": " + ex);
            }
        }
    }

    /**
     * guests/hostref.txt (the Strikers characters' standing poses for the retarget, tools/outside/hostref.py),
     * from the Strikers disc; portraits() makes it first thing (the posed portrait renders read it too).
     */
    public static void hostref(Job job) {
        if (job.strikers == null) return;
        try {
            Out.save(job.out("hostref.txt"), HostRef.make(job.strikers));
        } catch (IOException | RuntimeException e) {
            job.progress.warn("hostref.txt: " + e);
        }
    }

    /** Icons, loading pictures, kits (after every part's portraits are in guests/portraits). */
    public static void run(Job job) {
        if (job.strikers == null) {
            job.progress.warn("Pictures: no Strikers disc, no icons / loading pictures / kits");
            return;
        }
        final Tables t;
        try {
            t = Tables.load(job.data);
        } catch (IOException e) {
            job.progress.warn("Pictures: " + e.getMessage());
            return;
        }
        final List<Guest> roster = roster(job);
        final List<Guest> captains = new ArrayList<>();
        for (Guest g : roster) if (g.role.equals("captain")) captains.add(g);
        final Map<String, float[][]> colours = colours(job);
        final List<String> kitPacks = new ArrayList<>();
        for (Guest g : captains) {
            if (colours.containsKey(g.pack)) kitPacks.add(g.pack);
            else job.progress.warn("Kits " + g.pack + ": no colours");
        }
        final int total = captains.size() + roster.size() + Kits.FAMILIES.length * (1 + kitPacks.size());
        final int[] done = {0};
        final ExecutorService pool = Kits.pool();
        try {
            icons(job, t, captains, done, total);
            loading(job, roster, pool, done, total);
            try {
                Map<String, float[][]> use = new LinkedHashMap<>();
                for (String p : kitPacks) use.put(p, colours.get(p));
                Kits.make(job.strikers, use, job.out("kits"), pool, (what) -> job.progress.step(what, done[0]++, total),
                        job.progress::warn);
            } catch (IOException | RuntimeException e) {
                job.progress.warn("Kits: " + e);
            }
        } finally {
            pool.shutdownNow();
        }
    }

    // ------------------------------------------------------------------ the parts

    static void icons(Job job, Tables t, List<Guest> captains, int[] done, int total) {
        final Icons icons;
        final Hsd ifall;
        try {
            icons = new Icons(Res.records(job.strikers.read(ICONS_RES)));
            ifall = job.melee != null ? new Hsd(job.melee.read("IfAll.usd")) : null;
        } catch (IOException | RuntimeException e) {
            job.progress.warn("Icons: " + e);
            return;
        }
        final Map<String, byte[][]> bySeries = new HashMap<>();
        for (Guest g : captains) {
            job.progress.step("Icon: " + g.pack, done[0]++, total);
            final String series = g.game.equals("Colosseum") || g.game.equals("Melee Pokemon") ? "pokemon" : t.series.get(g.pack);
            if (series == null) {
                job.progress.warn("Icon " + g.pack + ": no series");
                continue;
            }
            try {
                byte[][] recs = bySeries.get(series);
                if (recs == null) {
                    if (ifall == null) throw new IOException("no Melee files (the series emblems are Melee's)");
                    final Integer desc = t.emblem.get(series);
                    final int[] col = t.colour.get(series);
                    if (desc == null || col == null) throw new IOException("no emblem for " + series);
                    final int[] wh = new int[2];
                    final byte[] emblem = ifall.decode(desc, wh);
                    final int[] img = icons.compose(emblem, wh[0], wh[1], new int[] {col[0], col[1], col[2]},
                            new int[] {col[3], col[4], col[5]});
                    recs = new byte[][] {icons.record(img, false), icons.record(img, true)};
                    bySeries.put(series, recs);
                }
                Out.save(job.out("icons/" + g.pack + ".gtx"), recs[0]);
                Out.save(job.out("icons/" + g.pack + "_r.gtx"), recs[1]);
            } catch (IOException | RuntimeException e) {
                job.progress.warn("Icon " + g.pack + ": " + e);
            }
        }
    }

    static void loading(Job job, List<Guest> roster, ExecutorService pool, int[] done, int total) {
        final Loading loading;
        try {
            loading = new Loading(Res.records(job.strikers.read(LOADING_RES)).get(0).b);
        } catch (IOException | RuntimeException e) {
            job.progress.warn("Loading pictures: " + e);
            return;
        }
        final List<Future<?>> jobs = new ArrayList<>();
        for (Guest g : roster) {
            final File png = job.out("portraits/" + g.pack + ".png");
            jobs.add(!png.isFile() ? null : pool.submit(() -> {
                final Png.Image im = Png.decode(Files.readAllBytes(png.toPath()));
                final byte[][] recs = loading.make(im.rgba, im.w, im.h);
                for (int i = 0; i < recs.length; i++)
                    Out.save(job.out("loading/" + g.pack + Loading.SUFFIX[i] + ".gtx"), recs[i]);
                return null;
            }));
        }
        for (int i = 0; i < roster.size(); i++) {
            final String pack = roster.get(i).pack;
            job.progress.step("Loading picture: " + pack, done[0]++, total);
            if (jobs.get(i) == null) {
                job.progress.warn("Loading picture " + pack + ": no portrait");
                continue;
            }
            try {
                jobs.get(i).get();
            } catch (Exception e) {
                job.progress.warn("Loading picture " + pack + ": " + (e.getCause() != null ? e.getCause() : e));
            }
        }
    }

    // ------------------------------------------------------------------ our data

    /** Our text file: guestdata/<name>, else the one in guests/ (null when neither). */
    static String ourText(Job job, String name) {
        try {
            if (job.data.has(name)) return job.data.text(name);
            final File f = job.out(name);
            if (f.isFile()) return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            job.progress.warn("Pictures: " + name + ": " + e.getMessage());
        }
        return null;
    }

    /** roster.txt (tools/outside/roster.py): pack, role, plays as, name, game [, franchise, from]. */
    static List<Guest> roster(Job job) {
        final List<Guest> out = new ArrayList<>();
        final String s = ourText(job, "roster.txt");
        if (s == null) {
            job.progress.warn("Pictures: no roster.txt");
            return out;
        }
        for (String line : s.split("\n")) {
            if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
            final String[] p = line.split("\t");
            if (p.length < 5 || line.startsWith("#")) continue;
            out.add(new Guest(p[0], p[1], p[4]));
        }
        return out;
    }

    /** colors.txt (tools/outside/guestcolors.py): pack -> main, trim, patch (RGB as floats). */
    static Map<String, float[][]> colours(Job job) {
        final String s = ourText(job, "colors.txt");
        if (s == null) {
            job.progress.warn("Kits: no colors.txt");
            return new LinkedHashMap<>();
        }
        return colours(s);
    }

    static Map<String, float[][]> colours(String s) {
        final Map<String, float[][]> out = new LinkedHashMap<>();
        for (String line : s.split("\n")) {
            final String[] p = line.trim().split("\\s+");
            if (p.length != 4 || p[0].startsWith("#")) continue;
            final float[][] c = new float[3][3];
            for (int k = 0; k < 3; k++)
                for (int i = 0; i < 3; i++) c[k][i] = Integer.parseInt(p[1 + k].substring(2 * i, 2 * i + 2), 16);
            out.put(p[0], c);
        }
        return out;
    }
}
