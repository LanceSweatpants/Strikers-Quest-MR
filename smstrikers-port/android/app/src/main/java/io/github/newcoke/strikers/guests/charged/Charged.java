package io.github.newcoke.strikers.guests.charged;

import io.github.newcoke.strikers.guests.Data;
import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Png;
import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The 8 Mario Strikers Charged guests from the player's own Charged disc (Wii, R4QE01): per guest its GPK1 pack
 * (guests/&lt;pack&gt;.gpk: model, skin, its own texture records, its own animations; tools/outside/guestpack.py,
 * byte for byte) and its portrait (guests/portraits/&lt;pack&gt;.png: the captain / sidekick select picture from
 * Art/fe/mainui.dmn; tools/outside/chargedres.py + build_portraits.py, pixel for pixel).
 *
 * Which guests, their portraits' texture paths and the clips left out: our data, guestdata/charged/guests.txt.
 * Disc paths (the FST's, case as on the disc): Art/characters/&lt;n&gt;/&lt;n&gt;.rlg + .rlt,
 * Art/animation/&lt;n&gt;.shier + .sanim.zlib, Art/fe/mainui.dmn.
 */
public final class Charged {
    private Charged() {}

    public static final String DATA = "charged/guests.txt";

    /** One guest: its pack name (= the Charged character's name), its portrait, its name. */
    public static final class Guest {
        public final String pack, bundle, portrait, shown;

        Guest(String pack, String bundle, String portrait, String shown) {
            this.pack = pack;
            this.bundle = bundle;
            this.portrait = portrait;
            this.shown = shown;
        }
    }

    /** guestdata/charged/guests.txt read. */
    public static final class Setup {
        public final List<Guest> guests;
        public final Set<String> skipAnims;

        Setup(List<Guest> guests, Set<String> skipAnims) {
            this.guests = Collections.unmodifiableList(guests);
            this.skipAnims = Collections.unmodifiableSet(skipAnims);
        }

        public Guest guest(String pack) {
            for (Guest g : guests) if (g.pack.equals(pack)) return g;
            return null;
        }
    }

    public static Setup setup(Data data) throws IOException {
        final ArrayList<Guest> guests = new ArrayList<>();
        final HashSet<String> skip = new HashSet<>();
        for (String line : data.text(DATA).split("\n")) {
            line = line.replace("\r", "");
            if (line.trim().isEmpty() || line.startsWith("#")) continue;
            final String[] c = line.split("\t");
            if (c[0].equals("guest") && c.length >= 5) guests.add(new Guest(c[1], c[2], c[3], c[4]));
            else if (c[0].equals("skip") && c.length >= 2) skip.add(c[1]);
            else throw new IOException(DATA + ": can't read \"" + line + "\"");
        }
        return new Setup(guests, skip);
    }

    /** Every Charged guest into job.guests (nothing when the player has no Charged disc). */
    public static void run(Job job) {
        if (job.charged == null) return;
        final Setup s;
        try {
            s = setup(job.data);
        } catch (IOException e) {
            job.progress.warn("Charged: " + e.getMessage());
            return;
        }
        final HashMap<String, Bundle> bundles = new HashMap<>();
        final int total = s.guests.size();
        int done = 0;
        for (Guest g : s.guests) {
            job.progress.step("Charged: " + g.shown, done, total);
            try {
                GuestPack.build(job.charged, g.pack, s.skipAnims).save(job.out(g.pack + ".gpk"));
            } catch (Exception | OutOfMemoryError e) {
                job.progress.warn("Charged: " + g.shown + " left out (" + why(e) + ")");
                done++;
                continue;
            }
            try {
                Bundle b = bundles.get(g.bundle);
                if (b == null) {
                    b = Bundle.read(job.charged.read(g.bundle));
                    bundles.put(g.bundle, b);
                }
                final Bundle.Pic p = b.find(g.portrait);
                if (p == null) throw new IOException("no " + g.portrait + " in " + g.bundle);
                Png.write(job.out("portraits/" + g.pack + ".png"), p.rgba, p.w, p.h);
            } catch (Exception | OutOfMemoryError e) {
                job.progress.warn("Charged: " + g.shown + ": no portrait (" + why(e) + ")");
            }
            done++;
        }
        job.progress.step("Charged: done", total, total);
    }

    private static String why(Throwable e) {
        return e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
    }

    /** One guest's pack (tests): the .gpk bytes. */
    public static byte[] pack(Source charged, Data data, String pack) throws IOException {
        return GuestPack.build(charged, pack, setup(data).skipAnims).toBytes();
    }

    /** One guest's pack written straight to a file (no extra copy). */
    public static void pack(Source charged, Data data, String pack, java.io.File out) throws IOException {
        final Out o = GuestPack.build(charged, pack, setup(data).skipAnims);
        o.save(out);
    }

    /** One guest's portrait (tests), or null when its bundle has none. */
    public static Png.Image portrait(Source charged, Data data, String pack) throws IOException {
        final Guest g = setup(data).guest(pack);
        if (g == null) throw new IOException("not a Charged guest: " + pack);
        final Bundle.Pic p = Bundle.read(charged.read(g.bundle)).find(g.portrait);
        return p == null ? null : new Png.Image(p.w, p.h, p.rgba);
    }
}
