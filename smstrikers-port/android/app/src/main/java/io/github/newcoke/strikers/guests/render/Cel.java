package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;

import java.io.File;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * strikers-quest r141 (coordinator, for Lance's BLACK 1/1s: "their OWN celebration on a loop ... the way the game itself
 * animates them in matches"): a retarget guest's (a Pokemon's, a Melee fighter's, a shiny one's) celebration as a match
 * poses it in the goal cut-scene - its Strikers class's own goal celebration (art/nis/<class>_goal_winner_*.nis: the clip
 * the cut-scene plays on that class's captain, on its own skeleton) through the game's retarget (Retarget, as BigPose and
 * Evo), with the match's own rules for it:
 * <ul>
 * <li>its arms (limbs.txt "arms"), the class's hostref, as in a match;</li>
 * <li>a flier's wings beating and its bob, a floater's bob, a gas cloud's swirl (xr_guest.cpp r69: Retarget's layers);</li>
 * <li>a long body's sway (limbs.txt "slither": xr_guest.cpp slither_world standing still), a top's slow turn ("spin");</li>
 * <li>on the spot: its hips' turn and place held where its idle has them (the cut-scene runs the captain about the
 * pitch: here it celebrates where it stands, facing you);</li>
 * <li>on the floor: nobody through it; a walker's lowest point where the captain's soles are (it jumps when he jumps,
 * r130 host_sole); fliers and floaters keep their hover.</li>
 * </ul>
 * Written as guests/cel/[pack].gev (xr_cards.cpp: a BLACK 1/1's throne and cameo): GEV1 - its class's idle, then the
 * celebration - with SEG1 (kind 0 idle, kind 5 cheer; its head node), as the home figures' files. A celebration that tears
 * the model (an edge 35% of its height longer than at idle) in more than a fifth of its keys is left out: its idle only
 * (xr_cards.cpp then bobs it). Every turn here through StrictMath (Trig) and the rest plain IEEE arithmetic: the same bytes
 * on every headset and on the PC (tools/guestconv CelTest).
 */
public final class Cel {
    private Cel() {}

    /** The class's goal celebrations tried in turn: the first on the class's own skeleton (its node count) wins. */
    static final String[] NIS = {"high_0", "high_1", "low_0"};
    // (r141 PC check: at BigPose's 15% a Gengar opening his mouth, a Butterfree's wings beating, a Snorlax turning "tore":
    // a celebration stretches a face and a joint; a limb pulled off its body stretches an edge a third of its height)
    static final double TORN = 0.35, TORN_KEYS = 0.2;
    private static final int L_THIGH = Py.lowerHash("bip01 l thigh"), R_THIGH = Py.lowerHash("bip01 r thigh");
    private static final int HB = Py.lowerHash("bip01"), HP = Py.lowerHash("bip01 pelvis"), HEAD = Py.lowerHash("bip01 head");
    private static final int[] FEET = {Py.lowerHash("bip01 l foot"), Py.lowerHash("bip01 r foot"), Py.lowerHash("bip01 l toe0"),
        Py.lowerHash("bip01 r toe0")};
    /** The last bake's account (the PC check's list, the headset's log). */
    public static volatile String lastNote = "";
    /** The PC check: a tearing celebration kept anyway (to look at it). */
    public static volatile boolean keepTorn = false;

    /** The class's goal celebrations on its own skeleton (its node count), in NIS's order. */
    static List<Nl.Clip> celebrations(Source strikers, Nl.Host h) {
        final List<Nl.Clip> out = new ArrayList<>();
        for (String v : NIS) {
            final String path = "art/nis/" + h.cls.toLowerCase(Locale.ROOT) + "_goal_winner_" + v + ".nis";
            try {
                if (!strikers.has(path)) continue;
                final Nl.Clip c = Nl.clip(strikers.read(path), "nis");
                if (c != null && c.nodes == h.shier.ids.length && c.keys > 4) out.add(c);
            } catch (Exception e) {
                // (the next one)
            }
        }
        return out;
    }

    /**
     * The headset (xr_cards.cpp, through JNI, on a BLACK 1/1's build thread): the pack's celebration into `out`
     * (guests/cel/[pack].gev); home: STRIKERS_HOME, data: STRIKERS_DATA ("" to find the game's files in home). 1 written,
     * 0 it has none to make (no class, no Strikers files), -1 failed.
     */
    public static int bake(String gpkPath, String out, String home, String data) {
        try {
            BigPose.setup(home, data);
            String base = new File(gpkPath).getName();
            if (base.endsWith(".gpk")) base = base.substring(0, base.length() - 4);
            if (base.endsWith("_shiny")) base = base.substring(0, base.length() - 6);
            // r183: asked for guests/evo/<pack>.gev (an ORIGINAL's standing loop, xr_cards.cpp / Originals.prebake): its
            // class's idle through the retarget, as the converter's Evo makes a guest's
            final Gpk gp = new Gpk(Files.readAllBytes(new File(gpkPath).toPath()));
            final byte[] g = out.replace('\\', '/').contains("/evo/") ? makeEvo(base, gp) : make(base, gp);
            if (g == null) return 0;
            final File f = new File(out), tmp = new File(out + ".tmp");
            final File dir = f.getParentFile();
            if (dir != null && !dir.isDirectory() && !dir.mkdirs()) return -1;
            Out.save(tmp, g);
            if (!tmp.renameTo(f)) {
                tmp.delete();
                return -1;
            }
            return 1;
        } catch (Throwable e) {
            lastNote = gpkPath + ": " + e;
            System.out.println("Cel: " + lastNote);
            return -1;
        }
    }

    /** r183: the pack's standing idle (GEV1, Evo.gev), null when it has no class / no Strikers files here. */
    public static byte[] makeEvo(String base, Gpk pack) {
        final String cls = BigPose.classOf(base);
        if (cls == null || (pack.mode & 1) == 0) {
            lastNote = base + ": no class here / not a retarget pack";
            return null;
        }
        final Nl.Host host = BigPose.hostOf(cls);
        if (host == null) {
            lastNote = base + ": no Strikers files for " + cls;
            return null;
        }
        return Evo.gev(host, pack, BigPose.refOf(cls), BigPose.armsOf(base));
    }

    /** The file's bytes for this pack (its base name: the roster's), null when it has no class / no Strikers files here. */
    public static byte[] make(String base, Gpk pack) {
        final String cls = BigPose.classOf(base);
        if (cls == null || (pack.mode & 1) == 0) {
            lastNote = base + ": no class here / not a retarget pack";
            return null;
        }
        final Nl.Host host = BigPose.hostOf(cls);
        final Source strikers = BigPose.strikersFiles();
        if (host == null || strikers == null) {
            lastNote = base + ": no Strikers files for " + cls;
            return null;
        }
        final Nl.Clip idle = Nl.clip(host.sanim, "idle");
        if (idle == null) {
            lastNote = base + ": no idle for " + cls;
            return null;
        }
        final List<Nl.Clip> cels = celebrations(strikers, host);
        double slither = 0.0, spin = 0.0;
        final String limbs = BigPose.limbs();
        if (limbs != null)
            for (String line : limbs.split("\n")) {
                final String[] p = line.trim().split("\\s+");
                if (p.length < 3 || p[0].startsWith("#") || !p[0].equals(base)) continue;
                try {
                    if (p[1].equals("slither")) slither = Double.parseDouble(p[2]);
                    if (p[1].equals("spin")) spin = Double.parseDouble(p[2]);
                } catch (NumberFormatException e) {
                    // (not a number: none)
                }
            }
        final HostRef.Ref ref = BigPose.refOf(cls);
        final Seg s = new Seg();
        s.host = host;
        s.pack = pack;
        s.slither = slither;
        s.spin = spin;
        s.flies = (pack.mode & 2) != 0;
        s.floats = (pack.mode & 4) != 0;
        s.shape = new BigPose.Shape(pack);
        final double arms = BigPose.armsOf(base);
        // where it stands: its idle's first key (its hips' place and turn, the captain's soles, its points for the tear test)
        final Retarget ri = new Retarget(host, idle, pack, ref, arms);
        final double[][][] w0 = ri.poseKey(0);
        s.at0 = hips(pack, w0, ri.lastHost, host);
        s.sole0 = sole(host, ri.lastHost);
        s.p0 = s.shape.points(w0);
        final double[] mid0 = middle(s.p0);  // (its middle at idle: where it stands; its turn from the hips)
        s.at0[0] = mid0[0];
        s.at0[1] = mid0[1];
        s.h0 = height(s.p0);
        final List<double[][][]> frames = new ArrayList<>(s.run(ri, idle.keys()));
        final int idleKeys = idle.keys();
        // its celebration: the class's first goal celebration that doesn't tear it (r141 PC check: Mario's first one
        // pulled Diglett, Grimer, Magmar and the seals long; his others don't)
        int celKeys = 0, torn = 0, tried = 0;
        for (Nl.Clip cel : cels) {
            tried++;
            final List<double[][][]> seg = s.run(new Retarget(host, cel, pack, ref, arms), cel.keys());
            torn = s.torn;
            if (s.torn > TORN_KEYS * cel.keys() && !keepTorn) continue;
            frames.addAll(seg);
            celKeys = cel.keys();
            break;
        }
        final boolean flies = s.flies, floats = s.floats;
        if (celKeys == 0 && tried > 0)
            lastNote = String.format(Locale.ROOT, "%s on %s: each of its %d goal celebrations tears it (%d of the last's keys): its idle only",
                    base, cls, tried, torn);
        if (celKeys > 0)
            lastNote = String.format(Locale.ROOT, "%s on %s: idle %d + celebration %d keys (%d torn, celebration %d of %d)%s%s%s%s", base,
                    cls, idleKeys, celKeys, torn, tried, cels.size(), flies ? ", flies" : "", floats ? ", floats" : "",
                    slither > 0 ? ", slithers" : "", spin > 0 ? ", spins" : "");
        else if (cels.isEmpty())
            lastNote = base + " on " + cls + ": no goal celebration on its skeleton: its idle only";
        // GEV1 + SEG1
        int head = -1;
        for (int i = 0; i < pack.nodes.length && head < 0; i++)
            if (pack.nodes[i].map == HEAD) head = i;
        final int nn = pack.nodes.length;
        final Out o = new Out();
        o.bytes(new byte[] {'G', 'E', 'V', '1'}).le32(nn).le32(frames.size()).lef32(30.0f);
        for (double[][][] w : frames)
            for (double[][] m : w)
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 3; c++) o.lef32((float) m[r][c]);
        final int segs = celKeys > 0 ? 2 : 1;
        o.bytes(new byte[] {'S', 'E', 'G', '1'}).le32(segs).le32(head);
        o.le32(0).le32(idleKeys).le32(0);
        if (celKeys > 0) o.le32(idleKeys).le32(celKeys).le32(5);
        return o.toBytes();
    }

    /** One clip's keys made as a match would show them standing on its throne (the layers, the spot, the floor). */
    static final class Seg {
        Nl.Host host;
        Gpk pack;
        double slither, spin, sole0, h0;
        boolean flies, floats;
        BigPose.Shape shape;
        double[] at0;
        float[] p0;
        int torn;  // (the last run's keys that tear it)

        List<double[][][]> run(Retarget r, int n) {
            final List<double[][][]> segFrames = new ArrayList<>();
            int segTorn = 0;
            for (int k = 0; k < n; k++) {
                final double u = (double) k / n;  // (its share of the loop: the layers go round a whole number of times)
                r.layers = true;  // (a flier's wings, a floater's / flier's bob, a gas cloud's swirl: none of them elsewhere)
                r.flies = flies;
                r.flap = 0.0;
                r.bob = 0.0;
                if (flies) {
                    final double beat = 2.0 * Math.PI * Math.max(1, Math.round(2.2 * n / 30.0)) * u;
                    r.flap = 0.15 + 0.5 * Trig.sin(beat);
                    r.bob = 0.035 * Trig.cos(beat);
                } else if (floats) {
                    r.bob = 0.04 * Trig.sin(2.0 * Math.PI * Math.max(1, Math.round(0.7 * n / 30.0)) * u);
                }
                r.swirl = 2.0 * Math.PI * u;
                final double[][][] w = r.poseKey(k);
                // on the spot: turned as its idle faces (the captain's hip line), its middle where its idle's is (r141 PC check:
                // its middle, not its hips - Kirby hangs on his root, which the Yoshi cut-scene carries 11 m across the pitch)
                final double[] at = hips(pack, w, r.lastHost, host), mid = middle(shape.points(w));
                turnAbout(w, mid[0], mid[1], at0[2] - at[2], at0[0] - mid[0], at0[1] - mid[1]);
                // a top's slow turn standing still (spin), a long body's sway (slither)
                if (spin > 0.0) turnAbout(w, at0[0], at0[1], 2.0 * Math.PI * u, 0.0, 0.0);
                if (slither > 0.0) sway(pack, w, slither, 2.0 * Math.PI * Math.max(1, Math.round(1.2 * n / 30.0 / (2.0 * Math.PI))) * u);
                // on the floor
                final float[] p = shape.points(w);
                double low = 1e9;
                for (int i = 2; i < p.length; i += 3) low = Math.min(low, p[i]);
                double shift = low < 0.0 ? -low : 0.0;
                if (!flies && !floats && slither <= 0.0) shift = Math.max(0.0, sole(host, r.lastHost) - sole0) - low;
                if (low < 1e8 && shift != 0.0) {
                    for (double[][] m : w) m[3][2] += shift;
                    for (int i = 2; i < p.length; i += 3) p[i] = (float) (p[i] + shift);
                }
                // (r141 PC check: a Magneton's three magnets flew metres apart with no edge stretched: its reach from its
                // middle more than SPREAD times its idle's tears it too)
                if (tears(shape, p, p0, h0) || reach(p) > SPREAD * reach(p0)) segTorn++;
                segFrames.add(w);
            }
            torn = segTorn;
            return segFrames;
        }
    }

    /** Its hips' place (x, y) and turn: the guest's hips (bip01, else its pelvis, else its root) and the captain's hip line. */
    static double[] hips(Gpk pack, double[][][] w, double[][][] hw, Nl.Host host) {
        int at = -1;
        for (int want : new int[] {HB, HP})  // (not its root, which stays where the match puts it: the hips that follow)
            for (int i = 0; i < pack.nodes.length && at < 0; i++)
                if (pack.nodes[i].map == want && pack.nodes[i].parent >= 0) at = i;
        if (at < 0) at = 0;
        final int l = host.shier.indexOf(L_THIGH), r = host.shier.indexOf(R_THIGH);
        double yaw = 0.0;
        if (l >= 0 && r >= 0) yaw = StrictMath.atan2(hw[r][3][1] - hw[l][3][1], hw[r][3][0] - hw[l][3][0]);
        return new double[] {w[at][3][0], w[at][3][1], yaw};
    }

    /** Every node turned by `a` about the vertical through (cx, cy), then moved by (dx, dy). */
    static void turnAbout(double[][][] w, double cx, double cy, double a, double dx, double dy) {
        final double c = Trig.cos(a), s = Trig.sin(a);
        for (double[][] m : w) {
            for (int r = 0; r < 3; r++) {
                final double x = m[r][0], y = m[r][1];
                m[r][0] = x * c - y * s;
                m[r][1] = x * s + y * c;
            }
            final double x = m[3][0] - cx, y = m[3][1] - cy;
            m[3][0] = cx + x * c - y * s + dx;
            m[3][1] = cy + x * s + y * c + dy;
        }
    }

    /** The captain's soles: its lower foot bone's height. */
    static double sole(Nl.Host host, double[][][] hw) {
        double lo = 1e9;
        for (int id : FEET) {
            final int i = host.shier.indexOf(id);
            if (i >= 0) lo = Math.min(lo, hw[i][3][2]);
        }
        return lo < 1e8 ? lo : 0.0;
    }

    /** xr_guest.cpp slither_world standing still (moving 0): facing +X, the wave from the head down to the tail. */
    static void sway(Gpk pack, double[][][] w, double slither, double phase) {
        final int n = w.length;
        if (n < 3) return;
        int head = -1;
        double best = -1e9;
        for (int i = 0; i < n; i++) {
            if (pack.nodes[i].map == HEAD) {
                head = i;
                break;
            }
            if (w[i][3][0] > best) {
                best = w[i][3][0];
                head = i;
            }
        }
        final double hx = w[head][3][0];
        final double[] along = new double[n];
        double len = 0.0;
        for (int i = 0; i < n; i++) {
            along[i] = Math.max(0.0, hx - w[i][3][0]);
            len = Math.max(len, along[i]);
        }
        if (len < 1e-3) return;
        final double k = 2.0 * Math.PI / (0.9 * len), amp = slither * 0.07 * len * 0.35;
        for (int i = 0; i < n; i++) {
            final double u = along[i] / len;
            if (u <= 0.0) continue;
            final double a = amp * u, s = Trig.sin(k * along[i] - phase);
            final double slope = a * k * Trig.cos(k * along[i] - phase) + amp / len * s;
            final double th = -StrictMath.atan(slope);
            w[i][3][1] += a * s;  // (across: +Y, left of facing +X)
            final double c = Trig.cos(th), sn = Trig.sin(th);
            for (int r = 0; r < 3; r++) {
                final double x = w[i][r][0], y = w[i][r][1];
                w[i][r][0] = x * c - y * sn;
                w[i][r][1] = x * sn + y * c;
            }
        }
    }

    static final double SPREAD = 1.8;

    /** Its points' middle across the floor (x, y). */
    static double[] middle(float[] p) {
        double cx = 0, cy = 0;
        final int n = p.length / 3;
        for (int i = 0; i < n; i++) {
            cx += p[i * 3];
            cy += p[i * 3 + 1];
        }
        return new double[] {cx / Math.max(1, n), cy / Math.max(1, n)};
    }

    /** Its points' furthest from their middle. */
    static double reach(float[] p) {
        double cx = 0, cy = 0, cz = 0;
        final int n = p.length / 3;
        for (int i = 0; i < n; i++) {
            cx += p[i * 3];
            cy += p[i * 3 + 1];
            cz += p[i * 3 + 2];
        }
        cx /= Math.max(1, n);
        cy /= Math.max(1, n);
        cz /= Math.max(1, n);
        double r = 0;
        for (int i = 0; i < n; i++) {
            final double dx = p[i * 3] - cx, dy = p[i * 3 + 1] - cy, dz = p[i * 3 + 2] - cz;
            r = Math.max(r, dx * dx + dy * dy + dz * dz);
        }
        return Math.sqrt(r);
    }

    static double height(float[] p) {
        double lo = 1e9, hi = -1e9;
        for (int i = 2; i < p.length; i += 3) {
            lo = Math.min(lo, p[i]);
            hi = Math.max(hi, p[i]);
        }
        return Math.max(1e-4, hi - lo);
    }

    /** A tear: an edge of its drawn triangles TORN of its idle height longer than at idle (a limb pulled off its body). */
    static boolean tears(BigPose.Shape shape, float[] p, float[] p0, double h0) {
        final int[] tri = shape.tri;
        for (int t = 0; t + 2 < tri.length; t += 3)
            for (int e = 0; e < 3; e++) {
                final int a = tri[t + e], b = tri[t + (e + 1) % 3];
                if ((BigPose.dist(p, a, b) - BigPose.dist(p0, a, b)) / h0 > TORN) return true;
            }
        return false;
    }
}
