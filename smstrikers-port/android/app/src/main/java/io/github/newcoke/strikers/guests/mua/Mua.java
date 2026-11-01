package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Data;
import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Png;
import io.github.newcoke.strikers.guests.Source;
import io.github.newcoke.strikers.guests.pics.Banners;
import io.github.newcoke.strikers.guests.render.Etch;
import io.github.newcoke.strikers.guests.render.Gpk;
import io.github.newcoke.strikers.guests.render.HostRef;
import io.github.newcoke.strikers.guests.render.Nl;
import io.github.newcoke.strikers.guests.render.Portrait;

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
 * strikers-quest 1.16: the Marvel: Ultimate Alliance heroes (Wii, RMUE52), made on the headset from the player's own
 * disc as the PC tools made the ones on Lance's headset (tools/outside/mua):
 * <ul>
 * <li>guests/mua_*.gpk: each costume's match pack (mua2gpk.py + mua_cels.py: own idle / run, 4 celebrations);</li>
 * <li>guests/evo/mua_*.gev: its standing pose (build_costumes.gev_from_pack); guests/home/mua_*.gpk + .gev: its home
 * corner figure (build_home.py); guests/evo/music_hero_build.evm, music_hero_win.evm (mua_evo_music.py);</li>
 * <li>guests/stats/mua_*.ini (our own numbers, guestdata/mua/stats), guests/portraits/mua_*.png (hero_portraits.py's
 * renders, the CPU stand-in), guests/icons/mua_*[_r].gtx (hero_icons.py), their cells in guests/etch.r8.</li>
 * </ul>
 * The disc's packages are in z/assetsfb.wad (a 600 MB ZIP, read a piece at a time). Our own tuning: guestdata/mua
 * (heroes.txt, music.txt, libm.txt, stats/). The roster / colour lines are in guestdata/base (game "Ultimate Alliance").
 */
public final class Mua {
    private Mua() {}

    /** MUA (USA, Wii) */
    public static final String ID = "RMUE52";
    static final String PERMANENT = "packages/generated/maps/package/permanent.fb";
    static final String COMMON_ENTS = "packages/generated/common_ents_assets.fb";

    // ------------------------------------------------------------------------------------------------ the part
    /** Makes every hero file it can (nothing without the MUA disc); one costume's failure is a warning. */
    public static void run(Job job) {
        if (job.mua == null) return;
        final List<Hero> heroes;
        final Wad wad;
        final Igb shared;
        final byte[] fire4;
        try {
            heroes = heroes(job.data);
            wad = wad(job.mua);
            shared = shared(wad);
            fire4 = fire4(wad);
        } catch (IOException | RuntimeException | LinkageError e) {  // (LinkageError: a libguestdisc.so without nReadAt)
            job.progress.warn("Marvel heroes: " + e);
            return;
        }
        final List<Object[]> costumes = new ArrayList<>();  // {hero, costume number}
        for (Hero h : heroes) for (int c = 1; c <= h.costumes.length; c++) costumes.add(new Object[] {h, c});
        final int total = costumes.size() * 2 + 2;
        final AtomicInteger done = new AtomicInteger();
        final int threads = Math.max(1, Math.min(4, Runtime.getRuntime().availableProcessors() - 1));
        final ExecutorService pool = Executors.newFixedThreadPool(threads);
        try {
            // packs, poses, home figures, stats
            final List<Future<?>> fs = new ArrayList<>();
            for (Object[] hc : costumes) {
                final Hero h = (Hero) hc[0];
                final int c = (Integer) hc[1];
                fs.add(pool.submit(() -> {
                    final String pack = h.pack(c);
                    try {
                        costume(job, wad, h, c, shared, fire4);
                    } catch (Throwable e) {
                        warn(job, pack + ": " + e);
                    }
                    step(job, "Marvel: " + h.name + (c > 1 ? " (costume " + c + ")" : ""), done.incrementAndGet(), total);
                    return null;
                }));
            }
            waitAll(job, fs);
            try {
                for (Map.Entry<String, byte[]> e : music(job.mua, job.data).entrySet())
                    Out.save(job.out("evo/" + e.getKey() + ".evm"), e.getValue());
            } catch (IOException | RuntimeException e) {
                job.progress.warn("Marvel costume music: " + e);
            }
            step(job, "Marvel: costume music", done.incrementAndGet(), total);
            pictures(job, heroes, costumes, pool, done, total);
        } finally {
            pool.shutdownNow();
        }
        step(job, "Marvel heroes done", total, total);
    }

