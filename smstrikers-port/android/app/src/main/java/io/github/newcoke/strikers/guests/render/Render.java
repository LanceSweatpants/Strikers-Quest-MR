package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Png;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * strikers-quest 1.14: everything made by posing or drawing the guests' own models, from the packs already in
 * job.guests (the player's own game files, made by the models step):
 *  - guests/evo/pk_<name>.gev: each Colosseum Pokemon's standing idle for the 3D evolution (Evo; bit-identical to
 *    the PC's evo_pose.py),
 *  - guests/portraits/pk_<name>.png + pk_<name>_shiny.png (256 x 256, the whole figure) and sheik.png (136 x 188,
 *    head and shoulders: Melee has no picture of her): Portrait, the CPU stand-in for portrait_render.py's Cycles,
 *  - guests/etch.r8: the controller etchings atlas (Etch; a cell per roster place, empty for a guest not made),
 *  - guests/hostref.txt when it isn't there (HostRef: the Strikers characters' standing poses, from its disc).
 * A guest whose pack isn't there (its game missing) is skipped; one that fails is warned about and the rest go on.
 */
public final class Render {
    private Render() {}

    static final Map<String, String> CLASS = new HashMap<>();

    static {
        for (String c : new String[] {"Daisy", "DonkeyKong", "Luigi", "Mario", "Peach", "Waluigi", "Wario", "Yoshi", "Toad",
            "Koopa", "HammerBro", "Birdo"}) CLASS.put(c.toLowerCase(Locale.ROOT), c);
        CLASS.put("hammerbros", "HammerBro");
    }

    /** What one guest needs doing. */
    static final class Work {
        String pack, game, pose, portrait;
        int cell = -1;
    }

    public static void run(Job job) throws IOException {
        final List<Cfg.Guest> list = Cfg.guests(job.data);
        final Map<String, Double> limbs = Cfg.limbs(
                job.data.has("limbs.txt") ? job.data.text("limbs.txt") : job.data.has("render/limbs.txt") ? job.data.text("render/limbs.txt") : null);
        final Portrait.Look look = Portrait.Look.from(Cfg.numbers(job.data.text("render/portrait.txt")));
        // the etch cells: the roster the game reads (its non-franchise places, as etchres.py), else ours
        final Map<String, Work> work = new java.util.LinkedHashMap<>();
        for (Cfg.Guest g : list) {
            final Work w = new Work();
            w.pack = g.pack;
            w.game = g.game;
            w.pose = g.pose;
            w.portrait = g.portrait;
            w.cell = g.cell;
            work.put(g.pack, w);
        }
        final File rosterFile = job.out("roster.txt");
        final String roster = rosterFile.isFile() ? new String(Files.readAllBytes(rosterFile.toPath()), StandardCharsets.UTF_8)
                : job.data.has("roster.txt") ? job.data.text("roster.txt") : null;
        if (roster != null) {
            for (Work w : work.values()) w.cell = -1;
            int cell = 0;
            for (String line : roster.split("\n")) {
                final String[] p = line.replace("\r", "").split("\t");
                if (line.startsWith("#") || p.length < 5) continue;
                if (p.length > 5 && p[5].equals("franchise")) continue;
                if (cell >= Etch.GRID * Etch.GRID) break;
                Work w = work.get(p[0]);
                if (w == null) {  // a guest we don't list: posed on its slot (retarget) or by its own clip
                    w = new Work();
                    w.pack = p[0];
                    w.game = "?";
                    w.pose = CLASS.getOrDefault(p[2].toLowerCase(Locale.ROOT), "own");
                    w.portrait = "-";
                    work.put(p[0], w);
                }
                w.cell = cell++;
            }
        }
        // the Strikers characters the retarget packs stand on, and their standing poses
        final Map<String, Nl.Host> hosts = new HashMap<>();
        String hostref = null;
        if (job.strikers != null) {
            try {
                hostref = HostRef.text(job.strikers);
                final File hr = job.out("hostref.txt");
                if (!hr.isFile()) Out.save(hr, hostref.getBytes(StandardCharsets.UTF_8));
                for (Work w : work.values())
                    if (!w.pose.equals("own") && !hosts.containsKey(w.pose)) hosts.put(w.pose, new Nl.Host(job.strikers, w.pose));
            } catch (IOException | RuntimeException e) {
                job.progress.warn("Strikers characters: " + e.getMessage() + " (no guest poses or pictures)");
                hosts.clear();
                hostref = null;
            }
        } else {
            job.progress.warn("no Strikers disc: no guest poses or pictures");
        }
        final List<Work> todo = new ArrayList<>();
        for (Work w : work.values())
            if (new File(job.guests, w.pack + ".gpk").isFile() && (w.game.equals("colo") || w.cell >= 0 || !w.portrait.equals("-")))
                todo.add(w);
        final byte[] atlas = Etch.atlas();
        final int total = todo.size();
        final AtomicInteger done = new AtomicInteger();
        final String hostrefText = hostref;
        final int threads = Math.max(1, Math.min(4, Runtime.getRuntime().availableProcessors() - 1));
        final ExecutorService pool = Executors.newFixedThreadPool(threads);
        try {
            final List<Future<?>> fs = new ArrayList<>();
            for (Work w : todo) {
                fs.add(pool.submit(() -> {
                    try {
                        final byte[] cell = one(job, w, hosts, hostrefText, limbs, look);
                        if (cell != null) {
                            synchronized (atlas) {
                                Etch.put(atlas, w.cell, cell);
                            }
                        }
                    } catch (Throwable e) {
                        synchronized (done) {
                            job.progress.warn(w.pack + ": " + e);
                        }
                    }
                    synchronized (done) {  // (the launcher's progress line is told one thing at a time)
                        job.progress.step("Pictures: " + nice(w.pack), done.incrementAndGet(), total);
                    }
                    return null;
                }));
            }
            for (Future<?> f : fs) {
                try {
                    f.get();
                } catch (Exception e) {
                    job.progress.warn("render: " + e);
                }
            }
        } finally {
            pool.shutdown();
        }
        Out.save(job.out("etch.r8"), atlas);
    }

