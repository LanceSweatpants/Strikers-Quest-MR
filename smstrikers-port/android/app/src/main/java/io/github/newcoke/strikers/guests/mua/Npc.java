package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.Data;
import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * strikers-quest r135: Ultimate Alliance's NON-PLAYABLE characters as guests (Nightcrawler, Jean Grey, Professor X, The
 * Vision, Dr. Doom, Mysterio, Lizard, Rhino, Winter Soldier), made from the player's own disc as the PC tools made the
 * ones on Lance's headset (tools/outside/mua: npc_fb.py, mua2gpk.py's NPC flags, npc_cels.py, npc_home.py).
 * <p>An NPC has no packages/generated/characters/&lt;hero&gt;_&lt;n&gt;.fb: its skin and animation set sit in the map /
 * simulator packages that use it. What to take comes from the disc's own data/npcstat.engb (the entry's skin,
 * characteranims, BoltOn model / anim / bone); our tuning (guestdata/mua/npcs.txt, Hero blocks + the keys below) only
 * names the entry and the packages to read. The pieces are put together in the heroes' .fb layout, in npc_fb.py's
 * order (skin, bolt-on model, bolt-on set, the shared sets, its own set last), so HeroPack reads it as a costume.
 * <pre>
 * npc Mephisto_Nightcrawler                 its npcstat.engb entry
 * package packages/generated/maps/...       the package its skin / own set are read from
 * sets actors/fightstyle_villain.igb=packages/... ...   shared sets its moves also come from, and where
 * bolton actors/4710.igb,actors/47_nightcrawler_tail.igb,Bip01 Pelvis   (mua2gpk --bolton; checked against npcstat)
 * rest idle | run                           mua2gpk --rest-bind (--rest-clip)
 * unmap bip01,bip01 pelvis,...              mua2gpk --unmap
 * float 1                                   mua2gpk --float (pack mode 5)
 * reparent Bip01 Tail:Bip01 Pelvis          mua2gpk --reparent
 * </pre>
 * One look each (costumes = its skin), cel / home lines as npc_cels.py / npc_home.py take them (a piece "clip@upper":
 * its upper body only).
 */
public final class Npc {
    private Npc() {}

    static final String NPCSTAT = "data/npcstat.engb";

    public static boolean is(Hero h) {
        return h.more.containsKey("npc");
    }

    /**
     * guestdata/mua/npcs.txt's characters (none without the file); their math-correction lines (libm_npc.txt) go on top
     * of the heroes' table (call after Mua.heroes, which loads that one).
     */
    public static List<Hero> npcs(Data data) throws IOException {
        if (!data.has("mua/npcs.txt")) return new ArrayList<>();
        if (data.has("mua/libm_npc.txt")) Libm.add(data.text("mua/libm_npc.txt"));
        return Hero.parse(data.text("mua/npcs.txt"));
    }

    // ------------------------------------------------------------------------------------------------ npcstat
    /** engb.py: MUA's binary XML (u32 name, next sibling, first child, attribute count, then key / value pairs). */
    static final class Node {
        final String name;
        final Map<String, String> attrs = new LinkedHashMap<>();
        final List<Node> kids = new ArrayList<>();

        Node(String name) {
            this.name = name;
        }
    }

    static List<Node> engb(byte[] b) {
        return nodes(b, 8);
    }

    private static List<Node> nodes(byte[] b, int o) {
        final List<Node> out = new ArrayList<>();
        while (o != -1) {
            final Node n = new Node(cstr(b, Wad.le32(b, o)));
            final int next = Wad.le32(b, o + 4), child = Wad.le32(b, o + 8), na = Wad.le32(b, o + 12);
            for (int i = 0; i < na; i++) n.attrs.put(cstr(b, Wad.le32(b, o + 16 + 8 * i)), cstr(b, Wad.le32(b, o + 20 + 8 * i)));
            if (child != -1) n.kids.addAll(nodes(b, child));
            out.add(n);
            o = next;
        }
        return out;
    }

