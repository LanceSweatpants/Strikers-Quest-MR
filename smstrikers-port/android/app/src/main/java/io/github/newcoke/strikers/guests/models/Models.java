package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Data;
import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.math.BigDecimal;
import java.math.MathContext;
import java.math.RoundingMode;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/**
 * The guest model packs (GPK2) made on the headset from the player's own Melee and Colosseum files, as the PC tools
 * made the ones on Lance's headset (tools/outside/meleepack.py via repack-guests.ps1; colo_guests.py + colopack.py,
 * colo_resize_finish.py's fixes, colo_flame.py, colo_shiny.py pack / rare):
 *  - every roster Melee fighter: guests/<pack>.gpk from its default costume Pl<Xx>Nr.dat;
 *  - every roster Colosseum Pokemon: guests/pk_<name>.gpk from pkx_<model>.fsys, and guests/pk_<name>_shiny.gpk
 *    (Colosseum's own shiny model's pictures where it has one, else our colour map over the normal pictures).
 * Our tuning (bone maps, visible lists, sizes, colour maps) comes from guestdata/models/*.txt.
 */
public final class Models {
    private Models() {}

    /** One roster pack's recipe (melee.txt / colo.txt). */
    static final class Spec {
        boolean colo;
        String pack, file;           // Melee: costume file; Colosseum: pkx model name
        MeleePack.BoneMap map = new MeleePack.BoneMap();
        Set<Integer> show;
        String heightM, gameHeight, hover, gasColor, gasOpacity, yaw;
        boolean shinyRare;
        int flameJoint = -1;
        double flameSize;
        final Set<Integer> drop = new TreeSet<>();
        final List<Integer> retex = new ArrayList<>();
    }

    /** Converts everything the player's games allow. Per-guest failures are warned about and skipped. */
    public static void run(Job job) {
        final List<Spec> specs;
        final Map<String, List<Shiny.Rule>> maps;
        try {
            specs = specs(job.data);
            maps = Shiny.parse(job.data.text("models/shiny.txt"));
        } catch (IOException | RuntimeException e) {
            job.progress.warn("guest models: our data is missing (" + e.getMessage() + ")");
            return;
        }
        final List<Spec> todo = new ArrayList<>();
        for (Spec s : specs) if (s.colo ? job.colo != null : job.melee != null) todo.add(s);
        int done = 0;
        for (Spec s : todo) {
            job.progress.step((s.colo ? "Colosseum: " : "Melee: ") + s.pack, done, todo.size());
            try {
                for (Map.Entry<String, byte[]> e : build(job, s, maps).entrySet()) Out.save(job.out(e.getKey()), e.getValue());
            } catch (IOException | RuntimeException e) {
                job.progress.warn(s.pack + ": " + e.getMessage());
            }
            done++;
        }
        job.progress.step("guest models done", done, todo.size());
    }

    /** One pack (and, for a Pokemon, its shiny) by pack name: written to job.guests. */
    public static void convert(Job job, String pack) throws IOException {
        for (Map.Entry<String, byte[]> e : make(job, pack).entrySet()) Out.save(job.out(e.getKey()), e.getValue());
    }

    /** One pack's files (file name -> bytes) without writing them. */
    public static Map<String, byte[]> make(Job job, String pack) throws IOException {
        for (Spec s : specs(job.data)) {
            if (s.pack.equals(pack)) return build(job, s, s.colo ? Shiny.parse(job.data.text("models/shiny.txt")) : null);
        }
        throw new IOException("no such guest pack: " + pack);
    }

    /** The roster packs this part makes. */
    public static List<String> packs(Data data) throws IOException {
        final List<String> out = new ArrayList<>();
        for (Spec s : specs(data)) out.add(s.pack);
        return out;
    }

    /**
     * For the PC data tool (tools/guestconv/test/ModelsData.java, libm.txt): every sin / cos argument the conversions
     * hand to fdlibm is added to sink (double bits); null stops it. libmHash: the key libm.txt lists them by.
     */
    public static void recordAngles(java.util.Set<Long> sink) {
        Trig.record = sink;
        Trig.load("");
    }

    public static int libmHash(double x) {
        return Trig.hash(x);
    }

    // ------------------------------------------------------------------------------------------------ building
    private static Map<String, byte[]> build(Job job, Spec s, Map<String, List<Shiny.Rule>> maps) throws IOException {
        final Map<String, byte[]> out = new LinkedHashMap<>();
        if (!s.colo) {
            if (job.melee == null) throw new IOException("no Melee files");
            final MeleePack.Options o = new MeleePack.Options();
            o.name = s.pack;
            o.map = s.map;
            o.show = s.show;
            out.put(s.pack + ".gpk", MeleePack.run(job.melee.read(s.file), o).gpk);
            return out;
        }
        final Source colo = job.colo;
        if (colo == null) throw new IOException("no Colosseum files");
        final byte[] fsys = colo.read("pkx_" + s.file + ".fsys");
        // the pack as colo_guests.py built it, with colo_resize_finish.py's fixes = colo-guests/final
        final Colo.Dat dat = Colo.datOf(fsys);
        byte[] fin = MeleePack.run(dat.dat, coloOptions(s, dat)).gpk;
        if (!s.drop.isEmpty()) fin = Colo.drop(fin, s.drop);
        // shiny: the final's pictures recoloured, or the rare_ model's pictures in it (a pack without its shiny
        // still plays: the shiny's failure is only warned about)
        byte[] shiny = null;
        try {
            if (s.shinyRare) {
                final Colo.Dat rare = Colo.datOf(colo.read("pkx_rare_" + s.file + ".fsys"));
                final MeleePack.Options o = coloOptions(s, rare);
                o.texturesOnly = true;
                o.retex = null;
                shiny = Colo.transplant(fin, MeleePack.run(rare.dat, o).texRecords);
            } else {
                final List<Shiny.Rule> rules = maps != null ? maps.get(s.pack) : null;
                if (rules == null || rules.isEmpty()) throw new IOException("no shiny colour map");
                shiny = new Shiny(rules).recolourPack(fin);
            }
            if (s.flameJoint >= 0) shiny = Colo.flame(fsys, shiny, s.flameJoint, s.flameSize);
        } catch (IOException | RuntimeException e) {
            job.progress.warn(s.pack + " shiny: " + e.getMessage());
            shiny = null;
        }
        if (s.flameJoint >= 0) fin = Colo.flame(fsys, fin, s.flameJoint, s.flameSize);
        out.put(s.pack + ".gpk", fin);
        if (shiny != null) out.put(s.pack + "_shiny.gpk", shiny);
        return out;
    }

