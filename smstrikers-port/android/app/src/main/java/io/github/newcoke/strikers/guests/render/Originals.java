package io.github.newcoke.strikers.guests.render;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * strikers-quest r183: the ORIGINALS (Quest 3 Strikers' own characters, shipped in the APK and staged into
 * STRIKERS_HOME/guests by StrikersHome) get their figures' pose files on this headset, from the game's own files like
 * every guest's (nothing of the game ships): guests/evo/[pack].gev (the standing loop: the 3D evolution, the hub's,
 * awards night's and the magazine's figures) and guests/cel/[pack].gev (its class's goal celebration: a card's moving
 * figure). Made once, on a low-priority thread at start; xr_cards.cpp makes any still missing when a card needs it.
 */
public final class Originals {
    private Originals() {}

    public static void prebakeLater(final String home, final String data) {
        final Thread t = new Thread(() -> prebake(home, data), "originals-figures");
        t.setPriority(Thread.MIN_PRIORITY);
        t.setDaemon(true);
        t.start();
    }

    static void prebake(String home, String data) {
        try {
            final File list = new File(home, "guests/originals.txt");
            if (!list.isFile()) return;
            int made = 0;
            for (String line : new String(Files.readAllBytes(list.toPath()), StandardCharsets.UTF_8).split("\n")) {
                if (line.startsWith("#")) continue;
                final String[] p = line.replace("\r", "").split("\t");
                if (p.length < 3 || !p[0].startsWith("orig_")) continue;
                final File gpk = new File(home, "guests/" + p[0] + ".gpk");
                if (!gpk.isFile()) continue;
                for (String dir : new String[] {"evo", "cel"}) {
                    final File out = new File(home, "guests/" + dir + "/" + p[0] + ".gev");
                    if (out.isFile() && out.length() > 0) continue;
                    final int r = Cel.bake(gpk.getPath(), out.getPath(), home, data != null ? data : "");
                    if (r == 1) made++;
                    else if (r < 0) System.out.println("Originals: " + p[0] + " " + dir + " failed: " + Cel.lastNote);
                    else if (dir.equals("evo")) return;  // (no Strikers files here yet: the cards make them later)
                }
            }
            System.out.println("Originals: " + made + " pose files made");
        } catch (Throwable e) {
            System.out.println("Originals: " + e);
        }
    }
}