    private static void step(Job job, String what, int d, int t) {
        synchronized (job) {
            job.progress.step(what, d, t);
        }
    }

    private static void warn(Job job, String what) {
        synchronized (job) {
            job.progress.warn(what);
        }
    }

    private static void waitAll(Job job, List<Future<?>> fs) {
        for (Future<?> f : fs) {
            try {
                f.get();
            } catch (Exception e) {
                warn(job, "Marvel heroes: " + e);
            }
        }
    }

    /** One costume: its match pack, standing pose, home figure and stats into guests/. */
    static void costume(Job job, Wad wad, Hero h, int c, Igb shared, byte[] fire4) throws IOException {
        final String pack = h.pack(c);
        final String cid = h.costumes[c - 1];
        final HeroPack.Costume cos = HeroPack.open(wad.character(h.hero, cid), cid);
        final byte[] gpk = HeroCels.add(h, cos, HeroPack.base(h, c, cos, shared));
        final Home.Result home = Home.build(h, c, cos, gpk, shared, fire4);
        final byte[] gev = EvoPose.gev(gpk);
        Out.save(job.out("home/" + pack + ".gpk"), home.gpk);
        Out.save(job.out("home/" + pack + ".gev"), home.gev);
        if (gev != null) Out.save(job.out("evo/" + pack + ".gev"), gev);
        final String stats = "mua/stats/" + pack + ".ini";
        if (job.data.has(stats)) Out.save(job.out("stats/" + pack + ".ini"), job.data.read(stats));
        Out.save(job.out(pack + ".gpk"), gpk);  // (last: a pack there = the rest is there too)
    }

