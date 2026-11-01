package io.github.newcoke.strikers.guests.mua;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * One Marvel hero's tuning, ours (guestdata/mua/heroes.txt; tools/outside/mua heroes.json + the tools' per-hero
 * tables): numbers and names only. A block per hero:
 * <pre>
 * hero ghostrider              MUA's package name (packages/generated/characters/ghostrider_0601.fb)
 * pack mua_ghostrider          the guest pack (costume n > 1: pack_cn)
 * name Ghost Rider
 * costumes 0601 0602 0603 0604 its costumes' packages, the unlock order
 * slot luigi
 * height 1.62                  game units (as written in heroes.json)
 * keep cape_segment            segments kept in its pack (KEEP in build_heroes.py)
 * run shared                   its run: own (its main / combat set, then MUA's shared set), shared (the shared one)
 * runs own_idle own_run        the own clips the pack on the owner's headset has (an older tool: none from the
 *                              shared set), per costume when they differ ("runs.c1 own_idle")
 * cel power_9 | power_4 power_4_loop*2 power_4_end | ...    its 4 celebrations (mua_cels.py PLANS)
 * home ...                     its home figure's moves (build_home.py plan): see Home
 * extra claws                  claws / webs / fire / torch / board
 * lift all                     the home loop's lift counts every node (before r130), else fx points skipped
 * colors 1C1C1C FF6A10 FFB040  main / trim / patch (the banner's fill: hero_icons.py's rule)
 * figure 160                   the home loop frame its banner / etch outline comes from (0; own_idle: the pack's idle)
 * portrait_stand own_idle      its portrait posed with r124's own standing turn (r127's ten), else the plain retarget
 * card ...                     its card numbers (heroes.json; the stats .ini in guestdata/mua/stats come from them)
 * </pre>
 */
public final class Hero {
    public String hero, pack, name, slot, extra;
    public String[] costumes = new String[0];
    public String heightText;
    public double height;
    public String[] keep = new String[0];
    public boolean sharedRun;
    public boolean liftAll;
    /** The own clips (by costume number, 1-based; key 0 = all) the pack gets: "idle", "run" (null: both, as found). */
    public final Map<Integer, List<String>> runs = new LinkedHashMap<>();
    /** The 4 celebrations: each a list of (clip, repeats). */
    public final List<List<Object[]>> cels = new ArrayList<>();
    /** Home figure moves: (kind, [(clip, from, to | "hang" | null)]). */
    public final List<Object[]> home = new ArrayList<>();
    public final Map<String, String> more = new LinkedHashMap<>();

    public String pack(int costume) {
        return costume == 1 ? pack : pack + "_c" + costume;
    }

    public List<String> runs(int costume) {
        final List<String> r = runs.get(costume);
        return r != null ? r : runs.get(0);
    }

    public static List<Hero> parse(String text) throws IOException {
        final List<Hero> out = new ArrayList<>();
        Hero h = null;
        for (String line : text.split("\n")) {
            final String l = line.replace("\r", "").trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final int sp = l.indexOf(' ');
            final String k = sp < 0 ? l : l.substring(0, sp), v = sp < 0 ? "" : l.substring(sp + 1).trim();
            if (k.equals("hero")) {
                h = new Hero();
                h.hero = v;
                out.add(h);
                continue;
            }
            if (h == null) throw new IOException("mua/heroes.txt: " + l);
            switch (k) {
                case "pack": h.pack = v; break;
                case "name": h.name = v; break;
                case "costumes": h.costumes = v.split("\\s+"); break;
                case "slot": h.slot = v; break;
                case "height": h.heightText = v; h.height = Double.parseDouble(v); break;
                case "keep": h.keep = v.equals("-") ? new String[0] : v.split("[,\\s]+"); break;
                case "run": h.sharedRun = v.equals("shared"); break;
                case "lift": h.liftAll = v.equals("all"); break;
                case "extra": h.extra = v.equals("-") ? null : v; break;
                case "cel": {
                    final List<Object[]> c = new ArrayList<>();
                    for (String piece : v.split("\\s+")) {
                        final int star = piece.indexOf('*');
                        c.add(star < 0 ? new Object[] {piece, 1} : new Object[] {piece.substring(0, star), Integer.parseInt(piece.substring(star + 1))});
                    }
                    h.cels.add(c);
                    break;
                }
                case "home": {
                    // home <kind> clip[:from[:to]] ... (to: a number, "hang", or empty = to its end)
                    final String[] f = v.split("\\s+");
                    final List<Object[]> clips = new ArrayList<>();
                    for (int i = 1; i < f.length; i++) {
                        final String[] c = f[i].split(":", -1);
                        final double from = c.length > 1 && !c[1].isEmpty() ? Double.parseDouble(c[1]) : 0.0;
                        Object to = null;
                        if (c.length > 2 && !c[2].isEmpty()) to = c[2].equals("hang") ? "hang" : (Object) Double.parseDouble(c[2]);
                        clips.add(new Object[] {c[0], from, to});
                    }
                    h.home.add(new Object[] {Integer.parseInt(f[0]), clips});
                    break;
                }
                default: {
                    if (k.startsWith("runs")) {
                        final int costume = k.equals("runs") ? 0 : Integer.parseInt(k.substring(6));
                        final List<String> r = new ArrayList<>();
                        for (String x : v.split("\\s+")) if (!x.isEmpty() && !x.equals("-")) r.add(x);
                        h.runs.put(costume, r);
                    } else {
                        h.more.put(k, v);
                    }
                    break;
                }
            }
        }
        return out;
    }
}