    private static String cstr(byte[] b, int o) {
        int e = o;
        while (b[e] != 0) e++;
        return new String(b, o, e - o, StandardCharsets.ISO_8859_1);
    }

    /** npcstat's characters by their name attribute (the first of a name). */
    static Map<String, Node> npcstat(byte[] b) {
        final Map<String, Node> out = new LinkedHashMap<>();
        for (Node top : engb(b))
            for (Node st : top.kids) {
                final String n = st.attrs.get("name");
                if (n != null && !out.containsKey(n)) out.put(n, st);
            }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ its .fb
    /** The packages read during one run (strange1.fb holds three of them), dropped by done(). */
    private static final Map<String, Wad.Fb> PACKAGES = new ConcurrentHashMap<>();
    private static volatile Map<String, Node> stats;

    static void done() {
        PACKAGES.clear();
        stats = null;
    }

    private static Wad.Fb pkg(Wad wad, String path) throws IOException {
        Wad.Fb f = PACKAGES.get(path);
        if (f == null) {
            f = new Wad.Fb(wad.read(path));
            PACKAGES.put(path, f);
        }
        return f;
    }

    /** npc_fb.py: the NPC's pieces in the heroes' .fb layout. */
    public static Wad.Fb fb(Wad wad, Hero h) throws IOException {
        Map<String, Node> st = stats;
        if (st == null) {
            st = npcstat(wad.read(NPCSTAT));
            stats = st;
        }
        final Node e = st.get(h.more.get("npc"));
        if (e == null) throw new IOException(h.hero + ": no npcstat entry " + h.more.get("npc"));
        final String skin = "actors/" + e.attrs.get("skin") + ".igb";
        final String main = "actors/" + e.attrs.get("characteranims").toLowerCase(Locale.ROOT) + ".igb";
        String[] bolt = null;
        for (Node k : e.kids) {
            if (!k.name.equals("BoltOn")) continue;
            bolt = new String[] {"actors/" + k.attrs.get("model") + ".igb",
                "actors/" + k.attrs.get("anim").toLowerCase(Locale.ROOT) + ".igb", k.attrs.get("bolt")};
        }
        final String want = h.more.get("bolton");
        if (want != null) {
            final String[] w = want.split(",");
            if (bolt == null || !w[0].equalsIgnoreCase(bolt[0]) || !w[1].equalsIgnoreCase(bolt[1]) || !w[2].equalsIgnoreCase(bolt[2]))
                throw new IOException(h.hero + ": npcstat's bolt-on differs from " + want);
        }
        if (!skin.equals("actors/" + h.costumes[0] + ".igb")) throw new IOException(h.hero + ": npcstat skin " + skin);
        final String home = h.more.get("package");
        final List<String[]> order = new ArrayList<>();  // {entry, package}
        order.add(new String[] {skin, home});
        if (bolt != null) {
            order.add(new String[] {bolt[0], home});
            order.add(new String[] {bolt[1], home});
        }
        final String sets = h.more.get("sets");
        if (sets != null)
            for (String kv : sets.trim().split("\\s+")) {
                final int eq = kv.indexOf('=');
                order.add(new String[] {kv.substring(0, eq), kv.substring(eq + 1)});
            }
        order.add(new String[] {main, home});
        final Out o = new Out();
        for (String[] ep : order) {
            final Wad.Fb f = pkg(wad, ep[1]);
            final byte[] d = f.data(ep[0]);
            if (d == null) throw new IOException(h.hero + ": " + ep[0] + " not in " + ep[1]);
            o.fixed(ep[0], 128).fixed(f.type(ep[0]), 64).le32(d.length).bytes(d);
        }
        return new Wad.Fb(o.toBytes());
    }

    // ------------------------------------------------------------------------------------------------ the bolt-on
    /** mua2gpk.bolton: a bolt-on actor's skeleton hung on one of the skin's bones, its parts on those nodes. */
    static final class Bolt {
        String[] names;
        int[] parents;
        double[][][] worlds, rests;  // MUA model space
        List<IgbModel.Part> parts = new ArrayList<>();
        Igb model, anims;
        IgbModel.Skel sk;
    }

    /** The NPC's bolt-on (npcs.txt "bolton"), or null (every hero). */
    static Bolt bolt(Hero h, HeroPack.Costume c) throws IOException {
        final String spec = h.more.get("bolton");
        if (spec == null) return null;
        final String[] f = spec.split(",");
        final Bolt b = new Bolt();
        b.model = new Igb(c.fb.data(f[0]));
        b.sk = IgbModel.skeleton(b.model);
        int bi = -1;
        for (int i = 0; i < c.sk.names.length && bi < 0; i++)
            if (c.sk.names[i].toLowerCase(Locale.ROOT).equals(f[2].toLowerCase(Locale.ROOT))) bi = i;
        if (bi < 0) throw new IOException(h.hero + ": no bone " + f[2]);
        final double[][] wb = c.sk.world[bi];
        final double[][] rot = Np.rot(wb);
        final int n0 = c.sk.names.length;
        String tag = f[0].substring(f[0].lastIndexOf('/') + 1);
        tag = tag.substring(0, tag.lastIndexOf('.'));
        for (IgbModel.Part p : IgbModel.skins(b.model, b.sk)) {
            final IgbModel.Part q = p;  // (its own: skins() makes new parts each call)
            final double[][] P = new double[p.pos.length][];
            for (int k = 0; k < P.length; k++) {
                final double[] v = Np.vm(p.pos[k], rot);
                P[k] = new double[] {v[0] + wb[3][0], v[1] + wb[3][1], v[2] + wb[3][2]};
            }
            q.pos = P;
            if (p.nrm != null) {
                final double[][] N = new double[p.nrm.length][];
                for (int k = 0; k < N.length; k++) N[k] = Np.vm(p.nrm[k], rot);
                q.nrm = N;
            }
            if (p.joints != null) {
                final int[][] J = new int[p.joints.length][];
                for (int k = 0; k < J.length; k++) {
                    J[k] = p.joints[k].clone();
                    for (int x = 0; x < J[k].length; x++) if (J[k][x] >= 0) J[k][x] += n0;
                }
                q.joints = J;
            }
            b.parts.add(q);
        }
        final int n = b.sk.names.length;
        b.names = new String[n];
        b.parents = new int[n];
        b.worlds = new double[n][][];
        b.rests = new double[n][][];
        b.anims = new Igb(c.fb.data(f[1]));
        final IgbAnim.Clip idle = IgbAnim.clip(b.anims, "idle");
        final double[][][] rest = idle != null ? IgbAnim.poseWorld(b.sk, idle.tracks, 0.0) : b.sk.world;
        for (int j = 0; j < n; j++) {
            b.names[j] = tag + "/" + b.sk.names[j];
            b.parents[j] = b.sk.parents[j] < 0 ? bi : n0 + b.sk.parents[j];
            b.worlds[j] = Np.mm(b.sk.world[j], wb);
            b.rests[j] = Np.mm(rest[j], wb);
        }
        return b;
    }

    /** mua2gpk.own_idle_tracks: a clip of its own set (else its combat set), or null. */
    static IgbAnim.Clip ownClip(HeroPack.Costume c, String name) throws IOException {
        IgbAnim.Clip cl = c.main != null ? IgbAnim.clip(c.main, name) : null;
        if (cl == null && c.combat != null) cl = IgbAnim.clip(c.combat, name);
        return cl;
    }

    /** The bind its pack was made with (npcs.txt "rest": its own clip's first key), else the skeleton's. */
    static double[][][] rest(Hero h, HeroPack.Costume c) throws IOException {
        final String r = h.more.get("rest");
        if (r == null) return c.sk.world;
        final IgbAnim.Clip cl = ownClip(c, r);
        if (cl == null) throw new IOException(h.hero + ": no " + r + " for its rest pose");
        return IgbAnim.poseWorld(c.sk, cl.tracks, 0.0);
    }

    // ------------------------------------------------------------------------------------------------ its moves
    /** npc_cels.sets_of: its own set, the .fb's other sets (file order, the bolt-on's left out), MUA's shared set. */
    static List<Igb> sets(Hero h, HeroPack.Costume c, Igb shared) throws IOException {
        final List<Igb> out = new ArrayList<>();
        final String spec = h.more.get("bolton");
        final String[] bolt = spec != null ? spec.split(",") : new String[0];
        String mainName = null;
        for (String n : c.fb.names()) if ("actoranimdb".equals(c.fb.type(n)) && !isBolt(n, bolt) && HeroPack.MAIN_SET.matcher(n).matches()) mainName = n;
        if (mainName != null) out.add(new Igb(c.fb.data(mainName)));
        for (String n : c.fb.names()) {
            if (!"actoranimdb".equals(c.fb.type(n)) || isBolt(n, bolt) || n.equals(mainName)) continue;
            out.add(new Igb(c.fb.data(n)));
        }
        if (shared != null) out.add(shared);
        return out;
    }

    private static boolean isBolt(String n, String[] bolt) {
        return bolt.length >= 2 && (n.equals(bolt[0]) || n.equals(bolt[1]));
    }

    /** npc_cels.find_clip: the first set that has it. */
    static IgbAnim.Clip find(List<Igb> sets, String name) throws IOException {
        for (Igb ag : sets) {
            final IgbAnim.Clip cl = IgbAnim.clip(ag, name);
            if (cl != null) return cl;
        }
        return null;
    }

    /** npc_cels.move_tracks: "clip" or "clip@upper"; a move from a set not its own leaves Bip01 Ponytail* to its idle. */
    static IgbAnim.Clip move(List<Igb> sets, IgbModel.Skel sk, String piece) throws IOException {
        final int at = piece.indexOf('@');
        final String name = at < 0 ? piece : piece.substring(0, at), part = at < 0 ? "" : piece.substring(at + 1);
        for (int k = 0; k < sets.size(); k++) {
            final IgbAnim.Clip cl = IgbAnim.clip(sets.get(k), name);
            if (cl == null) continue;
            Map<String, IgbAnim.Track> tr = cl.tracks;
            if (k > 0) {
                final Map<String, IgbAnim.Track> f = new LinkedHashMap<>();
                for (Map.Entry<String, IgbAnim.Track> e : tr.entrySet())
                    if (!e.getKey().toLowerCase(Locale.ROOT).startsWith("bip01 ponytail")) f.put(e.getKey(), e.getValue());
                tr = f;
            }
            if (part.equals("upper")) {
                final java.util.Set<String> up = subtree(sk, "Bip01 Spine1");
                final Map<String, IgbAnim.Track> f = new LinkedHashMap<>();
                for (Map.Entry<String, IgbAnim.Track> e : tr.entrySet()) if (up.contains(e.getKey())) f.put(e.getKey(), e.getValue());
                tr = f;
            }
            return new IgbAnim.Clip(tr, cl.dur);
        }
        return null;
    }

    static java.util.Set<String> subtree(IgbModel.Skel sk, String bone) {
        final int i0 = sk.index(bone);
        final java.util.Set<String> out = new java.util.HashSet<>();
        for (int i = 0; i < sk.names.length; i++) {
            int k = i;
            while (k >= 0 && k != i0) k = sk.parents[k];
            if (k == i0) out.add(sk.names[i]);
        }
        return out;
    }

    /** npc_cels.pose_world_fill: igb.pose_world, a bone with no track in the move taking its own idle's at time ft. */
    static double[][][] poseFill(IgbModel.Skel sk, Map<String, IgbAnim.Track> tracks, double t, Map<String, IgbAnim.Track> fill, double ft) {
        final int n = sk.names.length;
        final double[][][] bl = IgbAnim.bindLocal(sk);
        final double[][][] out = new double[n][][];
        for (int i = 0; i < n; i++) {
            final int p = sk.parents[i];
            final double[][] pw = p >= 0 && p < i ? out[p] : null;
            IgbAnim.Track trk = tracks.get(sk.names[i]);
            double tt = t;
            if (trk == null && fill != null) {
                trk = fill.get(sk.names[i]);
                tt = ft;
            }
            double[][] local;
            if (trk == null) {
                local = bl[i];
            } else {
                final double[][] s = IgbAnim.sample(trk, tt);
                local = Np.eye4();
                final double[][] r = IgbAnim.quatRows(s[0]);
                for (int a = 0; a < 3; a++) System.arraycopy(r[a], 0, local[a], 0, 3);
                if (trk.moves) System.arraycopy(s[1], 0, local[3], 0, 3);
                else System.arraycopy(bl[i][3], 0, local[3], 0, 3);
            }
            out[i] = Np.mm(local, pw != null ? pw : Np.eye4());
        }
        return out;
    }

    /** npc_cels.bolt_rt: the bolt-on's nodes at time t of its own idle (looped), local to their parents (moves x s). */
    static void boltLocals(Bolt b, IgbAnim.Clip idle, double t, double s, double[][][] R, double[][] T, int n0) {
        final double dur = idle != null ? Math.max(idle.dur, 1e-3) : 0.0;
        final double[][][] wp = idle != null ? IgbAnim.poseWorld(b.sk, idle.tracks, dur > 0 ? pyMod(t, dur) : 0.0) : b.sk.world;
        for (int j = 0; j < b.sk.names.length; j++) {
            final int par = b.sk.parents[j];
            final double[][] L = par < 0 ? wp[j] : Np.mm(wp[j], Np.inv(wp[par]));
            R[n0 + j] = Np.rot(L);
            T[n0 + j] = new double[] {L[3][0] * s, L[3][1] * s, L[3][2] * s};
        }
    }

    /** Python's float % (positive divisor). */
    static double pyMod(double x, double m) {
        double r = x % m;
        if (r != 0.0 && (r < 0) != (m < 0)) r += m;
        return r;
    }

    // ------------------------------------------------------------------------------------------------ a costume
    /** Its files: {match pack (mua2gpk + npc_cels), home/.gpk, home/.gev, evo/.gev or null} (PC checks too). */
    public static byte[][] files(Wad wad, Hero h, Igb shared) throws IOException {
        final HeroPack.Costume cos = HeroPack.open(fb(wad, h), h.costumes[0]);
        final byte[] gpk = NpcCels.add(h, cos, HeroPack.base(h, 1, cos, shared), shared);
        final byte[][] home = NpcHome.build(h, cos, gpk, shared);
        return new byte[][] {gpk, home[0], home[1], EvoPose.gev(gpk)};
    }

    /** Mua.costume for an NPC: its match pack, standing pose, home figure, stats. */
    static void costume(Job job, Wad wad, Hero h, Igb shared) throws IOException {
        final String pack = h.pack;
        final byte[][] f = files(wad, h, shared);
        final byte[] gpk = f[0], gev = f[3];
        Out.save(job.out("home/" + pack + ".gpk"), f[1]);
        Out.save(job.out("home/" + pack + ".gev"), f[2]);
        if (gev != null) Out.save(job.out("evo/" + pack + ".gev"), gev);
        final String stats = "mua/stats/" + pack + ".ini";
        if (job.data.has(stats)) Out.save(job.out("stats/" + pack + ".ini"), job.data.read(stats));
        Out.save(job.out(pack + ".gpk"), gpk);  // (last: a pack there = the rest is there too)
    }
}
