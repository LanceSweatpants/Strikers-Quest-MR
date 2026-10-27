package io.github.newcoke.strikers.guests;

import io.github.newcoke.strikers.guests.audio.Audio;
import io.github.newcoke.strikers.guests.charged.Charged;
import io.github.newcoke.strikers.guests.models.Models;
import io.github.newcoke.strikers.guests.pics.Pics;
import io.github.newcoke.strikers.guests.render.Render;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.Set;

/**
 * strikers-quest 1.14: the whole guest converter, in order. Our own data first (the roster cut to the games the player
 * has: every later part, and the game's etching cells, go by it), then the models, Charged, the sounds, the pictures
 * that come straight off the discs, the renders (evolution poses, Pokemon portraits, etching) and last the pictures made
 * from portraits (icons, loading pictures, kits). Plain Java: the PC checks run the very same thing.
 */
public final class Convert {
    private Convert() {}

    /** Raised whenever the converter's output changes: headsets make their guests again (GuestSetup's stamp). */
    public static final int VERSION = 1;

    /** Makes everything it can; returns how many guests are ready. One guest's failure is a warning, not the end. */
    public static int run(Job job) throws IOException {
        final String[] names = {"Our data", "Models", "Charged", "Sounds", "Pictures", "Portraits + poses", "Kits + loading"};
        final int[] weight = {1, 30, 5, 4, 3, 32, 25};
        int total = 0;
        for (int w : weight) total += w;
        final int[] base = new int[weight.length];
        for (int i = 1; i < weight.length; i++) base[i] = base[i - 1] + weight[i - 1];
        final int all = total;

        Set<String> kept = writeBase(sub(job, names[0], base[0], weight[0], all));
        Models.run(sub(job, names[1], base[1], weight[1], all));
        Charged.run(sub(job, names[2], base[2], weight[2], all));
        Audio.run(sub(job, names[3], base[3], weight[3], all));
        Pics.portraits(sub(job, names[4], base[4], weight[4], all));
        Render.run(sub(job, names[5], base[5], weight[5], all));
        Pics.run(sub(job, names[6], base[6], weight[6], all));
        job.progress.step("Done", all, all);

        int ready = 0;
        for (String pack : kept)
            if (job.out(pack + ".gpk").isFile()) ready++;
            else job.progress.warn(pack + ": not made");
        return ready;
    }

    /** A part's own steps (done / total of its own) as its share of the whole run. */
    private static Job sub(final Job job, final String part, final int base, final int weight, final int all) {
        return new Job(job.strikers, job.melee, job.colo, job.charged, job.data, job.guests, new Job.Progress() {
            @Override
            public void step(String what, int done, int total) {
                final int t = Math.max(1, total);
                job.progress.step(what, base * 1000 + (int) ((long) weight * 1000 * Math.min(done, t) / t), all * 1000);
            }

            @Override
            public void warn(String what) {
                job.progress.warn(what);
            }
        });
    }

    /**
     * Our own per-guest data into guests/: the roster (only the guests whose game the player has), the colours, the arm
     * sizes and each kept guest's stats. Returns the kept packs.
     */
    static Set<String> writeBase(Job job) throws IOException {
        job.progress.step("Your guests' list", 0, 1);
        final String roster = job.data.text("base/roster.txt");
        final StringBuilder out = new StringBuilder();
        final Set<String> kept = new LinkedHashSet<>();
        for (String line : roster.split("\n")) {
            String l = line.replace("\r", "");
            if (l.isEmpty() || l.startsWith("#")) {
                out.append(l).append('\n');
                continue;
            }
            String[] c = l.split("\t");
            if (c.length < 5) continue;
            if (!has(job, c[4])) continue;
            kept.add(c[0]);
            out.append(l).append('\n');
        }
        Out.save(job.out("roster.txt"), out.toString().getBytes(StandardCharsets.UTF_8));
        for (String f : new String[] {"colors.txt", "limbs.txt"})
            Out.save(job.out(f), job.data.read("base/" + f));
        for (String pack : kept) {
            final String name = "base/stats/" + pack + ".ini";
            if (job.data.has(name)) Out.save(job.out("stats/" + pack + ".ini"), job.data.read(name));
        }
        job.progress.step("Your guests' list", 1, 1);
        return kept;
    }

    /** The roster's game column -> whether the player has that game. */
    private static boolean has(Job job, String game) {
        switch (game.toLowerCase(Locale.ROOT)) {
            case "melee":
            case "melee pokemon":
                return job.melee != null;
            case "colosseum":
                return job.colo != null;
            case "charged":
                return job.charged != null;
            default:
                return false;
        }
    }
}