    /** One guest: its pose file, pictures and etch cell (returned; null for none). */
    static byte[] one(Job job, Work w, Map<String, Nl.Host> hosts, String hostref, Map<String, Double> limbs, Portrait.Look look)
            throws IOException {
        final Gpk pack = new Gpk(Files.readAllBytes(new File(job.guests, w.pack + ".gpk").toPath()));
        final boolean own = w.pose.equals("own") || !pack.v2;
        if (w.portrait.equals("bust")) {  // its bind pose, as it lies: needs nothing else
            final Mesh m = Mesh.bind(pack);
            final Portrait.Scene s = Portrait.scene(m, 136, 188, false, Double.NaN, look);
            Png.write(job.out("portraits/" + w.pack + ".png"), Portrait.render(s, m, look), 136, 188);
        }
        final Nl.Host host = own ? null : hosts.get(w.pose);
        if (!own && host == null) return null;  // no Strikers characters: nothing to stand it on
        final double arms = limbs.getOrDefault(w.pack, 1.0);
        final HostRef.Ref ref = hostref != null && host != null ? HostRef.read(hostref, host.cls) : null;
        double[][][] idle = null;
        if (!own) idle = new Retarget(host, "idle", pack, ref, arms).pose(0.0);
        if (w.game.equals("colo") && !own) {
            Out.save(job.out("evo/" + w.pack + ".gev"), Evo.gev(host, pack, ref, arms));
        }
        if (w.portrait.equals("full") && idle != null) {
            final Mesh m = Mesh.skinned(pack, idle, look.flames);
            final Portrait.Scene s = Portrait.scene(m, 256, 256, true, -70, look);
            Png.write(job.out("portraits/" + w.pack + ".png"), Portrait.render(s, m, look), 256, 256);
            final File shinyFile = new File(job.guests, w.pack + "_shiny.gpk");
            if (shinyFile.isFile()) {
                final Gpk shiny = new Gpk(Files.readAllBytes(shinyFile.toPath()));
                Portrait.Scene ss = s;
                Mesh sm;
                if (shiny.nodes.length == pack.nodes.length) {
                    sm = Mesh.skinned(shiny, idle, look.flames);
                } else {
                    sm = Mesh.skinned(shiny, new Retarget(host, "idle", shiny, ref, arms).pose(0.0), look.flames);
                }
                if (!sameShape(m, sm)) ss = Portrait.scene(sm, 256, 256, true, -70, look);
                Png.write(job.out("portraits/" + w.pack + "_shiny.png"), Portrait.render(ss, sm, look), 256, 256);
            }
        }
        if (w.cell < 0) return null;
        final Mesh sil = own ? Mesh.ownClip(pack, "idle", 0.0) : Mesh.skinned(pack, idle, false);
        if (sil == null) return null;
        return Etch.cell(Etch.silhouette(withoutSoft(sil), 4));
    }

    /** The outline is the body's: no gas cards or flames (etchres.py's models had none). */
    static Mesh withoutSoft(Mesh m) {
        int keep = 0;
        for (int k = 0; k < m.nt; k++)
            if (!m.mats.get(m.triMat[k]).soft) keep++;
        if (keep == m.nt) return m;
        final int[] tri = new int[keep * 3], mat = new int[keep];
        int t = 0;
        for (int k = 0; k < m.nt; k++) {
            if (m.mats.get(m.triMat[k]).soft) continue;
            System.arraycopy(m.tri, k * 3, tri, t * 3, 3);
            mat[t++] = m.triMat[k];
        }
        final Mesh o = new Mesh();
        o.nv = m.nv;
        o.p = m.p;
        o.n = m.n;
        o.uv = m.uv;
        o.nt = keep;
        o.tri = tri;
        o.triMat = mat;
        o.mats.addAll(m.mats);
        return o;
    }

    static boolean sameShape(Mesh a, Mesh b) {
        return a.nv == b.nv && a.nt == b.nt && java.util.Arrays.equals(a.p, b.p) && java.util.Arrays.equals(a.tri, b.tri);
    }

    static String nice(String pack) {
        String n = pack.startsWith("pk_") ? pack.substring(3) : pack;
        return n.isEmpty() ? pack : Character.toUpperCase(n.charAt(0)) + n.substring(1);
    }
}