    // ------------------------------------------------------------------------------------------------ pictures
    /** Portraits, banners and the etch cells (the Strikers disc: its characters to pose on, its banner). */
    static void pictures(Job job, List<Hero> heroes, List<Object[]> costumes, ExecutorService pool, AtomicInteger done, int total) {
        if (job.strikers == null) {
            job.progress.warn("Marvel heroes: no Strikers disc, no portraits / banners");
            return;
        }
        final Map<String, Nl.Host> hosts = new HashMap<>();
        final Map<String, HostRef.Ref> refs = new HashMap<>(), refsAll = new HashMap<>();
        final Portrait.Look look;
        final Banners banners;
        try {
            final File hr = job.out("hostref.txt");
            final String hostref = hr.isFile() ? new String(Files.readAllBytes(hr.toPath()), StandardCharsets.UTF_8)
                    : HostRef.text(job.strikers);
            final String hostrefAll = job.data.has("base/hostref_all.txt") ? job.data.text("base/hostref_all.txt") : null;
            for (Hero h : heroes) {
                final String cls = HeroPics.CLS.get(h.slot.toLowerCase(Locale.ROOT));
                if (cls == null || hosts.containsKey(cls)) continue;
                hosts.put(cls, new Nl.Host(job.strikers, cls));
                refs.put(cls, HostRef.read(hostref, cls));
                if (hostrefAll != null) refsAll.put(cls, HostRef.read(hostrefAll, cls));
            }
            look = Portrait.Look.from(HeroPics.numbers(job.data.text("render/portrait.txt")));
            banners = new Banners(job.strikers);
        } catch (IOException | RuntimeException e) {
            job.progress.warn("Marvel heroes' pictures: " + e);
            return;
        }
        final List<Future<?>> fs = new ArrayList<>();
        for (Object[] hc : costumes) {
            final Hero h = (Hero) hc[0];
            final int c = (Integer) hc[1];
            fs.add(pool.submit(() -> {
                final String pack = h.pack(c);
                try {
                    final File f = job.out(pack + ".gpk");
                    if (f.isFile()) {
                        final String cls = HeroPics.CLS.get(h.slot.toLowerCase(Locale.ROOT));
                        final String stand = h.more.get("portrait_stand");
                        final byte[] rgba = HeroPics.portrait(new Gpk(Files.readAllBytes(f.toPath())), hosts.get(cls), refs.get(cls),
                                stand != null ? refsAll.get(cls) : null, stand, look);
                        Png.write(job.out("portraits/" + pack + ".png"), rgba, 256, 256);
                    }
                } catch (Throwable e) {
                    warn(job, pack + " portrait: " + e);
                }
                step(job, "Marvel pictures: " + h.name, done.incrementAndGet(), total);
                return null;
            }));
        }
        // banners (the hero's own for all its costumes) and the etch cells: from its home figure
        final Map<String, Integer> cells = etchCells(job);
        final Map<String, byte[]> cellBytes = new java.util.concurrent.ConcurrentHashMap<>();
        for (Hero h : heroes) {
            fs.add(pool.submit(() -> {
                try {
                    // its outline: its home figure in a frame of its loop (or its match pack's own idle: "figure own_idle")
                    final boolean own = "own_idle".equals(h.more.get("figure"));
                    final File gpkF = own ? job.out(h.pack + ".gpk") : job.out("home/" + h.pack + ".gpk");
                    final File gevF = job.out("home/" + h.pack + ".gev");
                    if (!gpkF.isFile() || (!own && !gevF.isFile())) return null;
                    final byte[] home = Files.readAllBytes(gpkF.toPath()), gev = own ? null : Files.readAllBytes(gevF.toPath());
                    final int frame = figureFrame(h);
                    final byte[] sil = HeroPics.silhouette(HeroPics.posedObj(home, gev, frame), 512);
                    final int[][] col = HeroPics.bannerColours(h.more.get("colors").split("\\s+"));
                    final byte[][] recs = banners.make(sil, 512, 512, col[0], col[1]);
                    for (int c = 1; c <= h.costumes.length; c++) {
                        if (!job.out(h.pack(c) + ".gpk").isFile()) continue;
                        Out.save(job.out("icons/" + h.pack(c) + ".gtx"), recs[0]);
                        Out.save(job.out("icons/" + h.pack(c) + "_r.gtx"), recs[1]);
                    }
                    final Integer cell = cells.get(h.pack);
                    if (cell != null) {
                        final io.github.newcoke.strikers.guests.render.Mesh m = own
                                ? io.github.newcoke.strikers.guests.render.Mesh.ownClip(new Gpk(home), "own_idle", 0.0)
                                : HeroPics.homeMesh(new Gpk(home), gev, frame);
                        if (m != null) cellBytes.put(h.pack, Etch.cell(Etch.silhouette(m, 4)));
                    }
                } catch (Throwable e) {
                    warn(job, h.pack + " banner / etching: " + e);
                }
                return null;
            }));
        }
        waitAll(job, fs);
        if (!cellBytes.isEmpty()) {
            try {
                final File ef = job.out("etch.r8");
                byte[] atlas = ef.isFile() ? Files.readAllBytes(ef.toPath()) : null;
                if (atlas == null || atlas.length != Etch.atlas().length) atlas = Etch.atlas();
                for (Map.Entry<String, byte[]> e : cellBytes.entrySet()) Etch.put(atlas, cells.get(e.getKey()), e.getValue());
                Out.save(ef, atlas);
            } catch (IOException | RuntimeException e) {
                job.progress.warn("Marvel heroes' etchings: " + e);
            }
        }
        step(job, "Marvel: banners + etchings", done.incrementAndGet(), total);
    }

