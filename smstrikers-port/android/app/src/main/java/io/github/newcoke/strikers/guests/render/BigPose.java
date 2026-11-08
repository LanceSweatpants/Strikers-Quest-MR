package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.DirSource;
import io.github.newcoke.strikers.guests.Source;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * strikers-quest r137 (Lance: the big pictures "posed mid-celebration or mid-shot", for every character: "you know you're
 * doing allllll of the players i added too right???"): the action pose of a big picture (Big.renderAction), chosen the
 * same way on every headset and on the PC from the character's own motion, as the game plays it:
 * <ul>
 * <li>a home figure (Charged guests, the Strikers captains' Charged figures, Marvel's other characters): its pose file's
 * big moves and cheers (guests/home/*.gev SEG1 kinds 2 and 5: its celebrations, its power-up),</li>
 * <li>a Marvel hero (and costume): its own celebrations (the pack's own_cel1-4 clips),</li>
 * <li>a retarget guest (Pokemon, Melee): its Strikers class's shot / kick / power-up keys through the game's retarget
 * (Retarget, number for number the game's), its class from the roster as in a match; a Strikers captain's own pack
 * (cap_*: the captain's own skeleton) on its own class's keys.</li>
 * </ul>
 * Each candidate key is put on the spot (turned as its idle faces) and measured against the idle frame: left out are keys
 * that tear a retargeted model (an edge 15% of its height longer), fold it up, turn its face or its body away, turn its
 * body over (fallen back, rolled), lean it off its feet, lay it down or rear a four-legged one up, or go too wide for a
 * cover; of the rest the one that moves most, opens widest, stands tallest and shows its face best wins (ties: the first).
 * None left: the idle frame (as before). No head bone named: the node its top hangs on stands in. The picture puts its lowest point on
 * the floor. Measured on a broad sample on the PC (tools/guestconv BigTest, logs/big137/action_all.png).
 */
final class BigPose {
    private BigPose() {}

    /** The shot / kick / power-up keys a retarget guest is tried on (those its class has), each at these times. */
    static final String[] HOST_CLIPS = {"captain_shoot_to_score", "windup_powershot", "windup_kick", "onetimer_ground_600",
        "arm_powerup"};
    static final double[] TIMES = {0.2, 0.35, 0.5, 0.65, 0.8};
    static final int HEAD = Py.lowerHash("bip01 head");
    /** Its body (the node most of it hangs on) turned this far off its idle's up (degrees): fallen back, face down, rolled. */
    static final double BODY_TILT = 40.0;
    /** The PC check: every candidate measured, printed. */
    public static volatile boolean debug = false;
    /** The PC check: the first candidate whose label has this in it, whatever it measures (null: the choice). */
    public static volatile String force = null;

    // --- what the retarget needs: the Strikers game's files (its classes' clips), the hostref, each guest's class and arms
    private static Source strikers;
    private static boolean configured, strikersTried;
    private static String hostref;
    private static final Map<String, String> classOf = new HashMap<>();
    private static Map<String, Double> arms = new HashMap<>();
    private static final Map<String, Nl.Host> hosts = new HashMap<>();

    /** The PC check: the Strikers files (Pc.strikers()), guests/roster.txt and guests/limbs.txt as text. */
    static synchronized void configure(Source s, String roster, String limbs) {
        configured = true;
        strikers = s;
        strikersTried = true;
        readRoster(roster);
        arms = Cfg.limbs(limbs);
        limbsText = limbs;
    }

    // r141 (Cel: a guest's celebration through the same files): what setup / configure found
    private static String limbsText;

    static synchronized Source strikersFiles() {
        return strikers;
    }

    static synchronized String classOf(String base) {
        return classOf.get(base);
    }

    static synchronized double armsOf(String base) {
        return arms.getOrDefault(base, 1.0);
    }

    static synchronized String limbs() {
        return limbsText;
    }

    /** The class's files (null: none here) and its hostref (null: none). */
    static Nl.Host hostOf(String cls) {
        return host(cls);
    }

    static synchronized HostRef.Ref refOf(String cls) {
        return hostref != null ? HostRef.read(hostref, cls) : null;
    }

    /**
     * The headset: STRIKERS_HOME's guests/roster.txt and limbs.txt; the game's own disc image or files folder (`data`:
     * STRIKERS_DATA, else home/files, else the first image in home), read by the converter's disc reader.
     */
    static synchronized void setup(String home, String data) {
        if (configured || home == null) return;
        configured = true;
        poses(text(new File(home, "franchise/big_poses.txt")));  // r144: the reviewed list (assets/franchise, staged)
        // r183: and the ORIGINALS (shipped in the APK, staged as guests/originals.txt)
        final String roster = text(new File(home, "guests/roster.txt")), originals = text(new File(home, "guests/originals.txt"));
        readRoster(roster == null ? originals : originals == null ? roster : roster + "\n" + originals);
        limbsText = text(new File(home, "guests/limbs.txt"));
        arms = Cfg.limbs(limbsText);
        String path = data != null && !data.isEmpty() ? data : null;
        if (path == null && new File(home, "files/common.ini").isFile()) path = new File(home, "files").getPath();
        if (path == null) {
            final File[] fs = new File(home).listFiles();
            if (fs != null) {
                Arrays.sort(fs);
                for (File f : fs) {
                    final String n = f.getName().toLowerCase(Locale.ROOT);
                    if (f.isFile() && (n.endsWith(".iso") || n.endsWith(".rvz") || n.endsWith(".wia") || n.endsWith(".gcm"))) {
                        path = f.getPath();
                        break;
                    }
                }
            }
        }
        if (path == null) return;
        try {
            if (new File(path).isDirectory()) {  // (the game's files: art/... in it, or in its files/)
                final File root = new File(path, "art").isDirectory() || !new File(path, "files").isDirectory() ? new File(path)
                        : new File(path, "files");
                strikers = new DirSource("GQSE01", root);
            } else {  // (the headset's reader, Android only: by name, so the PC check builds without it)
                strikers = (Source) Class.forName("io.github.newcoke.strikers.guests.DiscSource").getMethod("open", String.class)
                        .invoke(null, path);
            }
        } catch (Throwable e) {
            System.out.println("BigPose: the Strikers files at " + path + ": " + e);
            strikers = null;
        }
    }

    private static String text(File f) {
        try {
            return f.isFile() ? new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8) : null;
        } catch (Exception e) {
            return null;
        }
    }

    private static void readRoster(String roster) {
        classOf.clear();
        if (roster == null) return;
        for (String line : roster.split("\n")) {
            final String[] p = line.replace("\r", "").split("\t");
            if (line.startsWith("#") || p.length < 3) continue;
            final String cls = Render.CLASS.get(p[2].toLowerCase(Locale.ROOT));
            if (cls != null && !classOf.containsKey(p[0])) classOf.put(p[0], cls);
        }
    }

    private static synchronized Nl.Host host(String cls) {
        if (strikers == null || cls == null) return null;
        if (hosts.containsKey(cls)) return hosts.get(cls);
        Nl.Host h = null;
        try {
            if (hostref == null) hostref = HostRef.text(strikers);
            h = new Nl.Host(strikers, cls);
        } catch (Throwable e) {
            System.out.println("BigPose: " + cls + ": " + e);
        }
        hosts.put(cls, h);
        return h;
    }

    // --- the choice
    static final class Choice {
        double[][][] world;
        String label = "idle";
    }

    private static final class Cand {
        final String label;
        final double[][][] world;
        final boolean retargeted;  // a guest on another character's motion (its own motion is never "torn")

        Cand(String label, double[][][] world, boolean retargeted) {
            this.label = label;
            this.world = world;
            this.retargeted = retargeted;
        }
    }

    // r144 (Lance on r143's cards: "contorted as fuck or dont show their eyes ... like fix that shit come on"): what a
    // headset draws is the reviewed list, franchise/big_poses.txt (assets/franchise: "<pack>\t<its key>", every
    // character's chosen key looked at on the PC, logs/big144): a pack on it in that key, every other pack (a key that
    // failed the look, a guest made after the list) in its idle - the known-good portrait pose. The chooser below only
    // proposes keys for that review (the PC check, `propose`).
    private static Map<String, String> listed;
    /** The PC check: the chooser's own pick (to review), not the list's. */
    public static volatile boolean propose = false;

    // r144b (Lance: "dont show their eyes"): a third column "view <yaw> <pitch>" - the big picture's camera for a pack whose
    // default view (the portraits' turn -70, level) is edge-on, back-turned or hides its face; yaw = its turn about the
    // vertical (as -70), pitch = looking down on it (degrees). The 256 portraits never read this.
    private static Map<String, double[]> views;

    static synchronized void poses(String text) {
        listed = new HashMap<>();
        views = new HashMap<>();
        if (text == null) return;
        for (String line : text.split("\n")) {
            final String[] p = line.replace("\r", "").split("\t");
            if (line.startsWith("#") || p.length < 2 || p[0].isEmpty()) continue;
            listed.put(p[0], p[1]);
            if (p.length >= 3 && p[2].startsWith("view ")) {
                final String[] v = p[2].substring(5).trim().split("\\s+");
                try {
                    views.put(p[0], new double[] {Double.parseDouble(v[0]), v.length > 1 ? Double.parseDouble(v[1]) : 0});
                } catch (NumberFormatException ignored) {
                    // (a broken view: the default camera)
                }
            }
        }
    }

    static synchronized String listedKey(String base) {
        return listed != null ? listed.get(base) : null;
    }

    /** r144b: the list's camera for this pack ({yaw, pitch}), null: the default (turn -70, level). */
    static synchronized double[] view(String name) {
        final String base = name.endsWith("_shiny") ? name.substring(0, name.length() - 6) : name;
        return views != null ? views.get(base) : null;
    }

    /** name: the pack's file name without .gpk (its shiny one's too); gev: its pose file; idle: its idle frame (Big.pose). */
    static Choice choose(Gpk pack, String name, byte[] gev, double[][][] idle) {
        final Choice ch = new Choice();
        ch.world = idle;
        final String base = name.endsWith("_shiny") ? name.substring(0, name.length() - 6) : name;
        final boolean review = propose || force != null;
        final String want = review ? null : listedKey(base);
        if (!review && (want == null || want.equals("idle"))) return ch;  // (not on the list, or listed idle for its view: its idle)
        final List<Cand> cands = new ArrayList<>();
        int head = gevMoves(pack, gev, cands);
        if (cands.isEmpty()) ownClips(pack, cands);
        if (cands.isEmpty() && (pack.mode & 1) != 0) hostClips(pack, base, cands);
        if (cands.isEmpty()) return ch;
        if (!review) {  // the list's key (looked at): on the spot as the review drew it; gone from this pack: its idle
            for (Cand c : cands)
                if (c.label.equals(want)) {
                    onTheSpot(c.world, idle);
                    ch.world = c.world;
                    ch.label = c.label;
                    return ch;
                }
            return ch;
        }
        if (head < 0)
            for (int i = 0; i < pack.nodes.length && head < 0; i++)
                if (pack.nodes[i].map == HEAD) head = i;
        // its body: the node most of it hangs on (a turtle's shell, a man's chest)
        int body = -1;
        {
            final double[] wsum = new double[pack.nodes.length];
            for (Gpk.Packet pk : pack.packets)
                for (int v = 0; v < pk.nv * 4; v++)
                    if (pk.node[v] >= 0 && pk.node[v] < wsum.length) wsum[pk.node[v]] += pk.w[v];
            double most = 0;
            for (int i = 0; i < wsum.length; i++)
                if (wsum[i] > most) {
                    most = wsum[i];
                    body = i;
                }
        }
        final Shape sh = new Shape(pack);
        sh.body = body;
        // no head bone named (a Hitmonlee, a Jigglypuff): the node its top hangs on stands in for it (a ball's one bone:
        // its face is that bone's front all the same)
        if (head < 0) head = sh.topNode(idle);
        if (head < 0) {  // (nothing to tell which way it faces: its idle)
            if (debug) System.out.println("  (no head node: idle)");
            return ch;
        }
        final Measure m0 = sh.measure(idle, null, head, null, false);
        double best = -1e9;
        for (Cand c : cands) {
            onTheSpot(c.world, idle);
            final Measure m = sh.measure(c.world, m0, head, idle, c.retargeted);
            if (debug && m != null)
                System.out.println(String.format(Locale.ROOT, "  %-44s %s score %.3f %s", c.label, m.ok ? "ok " : "NO ", m.score, m.why));
            if (force != null && c.label.contains(force)) {  // (the PC check: this key, whatever it measures)
                ch.world = c.world;
                ch.label = c.label + " (forced)";
                return ch;
            }
            if (force != null || m == null || !m.ok) continue;
            if (m.score > best + 1e-9) {
                best = m.score;
                ch.world = c.world;
                ch.label = c.label + String.format(Locale.ROOT, " (score %.3f)", m.score);
            }
        }
        return ch;
    }

    /** A home figure's moves (SEG1 kinds 2 big move, 5 cheer), up to 8 keys each; its head node (-1 none). */
    static int gevMoves(Gpk pack, byte[] g, List<Cand> out) {
        if (g.length < 16) return -1;
        final ByteBuffer b = ByteBuffer.wrap(g).order(ByteOrder.LITTLE_ENDIAN);
        final int nodes = b.getInt(4), frames = b.getInt(8);
        final int at = 16 + frames * nodes * 48;
        if (nodes != pack.nodes.length || g.length < at + 12 || g[at] != 'S' || g[at + 1] != 'E' || g[at + 2] != 'G' || g[at + 3] != '1')
            return -1;
        final int count = b.getInt(at + 4), head = b.getInt(at + 8);
        for (int i = 0; i < count && i < 256 && g.length >= at + 24 + i * 12; i++) {
            final int start = b.getInt(at + 12 + i * 12), n = b.getInt(at + 16 + i * 12), kind = b.getInt(at + 20 + i * 12);
            if (n <= 0 || start < 0 || start + n > frames || (kind != 2 && kind != 5)) continue;
            final int step = Math.max(1, n / 8);
            for (int f = start + step / 2; f < start + n; f += step) out.add(new Cand("move " + i + " frame " + f, Big.frame(pack, b, f), false));
        }
        return head >= 0 && head < nodes ? head : -1;
    }

    /** A Marvel hero's own celebrations (own_cel1-4), each at TIMES (its nearest key), root where its idle's is. */
    static void ownClips(Gpk pack, List<Cand> out) {
        for (Gpk.Anim a : pack.anims) {
            if (!a.name.toLowerCase(Locale.ROOT).startsWith("own_cel") || a.keys <= 0) continue;
            for (double t : TIMES) {
                final int k = Math.min((int) (t * (a.keys - 1) + 0.5), a.keys - 1);
                out.add(new Cand(a.name + " key " + k, ownKey(pack, a, k), false));
            }
        }
    }

    /** Mesh.ownClip's chain (capsim.chain): each node's own clip key x its parent's world, the root at the origin. */
    static double[][][] ownKey(Gpk pack, Gpk.Anim a, int k) {
        final int nn = pack.nodes.length;
        final double[][][] world = new double[nn][][];
        final ByteBuffer b = ByteBuffer.wrap(pack.b).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < nn; i++) {
            final Gpk.Node nd = pack.nodes[i];
            final int o = a.at + (k * nn + i) * 28;
            final double[][] local = Py.quatRows(b.getFloat(o), b.getFloat(o + 4), b.getFloat(o + 8), b.getFloat(o + 12));
            local[3] = a.hasTrans[i] ? new double[] {b.getFloat(o + 16), b.getFloat(o + 20), b.getFloat(o + 24), 1.0}
                    : new double[] {nd.trans[0], nd.trans[1], nd.trans[2], 1.0};
            final int par = nd.parent;
            world[i] = par < 0 || par >= i ? Py.ident() : Py.mul(local, world[par]);
        }
        return world;
    }

    /** Its Strikers class's keys through the game's retarget (a Strikers captain's cap_ pack: on its own class). */
    static void hostClips(Gpk pack, String base, List<Cand> out) {
        final String cls = base.startsWith("cap_") ? Render.CLASS.get(base.substring(4)) : classOf.get(base);
        final Nl.Host h = host(cls);
        if (h == null) return;
        if (base.startsWith("cap_")) {  // a Strikers captain's own skeleton: each bone the captain's own, as the game plays him
            final Map<Integer, Integer> at = new HashMap<>();
            for (int i = 0; i < h.shier.ids.length; i++) at.put(h.shier.ids[i], i);
            for (String clip : HOST_CLIPS) {
                final Nl.Clip c = Nl.clip(h.sanim, clip);
                if (c == null) continue;
                for (double t : TIMES) {
                    final double[][][] hw = Nl.hostWorld(h.shier, c.at(t));
                    final int nn = pack.nodes.length;
                    final double[][][] w = new double[nn][][];
                    for (int i = 0; i < nn; i++) {
                        final Gpk.Node nd = pack.nodes[i];
                        final Integer k = at.get(nd.map);
                        if (k != null) w[i] = Py.copy(hw[k]);
                        else w[i] = nd.parent >= 0 && nd.parent < i ? Py.mul(nd.bindLocal, w[nd.parent]) : Py.copy(nd.bindWorld);
                    }
                    out.add(new Cand(cls + " own " + clip + " t " + t, w, false));
                }
            }
            return;
        }
        final HostRef.Ref ref = hostref != null ? HostRef.read(hostref, cls) : null;
        final double arm = arms.getOrDefault(base, 1.0);
        for (String clip : HOST_CLIPS) {
            if (Nl.clip(h.sanim, clip) == null) continue;
            try {
                final Retarget r = new Retarget(h, clip, pack, ref, arm);
                for (double t : TIMES) out.add(new Cand(cls + " " + clip + " t " + t, r.pose(t), true));
            } catch (Throwable e) {
                System.out.println("BigPose: " + base + " on " + cls + " " + clip + ": " + e);
            }
        }
    }

    /** Turned as its idle faces (its root's turn about +Z), its middle over the idle's. */
    static void onTheSpot(double[][][] w, double[][][] idle) {
        final double a = Math.atan2(w[0][0][1], w[0][0][0]) - Math.atan2(idle[0][0][1], idle[0][0][0]);
        final double dx = w[0][3][0], dy = w[0][3][1];
        final double c = Math.cos(-a), s = Math.sin(-a);
        for (double[][] m : w) {
            for (int r = 0; r < 4; r++) {
                double x = m[r][0], y = m[r][1];
                if (r == 3) {
                    x -= dx;
                    y -= dy;
                }
                m[r][0] = x * c - y * s;
                m[r][1] = x * s + y * c;
            }
            m[3][0] += idle[0][3][0];
            m[3][1] += idle[0][3][1];
        }
    }

    // --- measuring a pose
    /** An edge this share of its height longer than at idle: torn (a limb pulled off its body). */
    static final double TORN = 0.15;
    // r144: the strict checks. Its face (the head's idle forward, after the portrait's turn, toward the camera: 0.94 at
    // idle) and chest at least these; the whole body turned (yaw / pitch / roll, degrees) at most this; its silhouette
    // over its idle's (intersection over union) at least these; a joint bent off its idle at most these (degrees); its
    // body tipped at most this.
    static final double FACE_MIN = 0.8, CHEST_MIN = 0.7, TURN_MAX = 20.0, IOU_RETARGET = 0.6, IOU_OWN = 0.45,
            BEND_RETARGET = 90.0, BEND_OWN = 150.0, BODY_TILT_STRICT = 25.0;
    /** Its points' up-and-down spread over their spread round it, at least this share of its idle's. */
    static final double UPRIGHT = 0.8;

    static final class Measure {
        boolean ok;
        double score, h, wide, low, torn, headAt = 1.0, upright = 1.0, tilt = 0.0, bodyTilt = 0.0;
        double yaw, pitch, roll, iou = 1.0, bend;  // r144
        boolean[] mask;  // r144: its silhouette (the idle's, kept)
        float[] p;  // its points (on the spot)
        String why = "";
    }

    /** The pack's drawn triangles (as Mesh.build with frame 0 of a flame) and their points posed, cheaply. */
    static final class Shape {
        final Gpk pack;
        final int[] tri;
        final int nv;
        int body = -1;  // its pelvis (or spine) node, -1 none

        Shape(Gpk pack) {
            this.pack = pack;
            int n = 0, t = 0;
            for (Gpk.Packet pk : pack.packets) {
                if (pk.flameFrame() > 0) continue;
                n += pk.nv;
                t += Math.max(0, pk.strip.length - 2);
            }
            nv = n;
            final int[] tr = new int[t * 3];
            int base = 0, k = 0;
            for (Gpk.Packet pk : pack.packets) {
                if (pk.flameFrame() > 0) continue;
                final int[] s = pk.strip;
                for (int i = 0; i + 2 < s.length; i++) {
                    final int a = s[i], c = s[i + 1], d = s[i + 2];
                    if (a == c || c == d || a == d || a >= pk.nv || c >= pk.nv || d >= pk.nv) continue;
                    tr[k++] = base + a;
                    tr[k++] = base + c;
                    tr[k++] = base + d;
                }
                base += pk.nv;
            }
            tri = Arrays.copyOf(tr, k);
        }

        /**
         * r144: how far its whole body turned from p0 to p (degrees): yaw about +Z, pitch about +Y, roll about +X, each
         * the best fit of its points round their middles in that plane (a kick's leg moves a few; a body spun round, all).
         */
        double[] turns(float[] p0, float[] p) {
            double c0x = 0, c0y = 0, c0z = 0, cx = 0, cy = 0, cz = 0;
            for (int i = 0; i < nv; i++) {
                c0x += p0[i * 3];
                c0y += p0[i * 3 + 1];
                c0z += p0[i * 3 + 2];
                cx += p[i * 3];
                cy += p[i * 3 + 1];
                cz += p[i * 3 + 2];
            }
            final double n = Math.max(1, nv);
            c0x /= n;
            c0y /= n;
            c0z /= n;
            cx /= n;
            cy /= n;
            cz /= n;
            double ys = 0, yc = 0, ps = 0, pc = 0, rs = 0, rc = 0;
            for (int i = 0; i < nv; i++) {
                final double x0 = p0[i * 3] - c0x, y0 = p0[i * 3 + 1] - c0y, z0 = p0[i * 3 + 2] - c0z;
                final double x1 = p[i * 3] - cx, y1 = p[i * 3 + 1] - cy, z1 = p[i * 3 + 2] - cz;
                ys += x0 * y1 - y0 * x1;
                yc += x0 * x1 + y0 * y1;
                ps += z0 * x1 - x0 * z1;
                pc += x0 * x1 + z0 * z1;
                rs += y0 * z1 - z0 * y1;
                rc += y0 * y1 + z0 * z1;
            }
            return new double[] {Math.toDegrees(Math.atan2(ys, yc)), Math.toDegrees(Math.atan2(ps, pc)),
                Math.toDegrees(Math.atan2(rs, rc))};
        }

        /** r144: its silhouette on the picture (the portrait's turn), 48 x 64 cells over its own height and middle. */
        boolean[] mask(float[] p) {
            final int gw = 48, gh = 64;
            final double ca = Math.cos(Math.toRadians(-70)), sa = Math.sin(Math.toRadians(-70));
            final double[] u = new double[nv], v = new double[nv];
            double lo = 1e9, hi = -1e9, xl = 1e9, xh = -1e9;
            for (int i = 0; i < nv; i++) {
                u[i] = p[i * 3] * ca - p[i * 3 + 1] * sa;
                v[i] = p[i * 3 + 2];
                lo = Math.min(lo, v[i]);
                hi = Math.max(hi, v[i]);
                xl = Math.min(xl, u[i]);
                xh = Math.max(xh, u[i]);
            }
            final double h = Math.max(1e-6, hi - lo), mid = 0.5 * (xl + xh);
            for (int i = 0; i < nv; i++) {
                u[i] = (u[i] - mid) / h * (gw / 1.5) + gw / 2.0;
                v[i] = (hi - v[i]) / h * (gh / 1.05);
            }
            final boolean[] out = new boolean[gw * gh];
            for (int t = 0; t + 2 < tri.length; t += 3) {
                final int a = tri[t], b = tri[t + 1], c = tri[t + 2];
                final double ax = u[a], ay = v[a], bx = u[b], by = v[b], cx = u[c], cy = v[c];
                final double area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
                if (Math.abs(area) < 1e-9) continue;
                final int x0 = Math.max(0, (int) Math.floor(Math.min(ax, Math.min(bx, cx)))),
                        x1 = Math.min(gw - 1, (int) Math.ceil(Math.max(ax, Math.max(bx, cx))));
                final int y0 = Math.max(0, (int) Math.floor(Math.min(ay, Math.min(by, cy)))),
                        y1 = Math.min(gh - 1, (int) Math.ceil(Math.max(ay, Math.max(by, cy))));
                for (int yy = y0; yy <= y1; yy++)
                    for (int xx = x0; xx <= x1; xx++) {
                        final double px = xx + 0.5, py = yy + 0.5;
                        final double w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) / area;
                        final double w2 = ((ax - px) * (by - py) - (bx - px) * (ay - py)) / area;
                        if (w1 >= 0 && w2 >= 0 && w1 + w2 <= 1) out[yy * gw + xx] = true;
                    }
            }
            return out;
        }

        /** r144: two silhouettes' overlap (intersection over union). */
        static double overlap(boolean[] a, boolean[] b) {
            int in = 0, un = 0;
            for (int i = 0; i < a.length; i++) {
                if (a[i] && b[i]) in++;
                if (a[i] || b[i]) un++;
            }
            return un > 0 ? (double) in / un : 1.0;
        }

        /**
         * r144: the most any joint that carries a share of it (1% of its skin weight) turned against its parent, from
         * its idle to this pose (degrees): an elbow folded back, a neck twisted round, a tail wrung.
         */
        double bend(double[][][] idle, double[][][] world) {
            final int nn = pack.nodes.length;
            final double[] share = new double[nn];
            double all = 0;
            for (Gpk.Packet pk : pack.packets)
                for (int v = 0; v < pk.nv * 4; v++)
                    if (pk.node[v] >= 0 && pk.node[v] < nn) {
                        share[pk.node[v]] += pk.w[v];
                        all += pk.w[v];
                    }
            double most = 0;
            for (int i = 0; i < nn; i++) {
                final int par = pack.nodes[i].parent;
                if (par < 0 || par >= nn || all <= 0 || share[i] < 0.01 * all) continue;
                final double[][] l0 = local(idle[i], idle[par]), l1 = local(world[i], world[par]);
                // the angle of l0^T l1 (both orthonormal): acos((trace - 1) / 2)
                double tr = 0;
                for (int r = 0; r < 3; r++)
                    for (int k = 0; k < 3; k++) tr += l0[r][k] * l1[r][k];
                most = Math.max(most, Math.toDegrees(Math.acos(Math.max(-1.0, Math.min(1.0, (tr - 1) / 2)))));
            }
            return most;
        }

        /** A node's turn in its parent's (row vectors: world = local x parent), its scale left out. */
        static double[][] local(double[][] w, double[][] parent) {
            final double[][] a = unit(w), p = unit(parent);
            final double[][] out = new double[3][3];  // a x p^T
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) out[r][c] = a[r][0] * p[c][0] + a[r][1] * p[c][1] + a[r][2] * p[c][2];
            return out;
        }

        static double[][] unit(double[][] m) {
            final double[][] o = new double[3][3];
            for (int r = 0; r < 3; r++) {
                final double l = Math.sqrt(m[r][0] * m[r][0] + m[r][1] * m[r][1] + m[r][2] * m[r][2]);
                for (int c = 0; c < 3; c++) o[r][c] = l > 1e-12 ? m[r][c] / l : (r == c ? 1 : 0);
            }
            return o;
        }

        /** The node its highest points hang on most (the top 8% of its points at idle, by their heaviest weight), -1 none. */
        int topNode(double[][][] idle) {
            final float[] p = points(idle);
            if (nv == 0) return -1;
            final float[] z = new float[nv];
            for (int i = 0; i < nv; i++) z[i] = p[i * 3 + 2];
            final float[] sorted = z.clone();
            Arrays.sort(sorted);
            final float cut = sorted[Math.max(0, (int) (nv * 0.92) - 1)];
            final double[] votes = new double[pack.nodes.length];
            int d = 0;
            for (Gpk.Packet pk : pack.packets) {
                if (pk.flameFrame() > 0) continue;
                for (int v = 0; v < pk.nv; v++, d++) {
                    if (z[d] < cut) continue;
                    int bestK = -1;
                    for (int k = 0; k < 4; k++)
                        if (pk.node[v * 4 + k] < votes.length && (bestK < 0 || pk.w[v * 4 + k] > pk.w[v * 4 + bestK])) bestK = k;
                    if (bestK >= 0 && pk.w[v * 4 + bestK] > 0) votes[pk.node[v * 4 + bestK]] += 1;
                }
            }
            int top = -1;
            for (int i = 0; i < votes.length; i++)
                if (votes[i] > 0 && (top < 0 || votes[i] > votes[top])) top = i;
            return top;
        }

        float[] points(double[][][] world) {
            final int nn = pack.nodes.length;
            final double[][] sk = new double[nn][];
            for (int i = 0; i < nn; i++) sk[i] = flat(Py.mul(pack.nodes[i].inv, world[i]));
            final float[] p = new float[nv * 3];
            int d = 0;
            for (Gpk.Packet pk : pack.packets) {
                if (pk.flameFrame() > 0) continue;
                for (int v = 0; v < pk.nv; v++, d++) {
                    final double x = pk.pos[v * 3], y = pk.pos[v * 3 + 1], z = pk.pos[v * 3 + 2];
                    double px = 0, py = 0, pz = 0, tw = 0;
                    for (int k = 0; k < 4; k++) {
                        final float wg = pk.w[v * 4 + k];
                        final int nd = pk.node[v * 4 + k];
                        if (wg <= 0 || nd >= nn) continue;
                        final double[] m = sk[nd];
                        px += wg * (x * m[0] + y * m[4] + z * m[8] + m[12]);
                        py += wg * (x * m[1] + y * m[5] + z * m[9] + m[13]);
                        pz += wg * (x * m[2] + y * m[6] + z * m[10] + m[14]);
                        tw += wg;
                    }
                    if (tw <= 0) {
                        final double[] m = flat(world[0]);
                        px = x * m[0] + y * m[4] + z * m[8] + m[12];
                        py = x * m[1] + y * m[5] + z * m[9] + m[13];
                        pz = x * m[2] + y * m[6] + z * m[10] + m[14];
                        tw = 1;
                    }
                    p[d * 3] = (float) (px / tw);
                    p[d * 3 + 1] = (float) (py / tw);
                    p[d * 3 + 2] = (float) (pz / tw);
                }
            }
            return p;
        }

        /**
         * Its height, width on the picture (the portrait's turn), lowest point; against the idle (m0, its world idle):
         * how far its points moved (shape, not place), how much its triangles stretched, whether its face turned away
         * (head: its node, -1 none), the score.
         */
        Measure measure(double[][][] world, Measure m0, int head, double[][][] idle, boolean checkTorn) {
            final Measure m = new Measure();
            m.p = points(world);
            double lo = 1e9, hi = -1e9, xl = 1e9, xh = -1e9, cx = 0, cy = 0, cz = 0;
            final double ca = Math.cos(Math.toRadians(-70)), sa = Math.sin(Math.toRadians(-70));
            for (int i = 0; i < nv; i++) {
                final double x = m.p[i * 3], y = m.p[i * 3 + 1], z = m.p[i * 3 + 2];
                lo = Math.min(lo, z);
                hi = Math.max(hi, z);
                final double sx = x * ca - y * sa;
                xl = Math.min(xl, sx);
                xh = Math.max(xh, sx);
                cx += x;
                cy += y;
                cz += z;
            }
            if (nv == 0) return null;
            m.low = lo;
            m.h = Math.max(1e-4, hi - lo);
            m.wide = (xh - xl) / m.h;
            if (head >= 0) m.headAt = (world[head][3][2] - lo) / m.h;
            // how upright it stands: its points' spread up and down against their spread round it (a body lying down,
            // rolled into its shell, flung flat in a kick: much less than its idle's)
            {
                double mx = 0, my = 0, mz = 0;
                for (int i = 0; i < nv; i++) {
                    mx += m.p[i * 3];
                    my += m.p[i * 3 + 1];
                    mz += m.p[i * 3 + 2];
                }
                mx /= Math.max(1, nv);
                my /= Math.max(1, nv);
                mz /= Math.max(1, nv);
                double vz = 0, vr = 0;
                for (int i = 0; i < nv; i++) {
                    final double dx = m.p[i * 3] - mx, dy = m.p[i * 3 + 1] - my, dz = m.p[i * 3 + 2] - mz;
                    vz += dz * dz;
                    vr += dx * dx + dy * dy;
                }
                m.upright = Math.sqrt(vz / Math.max(1e-12, vr));
            }
            // how far its head leans off its feet (degrees from straight up over the middle of its lowest points)
            if (head >= 0) {
                double bx = 0, by = 0;
                int nb = 0;
                for (int i = 0; i < nv; i++)
                    if (m.p[i * 3 + 2] < lo + 0.12 * m.h) {
                        bx += m.p[i * 3];
                        by += m.p[i * 3 + 1];
                        nb++;
                    }
                if (nb > 0) {
                    bx /= nb;
                    by /= nb;
                    final double[] hp = world[head][3];
                    m.tilt = Math.toDegrees(Math.atan2(Math.hypot(hp[0] - bx, hp[1] - by), Math.max(1e-6, hp[2] - lo)));
                }
            }
            if (m0 == null) {
                m.ok = true;
                return m;
            }
            cx /= nv;
            cy /= nv;
            cz /= nv;
            double c0x = 0, c0y = 0, c0z = 0;
            for (int i = 0; i < nv; i++) {
                c0x += m0.p[i * 3];
                c0y += m0.p[i * 3 + 1];
                c0z += m0.p[i * 3 + 2];
            }
            c0x /= nv;
            c0y /= nv;
            c0z /= nv;
            double moved = 0;
            for (int i = 0; i < nv; i++) {
                final double dx = (m.p[i * 3] - cx) - (m0.p[i * 3] - c0x), dy = (m.p[i * 3 + 1] - cy) - (m0.p[i * 3 + 1] - c0y),
                        dz = (m.p[i * 3 + 2] - cz) - (m0.p[i * 3 + 2] - c0z);
                moved += Math.sqrt(dx * dx + dy * dy + dz * dz);
            }
            moved /= nv * m0.h;
            // stretched triangles (a limb pulled off its body): each edge against its idle length
            // (as lengths against its height: a short edge at a seam doubling is nothing, a limb pulled 10 cm off is)
            final int ne = tri.length;
            double worst = 0;
            int torn = 0, edges = 0;
            for (int t = 0; t < ne; t += 3)
                for (int e = 0; e < 3; e++) {
                    final int a = tri[t + e], b = tri[t + (e + 1) % 3];
                    final double grow = (dist(m.p, a, b) - dist(m0.p, a, b)) / m0.h;
                    worst = Math.max(worst, grow);
                    if (grow > TORN) torn++;
                    edges++;
                }
            final double tornShare = edges > 0 ? (double) torn / edges : 0.0;
            m.torn = worst;
            // its face: the head's forward (its idle's +X carried along) after the portrait's turn, toward the camera (-Y)
            double face = 0.6, up = 0.0, headAt = 1.0;
            if (head >= 0 && idle != null) {
                final double[][] r0 = idle[head], r1 = world[head];
                final double lx = r0[0][0], ly = r0[1][0], lz = r0[2][0];
                double fx = lx * r1[0][0] + ly * r1[1][0] + lz * r1[2][0];
                double fy = lx * r1[0][1] + ly * r1[1][1] + lz * r1[2][1];
                double fz = lx * r1[0][2] + ly * r1[1][2] + lz * r1[2][2];
                final double fl = Math.sqrt(fx * fx + fy * fy + fz * fz);
                if (fl > 1e-9) {
                    fx /= fl;
                    fy /= fl;
                    fz /= fl;
                    face = -(fx * sa + fy * ca);
                    up = fz;
                }
                headAt = (world[head][3][2] - lo) / m.h;
            }
            // its body's up (the pelvis's idle up carried along) against straight up
            double bodyTilt = 0.0, bodyFace = 1.0;
            if (body >= 0 && idle != null) {
                final double[][] r0 = idle[body], r1 = world[body];
                final double lx = r0[0][2], ly = r0[1][2], lz = r0[2][2];
                final double ux = lx * r1[0][0] + ly * r1[1][0] + lz * r1[2][0];
                final double uy = lx * r1[0][1] + ly * r1[1][1] + lz * r1[2][1];
                final double uz = lx * r1[0][2] + ly * r1[1][2] + lz * r1[2][2];
                final double ul = Math.sqrt(ux * ux + uy * uy + uz * uz);
                final double i0 = Math.sqrt(r0[0][2] * r0[0][2] + r0[1][2] * r0[1][2] + r0[2][2] * r0[2][2]);
                // (its idle up itself, against straight up: what it is turned from)
                final double w0x = lx * r0[0][0] + ly * r0[1][0] + lz * r0[2][0], w0y = lx * r0[0][1] + ly * r0[1][1] + lz * r0[2][1],
                        w0z = lx * r0[0][2] + ly * r0[1][2] + lz * r0[2][2];
                final double wl = Math.sqrt(w0x * w0x + w0y * w0y + w0z * w0z);
                if (ul > 1e-9 && wl > 1e-9 && i0 > 1e-9) {
                    final double d = (ux * w0x + uy * w0y + uz * w0z) / (ul * wl);
                    bodyTilt = Math.toDegrees(Math.acos(Math.max(-1.0, Math.min(1.0, d))));
                }
                // and its chest's forward (its idle's +X carried along) toward the camera, as the face's
                final double fx0 = r0[0][0], fy0 = r0[1][0], fz0 = r0[2][0];
                double fx = fx0 * r1[0][0] + fy0 * r1[1][0] + fz0 * r1[2][0];
                double fy = fx0 * r1[0][1] + fy0 * r1[1][1] + fz0 * r1[2][1];
                double fz = fx0 * r1[0][2] + fy0 * r1[1][2] + fz0 * r1[2][2];
                final double fl = Math.sqrt(fx * fx + fy * fy + fz * fz);
                if (fl > 1e-9) bodyFace = -(fx / fl * sa + fy / fl * ca);
            }
            m.bodyTilt = bodyTilt;
            // r144 (Lance: "contorted as fuck or dont show their eyes ... arms or legs being bent all crazy"): the whole
            // body's turn against its idle (its points fitted round their middles: a Togepi spun side-on, a Porygon2
            // turned away), its silhouette against its idle's, the most any of its joints bent against its idle
            final double[] turn = turns(m0.p, m.p);
            m.yaw = turn[0];
            m.pitch = turn[1];
            m.roll = turn[2];
            if (m0.mask == null) m0.mask = mask(m0.p);
            m.iou = overlap(m0.mask, mask(m.p));
            m.bend = bend(idle, world);
            final double wide0 = m0.wide;
            if (checkTorn && worst > TORN) m.why = String.format(Locale.ROOT, "torn (an edge %.2f of its height longer, %d edges)", worst, torn);
            else if (m.h < 0.6 * m0.h) m.why = String.format(Locale.ROOT, "folded up (%.2f of its height)", m.h / m0.h);
            else if (face < 0.25 || up < -0.45) m.why = String.format(Locale.ROOT, "face away (%.2f, %.2f)", face, up);
            else if (head >= 0 && headAt < 0.75 * m0.headAt) m.why = String.format(Locale.ROOT, "head low (%.2f, idle %.2f)", headAt, m0.headAt);
            else if (m.wide > Math.max(1.35, 1.2 * wide0)) m.why = String.format(Locale.ROOT, "too wide (%.2f)", m.wide);
            else if (m.upright < UPRIGHT * m0.upright)
                m.why = String.format(Locale.ROOT, "lying / tilted (%.2f, idle %.2f)", m.upright, m0.upright);
            else if (bodyTilt > BODY_TILT) m.why = String.format(Locale.ROOT, "body turned over (%.0f degrees)", bodyTilt);
            else if (bodyFace < 0.2) m.why = String.format(Locale.ROOT, "body turned away (%.2f)", bodyFace);
            else if (m.tilt > m0.tilt + 30.0) m.why = String.format(Locale.ROOT, "leaning off its feet (%.0f, idle %.0f degrees)", m.tilt, m0.tilt);
            else if (m0.upright < 0.75 && m.upright > 1.3 * m0.upright)  // (a four-legged one rearing up on a biped's kick)
                m.why = String.format(Locale.ROOT, "reared up (%.2f, idle %.2f)", m.upright, m0.upright);
            // r144: the strict checks (measured on the ones Lance hit: Togepi, Milotic, Porygon2, Hitmonchan ...)
            if (m.why.isEmpty()) {
                if (face < FACE_MIN) m.why = String.format(Locale.ROOT, "face not to the camera (%.2f)", face);
                else if (bodyFace < CHEST_MIN) m.why = String.format(Locale.ROOT, "chest turned (%.2f)", bodyFace);
                else if (Math.abs(m.yaw) > TURN_MAX || Math.abs(m.pitch) > TURN_MAX || Math.abs(m.roll) > TURN_MAX)
                    m.why = String.format(Locale.ROOT, "body turned (yaw %.0f, pitch %.0f, roll %.0f)", m.yaw, m.pitch, m.roll);
                else if (m.iou < (checkTorn ? IOU_RETARGET : IOU_OWN)) m.why = String.format(Locale.ROOT, "not its shape (%.2f)", m.iou);
                else if (m.bend > (checkTorn ? BEND_RETARGET : BEND_OWN)) m.why = String.format(Locale.ROOT, "a joint bent %.0f degrees", m.bend);
                else if (bodyTilt > BODY_TILT_STRICT) m.why = String.format(Locale.ROOT, "body tipped (%.0f degrees)", bodyTilt);
            }
            m.ok = m.why.isEmpty();
            m.score = Math.min(moved, 0.3) + 0.25 * Math.min(m.wide / Math.max(0.05, wide0), 1.4)
                    + 0.2 * Math.min(m.upright / Math.max(1e-3, m0.upright), 1.3) + 0.3 * face;
            if (debug) m.why += String.format(Locale.ROOT, " [torn %.3f x%d (%.4f), moved %.3f, wide %.2f/%.2f, h %.2f, face %.2f up %.2f head %.2f, upright %.2f/%.2f, tilt %.0f/%.0f, body %.0f, chest %.2f, turn %.0f/%.0f/%.0f, iou %.2f, bend %.0f]",
                    worst, torn, tornShare, moved, m.wide, wide0, m.h / m0.h, face, up, headAt, m.upright, m0.upright, m.tilt, m0.tilt,
                    bodyTilt, bodyFace, m.yaw, m.pitch, m.roll, m.iou, m.bend);
            return m;
        }
    }

    static double dist(float[] p, int a, int b) {
        final double dx = p[a * 3] - p[b * 3], dy = p[a * 3 + 1] - p[b * 3 + 1], dz = p[a * 3 + 2] - p[b * 3 + 2];
        return Math.sqrt(dx * dx + dy * dy + dz * dz);
    }

    static double[] flat(double[][] m) {
        final double[] f = new double[16];
        for (int i = 0; i < 16; i++) f[i] = m[i / 4][i % 4];
        return f;
    }
}