    /** colo_guests.py + colopack.py's meleepack options for one Pokemon. */
    private static MeleePack.Options coloOptions(Spec s, Colo.Dat dat) {
        final MeleePack.Options o = new MeleePack.Options();
        o.name = s.pack;
        o.root = dat.root;
        o.modelSpaceEnv = true;
        o.subdivide = 1;
        if (dat.matanim != 0) o.matanim = dat.matanim;
        o.height = Py.num(Py.fixed(s.gameHeight != null ? Py.num(s.gameHeight) : gameHeight(Py.num(s.heightM)), 3));
        o.map = s.map;
        final double hover = s.hover != null ? Py.num(s.hover) : 0.0;
        if (s.gasColor != null) {
            final int v = Integer.parseInt(s.gasColor, 16);
            o.gasColor = new int[] {(v >> 16) & 255, (v >> 8) & 255, v & 255};
        }
        if (s.gasOpacity != null && Py.num(s.gasOpacity) != 0.0) o.gasOpacity = pctG(Py.num(s.gasOpacity));
        if (s.yaw != null && Py.num(s.yaw) != 0.0) o.upright = "y" + new BigDecimal(pctG(Py.num(s.yaw))).toPlainString();
        o.ground = Py.num(Py.fixed(hover, 3));
        o.retex = s.retex.isEmpty() ? null : s.retex;
        return o;
    }

    /** float("%g" % x): x to 6 significant digits (exact binary value, ties to even, as C's printf). */
    private static double pctG(double x) {
        if (x == 0.0) return x;
        return new BigDecimal(x).round(new MathContext(6, RoundingMode.HALF_EVEN)).doubleValue();
    }

    /**
     * colo_guests.game_height: clamp(1.25 + 0.4 log2(Pokedex height), 0.9, 1.9) (r93: Strikers-sized, each evolution
     * taller). log2 = ln / ln 2 (the PC's C log2 may differ in the last bit: the height is taken to 3 decimals).
     */
    static double gameHeight(double m) {
        final double l2 = StrictMath.log(m) / StrictMath.log(2.0);
        return MeleePack.pyMax(0.9, MeleePack.pyMin(1.9, 1.25 + 0.4 * l2));
    }

    // ------------------------------------------------------------------------------------------------ our data
    static List<Spec> specs(Data data) throws IOException {
        if (data.has("models/libm.txt")) Trig.load(data.text("models/libm.txt"));
        final List<Spec> out = new ArrayList<>();
        parse(data.text("models/melee.txt"), false, out);
        parse(data.text("models/colo.txt"), true, out);
        return out;
    }

    private static void parse(String text, boolean colo, List<Spec> out) throws IOException {
        Spec cur = null;
        for (String line : text.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] f = l.split("\\s+");
            final String k = f[0];
            if (k.equals("pack")) {
                cur = new Spec();
                cur.colo = colo;
                cur.pack = f[1];
                cur.file = f[2];
                out.add(cur);
                continue;
            }
            if (cur == null) throw new IOException("guest data: " + l);
            switch (k) {
                case "scale": cur.map.scaleHint = f[1]; break;
                case "show": {
                    cur.show = new HashSet<>();
                    for (int i = 1; i < f.length; i++) cur.show.add(Integer.parseInt(f[i]));
                    break;
                }
                case "class": cur.map.cls = f[1]; break;
                case "flap_l": cur.map.flapL = ints(f); break;
                case "flap_r": cur.map.flapR = ints(f); break;
                case "map": {
                    final int sp = l.indexOf(' ', 4);
                    cur.map.joints.add(Integer.parseInt(f[1]));
                    cur.map.bones.add(l.substring(sp + 1).trim());
                    break;
                }
                case "nomap": break;
                case "height_m": cur.heightM = f[1]; break;
                case "game_height": cur.gameHeight = f[1]; break;
                case "hover": cur.hover = f[1]; break;
                case "gas_color": cur.gasColor = f[1]; break;
                case "gas_opacity": cur.gasOpacity = f[1]; break;
                case "yaw": cur.yaw = f[1]; break;
                case "shiny": cur.shinyRare = f[1].equals("rare"); break;
                case "flame": {
                    cur.flameJoint = Integer.parseInt(f[1]);
                    cur.flameSize = Py.num(f[2]);
                    break;
                }
                case "drop": for (int i = 1; i < f.length; i++) cur.drop.add(Integer.parseInt(f[i])); break;
                case "retex": for (int i = 1; i < f.length; i++) cur.retex.add(Integer.parseInt(f[i], 16)); break;
                default: break;  // (unknown keys: newer data, ignored)
            }
        }
    }

    private static int[] ints(String[] f) {
        final int[] r = new int[f.length - 1];
        for (int i = 1; i < f.length; i++) r[i - 1] = Integer.parseInt(f[i]);
        return r;
    }
}