    /** The heroes' etch cells: their place in guests/roster.txt (as render.Render numbers them; costumes none). */
    static Map<String, Integer> etchCells(Job job) {
        final Map<String, Integer> out = new HashMap<>();
        try {
            final File rf = job.out("roster.txt");
            if (!rf.isFile()) return out;
            int place = 0;
            for (String line : new String(Files.readAllBytes(rf.toPath()), StandardCharsets.UTF_8).split("\n")) {
                final String[] p = line.replace("\r", "").split("\t");
                if (line.startsWith("#") || p.length < 5) continue;
                final int cell = place++;
                if (p.length > 5 && p[5].equals("franchise")) continue;
                if (cell >= Etch.GRID * Etch.GRID) break;
                if (p[0].startsWith("mua_")) out.put(p[0], cell);
            }
        } catch (IOException e) {
            job.progress.warn("Marvel heroes' etchings: " + e);
        }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ pieces
    public static List<Hero> heroes(Data data) throws IOException {
        if (data.has("mua/libm.txt")) Libm.load(data.text("mua/libm.txt"));
        return Hero.parse(data.text("mua/heroes.txt"));
    }

    /** MUA's shared hero moves (permanent.fb actors/common.igb: the shared run, levelup, menu_kowned ...). */
    public static Igb shared(Wad wad) throws IOException {
        final Wad.Fb fb = new Wad.Fb(wad.read(PERMANENT));
        final byte[] d = fb.data("actors/common.igb");
        if (d == null) throw new IOException("no actors/common.igb in permanent.fb");
        return new Igb(d);
    }

    /** One costume's match pack (mua2gpk + mua_cels). */
    public static byte[] pack(Wad wad, Hero h, int costume, Igb shared) throws IOException {
        final HeroPack.Costume c = HeroPack.open(wad.character(h.hero, h.costumes[costume - 1]), h.costumes[costume - 1]);
        return HeroCels.add(h, c, HeroPack.base(h, costume, c, shared));
    }

    /** MUA's own flame picture (common_ents_assets.fb textures/fire4.igb: Ghost Rider's skull, the Human Torch). */
    public static byte[] fire4(Wad wad) throws IOException {
        final Wad.Fb fb = new Wad.Fb(wad.read(COMMON_ENTS));
        final byte[] d = fb.data("textures/fire4.igb");
        if (d == null) throw new IOException("no textures/fire4.igb in common_ents_assets.fb");
        return d;
    }

    /** A costume's home-corner figure: {home/<pack>.gpk, home/<pack>.gev}, from its match pack. */
    public static byte[][] home(Wad wad, Hero h, int costume, byte[] matchPack, Igb shared, byte[] fire4) throws IOException {
        final HeroPack.Costume c = HeroPack.open(wad.character(h.hero, h.costumes[costume - 1]), h.costumes[costume - 1]);
        final Home.Result r = Home.build(h, costume, c, matchPack, shared, fire4);
        return new byte[][] {r.gpk, r.gev};
    }

    /** A pack's standing pose for the select screen / costume unlock (guests/evo/<pack>.gev), null without own_idle. */
    public static byte[] evo(byte[] pack) {
        return EvoPose.gev(pack);
    }

    /** The costume-unlock music: name -> EVM1 bytes (guests/evo/music_hero_build.evm, music_hero_win.evm). */
    public static Map<String, byte[]> music(Source mua, Data data) throws IOException {
        final byte[] bank = mua.read(Music.BANK);
        final Map<String, byte[]> out = new java.util.LinkedHashMap<>();
        for (Music.Cut c : Music.cuts(data.text("mua/music.txt"))) out.put(c.name, Music.evm(bank, c));
        return out;
    }

    public static Wad wad(Source mua) throws IOException {
        return new Wad(mua);
    }

    /** PC checks: a hero's portrait (RGBA 256 x 256) as the converter draws it. */
    public static byte[] portrait(Source strikers, Data data, Hero h, byte[] pack, String hostref, Map<String, Nl.Host> hosts)
            throws IOException {
        final String cls = HeroPics.CLS.get(h.slot.toLowerCase(Locale.ROOT));
        Nl.Host host = hosts.get(cls);
        if (host == null) {
            host = new Nl.Host(strikers, cls);
            hosts.put(cls, host);
        }
        final String stand = h.more.get("portrait_stand");
        final HostRef.Ref refAll = stand != null ? HostRef.read(data.text("base/hostref_all.txt"), cls) : null;
        return HeroPics.portrait(new Gpk(pack), host, HostRef.read(hostref, cls), refAll, stand,
                Portrait.Look.from(HeroPics.numbers(data.text("render/portrait.txt"))));
    }

    /** The home loop frame its outline is taken from (0; Spider-Man's crouch 160); "own_idle": its match pack's idle. */
    static int figureFrame(Hero h) {
        final String f = h.more.get("figure");
        return f == null || f.equals("own_idle") ? 0 : Integer.parseInt(f);
    }

    /**
     * PC checks: a hero's banner emblem (the silhouette, RGBA 512 x 512) from its home figure (home + gev) or, for
     * "figure own_idle", from its match pack (match).
     */
    public static byte[] silhouette(Hero h, byte[] home, byte[] gev, byte[] match) {
        if ("own_idle".equals(h.more.get("figure"))) return HeroPics.silhouette(HeroPics.posedObj(match, null, 0), 512);
        return HeroPics.silhouette(HeroPics.posedObj(home, gev, figureFrame(h)), 512);
    }

    public static int[][] bannerColours(Hero h) {
        return HeroPics.bannerColours(h.more.get("colors").split("\\s+"));
    }
}
