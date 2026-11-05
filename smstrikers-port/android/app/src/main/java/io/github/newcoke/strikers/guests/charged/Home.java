package io.github.newcoke.strikers.guests.charged;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Data;
import io.github.newcoke.strikers.guests.GxTex;
import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * r118 / r119 / r129: the home-corner figures of the 8 Strikers captains and the Charged guests, life size, doing their
 * own things - from the player's own Mario Strikers Charged disc (tools/outside/charged_home.py, the same bytes):
 * <pre>
 *   guests/home/&lt;pack&gt;.gpk  GPK2 (xr_evo): the character's Charged model (GuestPack's GPK1, its pictures decoded to
 *                          RGBA8 records, normals turned in as GPK2 keeps them, each node's bind = inverse of its
 *                          inverse bind), no clips
 *   guests/home/&lt;pack&gt;.gev  GEV1: world matrices per node, 30 a second, on the spot, facing +X, feet on 0: its menu
 *                          idle, greetings (wave, menu action), big moves (its cut-scenes' on-the-spot shots: goal
 *                          celebrations, captain intros, cup win; arm power-up), reaction, goodbye, cheer (r129: goal
 *                          celebrations, else cup win) and upset ("defeated" / "outraged"); then "SEG1": move table
 *                          (first frame, frames, kind 0-6) + the head node
 * </pre>
 * Who, how tall, which parts are left off and which cut-scenes: our data, guestdata/charged/home.txt. Disc paths:
 * Art/characters/&lt;n&gt;/..., Art/animation/&lt;n&gt;.shier / .sanim.zlib / &lt;n&gt;fe.sanim, Art/nis/&lt;n&gt;_&lt;cut-scene&gt;.nis.
 *
 * Numbers: the Python tool's numpy doubles in its order of operations; its matrix / dot products as numpy does them on
 * the PC (OpenBLAS: fused multiply-add chains in index order; einsum: plain sums from 0; np.linalg.inv: OpenBLAS's
 * left-looking LU + reciprocal-diagonal solves), rounded to float only where it packs. sin / cos / atan2: StrictMath,
 * except the arguments where the Windows CRT the tool ran on answers a last bit differently (our table,
 * guestdata/charged/home_trig.txt; without it 6 of the 12 loops differ in a few hundred near-zero floats). Checked on
 * the PC (tools/guestconv/test/HomeTest.java): all 24 files byte-identical to the headset's.
 */
public final class Home {
    private Home() {}

    public static final String DATA = "charged/home.txt";
    static final double KEYS_PER_S = 30.0, FPS = 30.0;
    static final int[] ENGINE_FMT = {4, 5, 14, 6, 1, 0, 1, 3, 9};  // Charged's texture format -> GX (0..8)

    /** One figure: the Charged character's name, the home pack it becomes, its life-size height, parts left off. */
    public static final class Cast {
        public final String name, pack;
        public final double height;
        final Set<Integer> hide;
        /** r141: more big moves from its in-game clips (home.txt "moves": a Charged sidekick's menu action, its deke). */
        final List<String> moves = new ArrayList<>();
        /** r141: its standing loop's clip when not its menu idle (home.txt "idle": Dry Bones' in-game idle). */
        String idleClip = null;
        /** r141: no cheer from its goal-celebration cut-scenes, their shots after its own moves (home.txt "nocel": they tumble
         * Monty Mole into a jumbled ball - a BLACK 1/1's throne loops its first big move: its power-up). */
        boolean noCel = false;

        Cast(String name, String pack, double height, Set<Integer> hide) {
            this.name = name;
            this.pack = pack;
            this.height = height;
            this.hide = hide;
        }
    }

    public static final class Setup {
        public final List<Cast> cast;
        final List<String> keep;  // cut-scene name starts kept (NIS_KEEP)
        final int tries;          // cut-scenes looked for per kept start: _0 .. _(tries-1)
        public final Trig trig;

        Setup(List<Cast> cast, List<String> keep, int tries, Trig trig) {
            this.cast = Collections.unmodifiableList(cast);
            this.keep = Collections.unmodifiableList(keep);
            this.tries = tries;
            this.trig = trig;
        }

        /** The same with other sin / cos / atan2 (the PC checks: one that logs what's asked). */
        public Setup with(Trig t) {
            return new Setup(cast, keep, tries, t);
        }
    }

    /**
     * sin / cos / atan2 as the Python tool got them from the Windows CRT: StrictMath (fdlibm, the same on every Java),
     * except where our table (guestdata/charged/home_trig.txt, made by tools/guestconv/test/HomeTrig.java) has the
     * CRT's answer for an argument StrictMath answers a last bit differently.
     */
    public static class Trig {
        private final HashMap<Long, Long> sin = new HashMap<>(), cos = new HashMap<>();
        private final HashMap<List<Long>, Long> atan2 = new HashMap<>();

        public Trig() {}

        public Trig(String table) throws IOException {
            for (String line : table.split("\n")) {
                line = line.replace("\r", "").trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                final String[] p = line.split(" ");
                try {
                    if (p[0].equals("s") && p.length == 3) sin.put(hex(p[1]), hex(p[2]));
                    else if (p[0].equals("c") && p.length == 3) cos.put(hex(p[1]), hex(p[2]));
                    else if (p[0].equals("a") && p.length == 4) atan2.put(java.util.Arrays.asList(hex(p[1]), hex(p[2])), hex(p[3]));
                    else throw new NumberFormatException();
                } catch (NumberFormatException e) {
                    throw new IOException("home_trig.txt: can't read \"" + line + "\"");
                }
            }
        }

        private static long hex(String s) {
            return Long.parseUnsignedLong(s, 16);
        }

        public double sin(double x) {
            final Long r = sin.get(Double.doubleToRawLongBits(x));
            return r != null ? Double.longBitsToDouble(r) : StrictMath.sin(x);
        }

        public double cos(double x) {
            final Long r = cos.get(Double.doubleToRawLongBits(x));
            return r != null ? Double.longBitsToDouble(r) : StrictMath.cos(x);
        }

        public double atan2(double y, double x) {
            final Long r = atan2.isEmpty() ? null
                    : atan2.get(java.util.Arrays.asList(Double.doubleToRawLongBits(y), Double.doubleToRawLongBits(x)));
            return r != null ? Double.longBitsToDouble(r) : StrictMath.atan2(y, x);
        }
    }

    public static final String TRIG = "charged/home_trig.txt";

    public static Setup setup(Data data) throws IOException {
        final ArrayList<Cast> cast = new ArrayList<>();
        final ArrayList<String> keep = new ArrayList<>();
        int tries = 10;
        for (String line : data.text(DATA).split("\n")) {
            line = line.replace("\r", "");
            if (line.trim().isEmpty() || line.startsWith("#")) continue;
            final String[] c = line.split("\t");
            if (c[0].equals("home") && c.length >= 4) {
                final HashSet<Integer> hide = new HashSet<>();
                if (c.length >= 5 && !c[4].isEmpty())
                    for (String h : c[4].split(",")) hide.add(Integer.parseInt(h.trim()));
                cast.add(new Cast(c[1], c[2], Double.parseDouble(c[3]), hide));
            } else if (c[0].equals("keep") && c.length >= 2) {
                keep.add(c[1]);
            } else if (c[0].equals("tries") && c.length >= 2) {
                tries = Integer.parseInt(c[1]);
            } else if (c[0].equals("idle") && c.length >= 3) {  // r141
                for (Cast k : cast)
                    if (k.name.equals(c[1])) k.idleClip = c[2].trim();
            } else if (c[0].equals("moves") && c.length >= 3) {  // r141
                for (Cast k : cast)
                    if (k.name.equals(c[1]))
                        for (String m : c[2].split(",")) k.moves.add(m.trim());
            } else if (c[0].equals("nocel") && c.length >= 2) {  // r141
                for (Cast k : cast)
                    if (k.name.equals(c[1].trim())) k.noCel = true;
            } else {
                throw new IOException(DATA + ": can't read \"" + line + "\"");
            }
        }
        return new Setup(cast, keep, tries, data.has(TRIG) ? new Trig(data.text(TRIG)) : new Trig());
    }

    /** Every figure into job.guests/home (nothing without a Charged disc). */
    public static void run(Job job) {
        if (job.charged == null) return;
        final Setup s;
        final Set<String> skip;
        try {
            s = setup(job.data);
            skip = Charged.setup(job.data).skipAnims;
        } catch (IOException e) {
            job.progress.warn("Home figures: " + e.getMessage());
            return;
        }
        final int total = s.cast.size();
        int done = 0;
        for (Cast c : s.cast) {
            job.progress.step("Home figure: " + c.name, done, total);
            try {
                final byte[][] files = make(job.charged, c, s, skip);
                Out.save(job.out("home/" + c.pack + ".gpk"), files[0]);
                Out.save(job.out("home/" + c.pack + ".gev"), files[1]);
            } catch (Exception | OutOfMemoryError e) {
                job.progress.warn("Home figure: " + c.name + " left out (" + (e.getMessage() != null ? e.getMessage()
                        : e.getClass().getSimpleName()) + ")");
            }
            done++;
        }
        job.progress.step("Home figures: done", total, total);
    }

    /** One figure: {its .gpk, its .gev}. */
    public static byte[][] make(Source charged, Cast c, Setup s, Set<String> skip) throws IOException {
        final Pack p = Pack.read(GuestPack.build(charged, c.name, skip).toBytes());
        final LinkedHashMap<String, Clip> clips = allClips(p, c.name, charged, s);
        return build(c, p, clips, s.trig);
    }

    // ------------------------------------------------------------------ the GPK1 pack, read (read_gpk1)
    static final class Node {
        long id;
        int par;
        double[] trans = new double[3];
        boolean hasInv;
        double[] inv = new double[16];
    }

    static final class Packet {
        int th, nv, ni;
        byte[] body, strip;
    }

    /** A clip: keys x nodes x (q x y z w, t x y z), flat; hasT per node. */
    static final class Clip {
        final int keys, nn;
        final double[] a;
        final boolean[] hasT;

        Clip(int keys, int nn, double[] a, boolean[] hasT) {
            this.keys = keys;
            this.nn = nn;
            this.a = a;
            this.hasT = hasT;
        }

        int at(int k, int i) {
            return (k * nn + i) * 7;
        }

        Clip copy() {
            return new Clip(keys, nn, a.clone(), hasT.clone());
        }
    }

    static final class Pack {
        int nn;
        double height;
        Node[] nodes;
        List<Integer> texHash = new ArrayList<>();
        List<byte[]> texRec = new ArrayList<>();
        List<Packet> pks = new ArrayList<>();
        LinkedHashMap<String, Clip> clips = new LinkedHashMap<>();

        static Pack read(byte[] b) throws IOException {
            if (b.length < 60 || b[0] != 'G' || b[1] != 'P' || b[2] != 'K' || b[3] != '1') throw new IOException("not a GPK1");
            final Pack p = new Pack();
            final int nn = Bin.le32(b, 8), npk = Bin.le32(b, 12), nt = Bin.le32(b, 16), na = Bin.le32(b, 20);
            p.nn = nn;
            p.height = Bin.lef32(b, 24);
            int o = 60;
            p.nodes = new Node[nn];
            for (int i = 0; i < nn; i++) {
                final Node n = new Node();
                n.id = Bin.le32(b, o) & 0xFFFFFFFFL;
                n.par = Bin.le32(b, o + 4);
                for (int j = 0; j < 3; j++) n.trans[j] = Bin.lef32(b, o + 8 + 4 * j);
                n.hasInv = Bin.le32(b, o + 20) != 0;
                for (int j = 0; j < 16; j++) n.inv[j] = Bin.lef32(b, o + 24 + 4 * j);
                p.nodes[i] = n;
                o += 88;
            }
            for (int t = 0; t < nt; t++) {
                final int h = Bin.le32(b, o), sz = Bin.le32(b, o + 4);
                p.texHash.add(h);
                p.texRec.add(Bin.slice(b, o + 8, sz));
                o += 8 + ((sz + 3) & ~3);
            }
            for (int k = 0; k < npk; k++) {
                final Packet q = new Packet();
                q.th = Bin.le32(b, o);
                q.nv = Bin.le32(b, o + 4);
                q.ni = Bin.le32(b, o + 8);
                q.body = Bin.slice(b, o + 12, q.nv * 52);
                q.strip = Bin.slice(b, o + 12 + q.nv * 52, q.ni * 2);
                p.pks.add(q);
                o += 12 + q.nv * 52 + ((q.ni * 2 + 3) & ~3);
            }
            for (int c = 0; c < na; c++) {
                int l = 0;
                while (l < 48 && b[o + l] != 0) l++;
                final String nm = new String(b, o, l, StandardCharsets.UTF_8);
                final int keys = Bin.le32(b, o + 48);
                final double[] a = new double[keys * nn * 7];
                for (int i = 0; i < a.length; i++) a[i] = Bin.lef32(b, o + 52 + 4 * i);
                final boolean[] ht = new boolean[nn];
                final int hb = o + 52 + keys * nn * 28;
                for (int i = 0; i < nn; i++) ht[i] = b[hb + i] != 0;
                p.clips.put(nm, new Clip(keys, nn, a, ht));
                o += 52 + keys * nn * 28 + ((nn + 3) & ~3);
            }
            return p;
        }
    }

    // ------------------------------------------------------------------ clips out of a .sanim / .nis (decode_clips)
    static LinkedHashMap<String, Clip> decodeClips(byte[] d, int nnodes, Trig trig) throws IOException {
        final LinkedHashMap<String, Clip> out = new LinkedHashMap<>();
        final double[] q = new double[4];
        for (Chunks.C c : Chunks.list(d, 0, d.length)) {
            if (c.id != 0x80017000) continue;
            final ArrayList<Chunks.C> sub = Chunks.list(d, c.ds, c.end);
            if (sub.size() < 2) continue;
            final byte[] nmData = sub.get(1).data(d);
            int nl = 0;
            while (nl < nmData.length && nmData[nl] != 0) nl++;
            final StringBuilder sb = new StringBuilder();
            for (int i = 0; i < nl; i++) sb.append((nmData[i] & 0x80) != 0 ? '�' : (char) nmData[i]);
            final String nm = sb.toString();
            final byte[] hdr = sub.get(0).data(d);
            if (hdr.length < 16) throw new IOException("clip " + nm + ": short header");
            final long nkeysL = Bin.u32L(hdr, 8), an = Bin.u32L(hdr, 12);
            if (an != nnodes) continue;
            if (nkeysL > 1_000_000) throw new IOException("clip " + nm + ": " + nkeysL + " keys");
            final int nkeys = (int) nkeysL;
            final ArrayList<Chunks.C> nodes = new ArrayList<>();
            for (Chunks.C s : sub) if (s.id == 0x80017100) nodes.add(s);
            final int propsAt = sub.get(sub.size() - 1).ds;
            if (propsAt + 4L * an > d.length) throw new IOException("clip " + nm + ": props past the end");
            final double[] arr = new double[nkeys * nnodes * 7];
            for (int i = 3; i < arr.length; i += 7) arr[i] = 1.0;
            final boolean[] hasT = new boolean[nnodes];
            for (int i = 0; i < Math.min(nodes.size(), nnodes); i++) {
                final Chunks.C nd = nodes.get(i);
                final int pr = Bin.u32(d, propsAt + 4 * i);
                double[][] r = null;
                double[][] t = null;
                for (Chunks.C s : Chunks.list(d, nd.ds, nd.end)) {
                    if (s.id == 0x17101) {
                        final int nk = (pr & 2) != 0 ? 1 : nkeys;
                        final double[][] keys = new double[nk][];
                        final int sds = s.ds;
                        for (int k = 0; k < nk; k++) {
                            if ((pr & 1) != 0) {  // a turn about Z (GuestPack.angleKey's numbers, the CRT's answers)
                                final double ang = (double) (Bin.u16(d, sds + 2 * k) * 2) * Math.PI / 65536.0;
                                keys[k] = new double[] {0.0, 0.0, trig.sin(ang / 2), trig.cos(ang / 2)};
                            } else if ((pr & 0x10) != 0) {
                                final int o = sds + 8 * k;
                                keys[k] = new double[] {Bin.s16(d, o) / 32768.0, Bin.s16(d, o + 2) / 32768.0,
                                        Bin.s16(d, o + 4) / 32768.0, Bin.s16(d, o + 6) / 32768.0};
                            } else if ((pr & 0x20) != 0) {
                                final int o = sds + 6 * k;
                                if (o + 6 > d.length) throw new IOException("clip " + nm + ": packed key past the end");
                                final int r0 = d[o] & 0xFF, r1 = d[o + 1] & 0xFF, r2 = d[o + 2] & 0xFF;
                                final int r3 = d[o + 3] & 0xFF, r4 = d[o + 4] & 0xFF, r5 = d[o + 5] & 0xFF;
                                keys[k] = new double[] {(short) ((r0 << 8) | (r1 & 0xF0)) / 32768.0,
                                        (short) ((r2 << 8) | ((r1 << 4) & 0xFF)) / 32768.0,
                                        (short) ((r3 << 8) | (r4 & 0xF0)) / 32768.0,
                                        (short) ((r5 << 8) | ((r4 << 4) & 0xFF)) / 32768.0};
                            } else {
                                final int o = sds + 4 * k;
                                keys[k] = new double[] {Bin.s8(d, o) / 128.0, Bin.s8(d, o + 1) / 128.0,
                                        Bin.s8(d, o + 2) / 128.0, Bin.s8(d, o + 3) / 128.0};
                            }
                        }
                        r = keys;
                    } else if (s.id == 0x17102) {
                        final int n = Math.max(0, Math.floorDiv(s.end - s.ds, 12));
                        if (s.ds + 12L * n > d.length) throw new IOException("clip " + nm + ": key past the end");
                        t = new double[n][];
                        for (int k = 0; k < n; k++)
                            t[k] = new double[] {Bin.f32(d, s.ds + 12 * k), Bin.f32(d, s.ds + 12 * k + 4),
                                    Bin.f32(d, s.ds + 12 * k + 8)};
                    }
                }
                for (int k = 0; k < nkeys; k++) {
                    final int at = (k * nnodes + i) * 7;
                    if (r != null && r.length > 0) {
                        final double[] rk = r[Math.min(k, r.length - 1)];
                        double nrm = Math.sqrt(dot4(rk, rk));
                        if (nrm == 0.0) nrm = 1.0;
                        for (int j = 0; j < 4; j++) arr[at + j] = rk[j] / nrm;
                    }
                    if (t != null && t.length > 0) {
                        final double[] tk = t[Math.min(k, t.length - 1)];
                        arr[at + 4] = tk[0];
                        arr[at + 5] = tk[1];
                        arr[at + 6] = tk[2];
                    }
                }
                hasT[i] = t != null && t.length > 0;
            }
            String key = nm;
            int n2 = 2;
            while (out.containsKey(key)) key = nm + "#" + n2++;
            out.put(key, new Clip(nkeys, nnodes, arr, hasT));
        }
        return out;
    }

    /** numpy's 1-D dot (OpenBLAS ddot, short: fused multiply-adds from 0 in order). */
    static double dot4(double[] x, double[] y) {
        double s = 0.0;
        for (int i = 0; i < x.length; i++) s = Fma.fma(y[i], x[i], s);
        return s;
    }

    // ------------------------------------------------------------------ all its clips (all_clips)
    static String nis(String name, String rest) {
        return "Art/nis/" + name + "_" + rest + ".nis";
    }

    static LinkedHashMap<String, Clip> allClips(Pack p, String name, Source src, Setup s) throws IOException {
        final LinkedHashMap<String, Clip> clips = new LinkedHashMap<>(p.clips);
        final String fe = "Art/animation/" + name + "fe.sanim";
        if (src.has(fe)) clips.putAll(decodeClips(src.read(fe), p.nn, s.trig));
        // the cut-scenes it does (os.listdir sorted, filtered by the kept starts): each kept start with _0, _1, ...
        final ArrayList<String> rests = new ArrayList<>();
        for (String k : s.keep)
            for (int i = 0; i < s.tries; i++) {
                final String rest = k + i;
                if (rest.contains("_same") || rest.contains("_other")) continue;
                if (src.has(nis(name, rest))) rests.add(rest);
            }
        final ArrayList<String> files = new ArrayList<>();
        for (String r : rests) files.add(name + "_" + r + ".nis");
        Collections.sort(files);
        for (String f : files) {
            final String rest = f.substring(name.length() + 1, f.length() - 4);
            final LinkedHashMap<String, Clip> got = decodeClips(src.read(nis(name, rest)), p.nn, s.trig);
            if (!got.isEmpty()) clips.put("nis:" + rest, got.values().iterator().next());
        }
        // a part keyed in the cut-scene's own space (Diddy's barrel jets: 13 m off his hips) takes its idle place
        final int hips = hipsOf(p)[1];
        final Clip idle = clips.get("idle");
        if (idle == null || idle.keys == 0) throw new IOException(name + ": no idle clip");
        final ArrayList<String> names = new ArrayList<>();
        for (String cn : clips.keySet()) if (cn.startsWith("nis:") || cn.startsWith("fe_")) names.add(cn);
        for (String cn : names) {
            final Clip c = clips.get(cn).copy();
            for (int i = 0; i < p.nn; i++) {
                if (i == hips || p.nodes[i].par < 0) continue;
                final double[] ref = new double[3];
                for (int j = 0; j < 3; j++) ref[j] = idle.hasT[i] ? idle.a[idle.at(0, i) + 4 + j] : p.nodes[i].trans[j];
                if (!c.hasT[i]) continue;
                if (c.keys == 0) throw new IOException(cn + ": no keys");
                double mx = -Double.MAX_VALUE;
                boolean nan = false;
                for (int k = 0; k < c.keys; k++) {
                    final int at = c.at(k, i) + 4;
                    final double dx = c.a[at] - ref[0], dy = c.a[at + 1] - ref[1], dz = c.a[at + 2] - ref[2];
                    final double v = Math.sqrt(dx * dx + dy * dy + dz * dz);
                    if (Double.isNaN(v)) nan = true;
                    if (v > mx) mx = v;
                }
                if (!nan && mx > 0.35 * p.height) {
                    for (int k = 0; k < c.keys; k++) System.arraycopy(idle.a, idle.at(0, i), c.a, c.at(k, i), 7);
                    c.hasT[i] = idle.hasT[i];
                }
            }
            clips.put(cn, c);
        }
        return clips;
    }

    // ------------------------------------------------------------------ posing
    /** quat_mat: (x y z w) -> 3x3, row-major. */
    static double[] quatMat(double x, double y, double z, double w) {
        return new double[] {
                1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
    }

    static double[] matQuat(double[] m) {
        final double tr = m[0] + m[4] + m[8];
        if (tr > 0) {
            final double S = Math.sqrt(tr + 1.0) * 2;
            return new double[] {(m[5] - m[7]) / S, (m[6] - m[2]) / S, (m[1] - m[3]) / S, 0.25 * S};
        }
        if (m[0] > m[4] && m[0] > m[8]) {
            final double S = Math.sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
            return new double[] {0.25 * S, (m[1] + m[3]) / S, (m[6] + m[2]) / S, (m[5] - m[7]) / S};
        }
        if (m[4] > m[8]) {
            final double S = Math.sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
            return new double[] {(m[1] + m[3]) / S, 0.25 * S, (m[5] + m[7]) / S, (m[6] - m[2]) / S};
        }
        final double S = Math.sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
        return new double[] {(m[6] + m[2]) / S, (m[5] + m[7]) / S, 0.25 * S, (m[1] - m[3]) / S};
    }

    /** n x n @ n x n, row-major, as numpy's matmul here (OpenBLAS dgemm: fused multiply-adds over k in order). */
    static double[] matmul(double[] A, double[] B, int n) {
        final double[] C = new double[n * n];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                double s = A[i * n] * B[j];
                for (int k = 1; k < n; k++) s = Fma.fma(A[i * n + k], B[k * n + j], s);
                C[i * n + j] = s;
            }
        return C;
    }

    /** Each node's local rotation (x y z w) and translation at key k (locals_of). */
    static void locals(Pack p, Clip c, int k, double[][] Q, double[][] T) {
        k = Math.min(k, c.keys - 1);
        for (int i = 0; i < p.nn; i++) {
            final int at = c.at(k, i);
            Q[i] = new double[] {c.a[at], c.a[at + 1], c.a[at + 2], c.a[at + 3]};
            T[i] = c.hasT[i] ? new double[] {c.a[at + 4], c.a[at + 5], c.a[at + 6]} : p.nodes[i].trans.clone();
        }
    }

    /** Every node's world matrix (4x4 row-major, row vectors): worlds(). */
    static double[][] worlds(Pack p, double[][] Q, double[][] T) {
        final double[][] W = new double[p.nn][];
        for (int i = 0; i < p.nn; i++) {
            final int par = p.nodes[i].par;
            if (par < 0 || par >= i) {
                W[i] = new double[] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                continue;
            }
            final double[] r = quatMat(Q[i][0], Q[i][1], Q[i][2], Q[i][3]);
            final double[] L = {r[0], r[1], r[2], 0, r[3], r[4], r[5], 0, r[6], r[7], r[8], 0, T[i][0], T[i][1], T[i][2], 1};
            W[i] = matmul(L, W[par], 4);
        }
        return W;
    }

    /** {root, hips}: the root's child that carries the body (the most nodes under it). */
    static int[] hipsOf(Pack p) throws IOException {
        final int nn = p.nn;
        final ArrayList<ArrayList<Integer>> kids = new ArrayList<>();
        for (int i = 0; i < nn; i++) kids.add(new ArrayList<>());
        for (int i = 0; i < nn; i++) {
            final int par = p.nodes[i].par;
            if (0 <= par && par < i) kids.get(par).add(i);
        }
        int root = -1;
        for (int i = 0; i < nn && root < 0; i++) if (p.nodes[i].par < 0 || p.nodes[i].par >= i) root = i;
        if (root < 0 || kids.get(root).isEmpty()) throw new IOException("no hips");
        final int[] count = new int[nn];
        for (int i = nn - 1; i >= 0; i--) {
            count[i] = 1;
            for (int c : kids.get(i)) count[i] += count[c];
        }
        int best = -1;
        for (int c : kids.get(root)) if (best < 0 || count[c] > count[best]) best = c;
        return new int[] {root, best};
    }

    /**
     * How far the hips are turned about Z from the reference hips frame: yaw_of's atan2 arguments {y, x} (its answer
     * taken only where a move uses it), or null for 0 (no reference axis off the vertical).
     */
    static double[] yawArgs(double[][] W, int hips, double[] ref) {
        final double[] R = W[hips];
        int best = -1;
        for (int a = 0; a < 3; a++) {
            final double x = ref[a * 3], y = ref[a * 3 + 1];
            if (Math.sqrt(Fma.fma(y, y, x * x)) > 0.5) {
                best = a;
                break;
            }
        }
        if (best < 0) return null;
        final double d00 = ref[best * 3], d01 = ref[best * 3 + 1], d10 = R[best * 4], d11 = R[best * 4 + 1];
        return new double[] {d00 * d11 - d01 * d10, Fma.fma(d01, d11, d00 * d10)};
    }

    /** The model's vertices (every step-th), skinned by a frame's world matrices: only the heights (z) are used. */
    static final class Skinner {
        final int nv;
        final double[] V;   // x y z 1
        final int[] ND;     // 4 per vertex
        final double[] WT;  // 4 per vertex, normalised
        final double[][] inv;

        Skinner(Pack p, int step) {
            int n = 0;
            for (Packet q : p.pks) n += (q.nv + step - 1) / step;
            nv = n;
            V = new double[n * 4];
            ND = new int[n * 4];
            WT = new double[n * 4];
            int v = 0;
            for (Packet q : p.pks)
                for (int k = 0; k < q.nv; k += step) {
                    final int o = k * 52;
                    for (int j = 0; j < 3; j++) V[v * 4 + j] = Bin.lef32(q.body, o + 4 * j);
                    V[v * 4 + 3] = 1.0;
                    for (int j = 0; j < 4; j++) {
                        final int node = q.body[o + 32 + j] & 0xFF;
                        ND[v * 4 + j] = Math.min(node, p.nn - 1);
                        WT[v * 4 + j] = node < p.nn ? Bin.lef32(q.body, o + 36 + 4 * j) : 0.0;
                    }
                    double tw = WT[v * 4] + WT[v * 4 + 1];
                    tw = tw + WT[v * 4 + 2];
                    tw = tw + WT[v * 4 + 3];
                    if (tw == 0) tw = 1.0;
                    for (int j = 0; j < 4; j++) WT[v * 4 + j] = WT[v * 4 + j] / tw;
                    v++;
                }
            inv = new double[p.nn][];
            for (int i = 0; i < p.nn; i++)
                inv[i] = p.nodes[i].hasInv ? p.nodes[i].inv : new double[] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        }

        /** Each vertex's skinned z (points()[:, 2]: einsum sums from 0, then the weighted sum over its 4 bones). */
        double[] heights(double[][] W) {
            final int nn = W.length;
            final double[][] S2 = new double[nn][4];  // column 2 of inv @ W, per node
            for (int q = 0; q < nn; q++)
                for (int i = 0; i < 4; i++) {
                    double s = 0.0;
                    for (int j = 0; j < 4; j++) s = s + inv[q][i * 4 + j] * W[q][j * 4 + 2];
                    S2[q][i] = s;
                }
            final double[] z = new double[nv];
            for (int v = 0; v < nv; v++) {
                double o = 0.0;
                for (int j = 0; j < 4; j++) {
                    final double[] m = S2[ND[v * 4 + j]];
                    double e = 0.0;
                    for (int i = 0; i < 4; i++) e = e + V[v * 4 + i] * m[i];
                    o = o + WT[v * 4 + j] * e;
                }
                z[v] = o;
            }
            return z;
        }
    }

    /** Per key: the hips' place and turn, its lowest and highest point (track). */
    static final class Track {
        final Clip clip;
        final double[] x, y, z, low, top;
        final double[][] yaw;  // yaw_of's atan2 arguments per key (null: 0)

        Track(Clip clip) {
            this.clip = clip;
            final int n = clip.keys;
            x = new double[n];
            y = new double[n];
            z = new double[n];
            yaw = new double[n][];
            low = new double[n];
            top = new double[n];
        }

        int size() {
            return clip.keys;
        }

        double yaw(int k, Trig trig) {
            return yaw[k] == null ? 0.0 : trig.atan2(yaw[k][0], yaw[k][1]);
        }
    }

    static Track track(Pack p, Skinner sk, Clip c, int hips, double[] ref) {
        final Track t = new Track(c);
        final double[][] Q = new double[p.nn][], T = new double[p.nn][];
        for (int k = 0; k < c.keys; k++) {
            locals(p, c, k, Q, T);
            final double[][] W = worlds(p, Q, T);
            final double[] zs = sk.heights(W);
            t.x[k] = W[hips][12];
            t.y[k] = W[hips][13];
            t.z[k] = W[hips][14];
            t.yaw[k] = yawArgs(W, hips, ref);
            double lo = zs.length > 0 ? zs[0] : Double.NaN, hi = lo;
            for (int v = 1; v < zs.length; v++) {
                if (zs[v] < lo || Double.isNaN(zs[v])) lo = Double.isNaN(lo) ? lo : zs[v];
                if (zs[v] > hi || Double.isNaN(zs[v])) hi = Double.isNaN(hi) ? hi : zs[v];
            }
            t.low[k] = lo;
            t.top[k] = hi;
        }
        return t;
    }

    /** A clip cut where the cut-scene's camera cuts: shots as (from, to) keys. */
    static List<int[]> shots(Track tr, double stand) {
        final ArrayList<Integer> cuts = new ArrayList<>();
        cuts.add(0);
        for (int k = 1; k < tr.size(); k++) {
            final double dx = tr.x[k] - tr.x[k - 1], dy = tr.y[k] - tr.y[k - 1];
            if (Math.sqrt(Fma.fma(dy, dy, dx * dx)) > 0.5 * stand || Math.abs(tr.z[k] - tr.z[k - 1]) > 0.5 * stand) cuts.add(k);
        }
        cuts.add(tr.size());
        final ArrayList<int[]> out = new ArrayList<>();
        for (int i = 0; i < cuts.size() - 1; i++) out.add(new int[] {cuts.get(i), cuts.get(i + 1)});
        return out;
    }

    /** Inside a shot, the stretches at least a second long where it stays about on the spot. */
    static List<int[]> stillParts(Track tr, int a, int b, double stand) {
        final ArrayList<int[]> out = new ArrayList<>();
        final int n = b - a;
        if (n < 3) return out;
        final boolean[] still = new boolean[n];
        for (int k = 0; k < n; k++) {
            final int k0 = Math.max(0, k - 4), k1 = Math.min(n - 1, k + 4);
            final double dx = tr.x[a + k1] - tr.x[a + k0], dy = tr.y[a + k1] - tr.y[a + k0];
            final double v = Math.sqrt(Fma.fma(dy, dy, dx * dx)) / Math.max(1, k1 - k0) * KEYS_PER_S;
            still[k] = v < 1.0 * stand;
        }
        int k = 0;
        while (k < n) {
            if (!still[k]) {
                k++;
                continue;
            }
            int e = k;
            while (e < n && still[e]) e++;
            if ((e - k) / KEYS_PER_S >= 1.0) out.add(new int[] {a + k, a + e});
            k = e;
        }
        return out;
    }

    /** null = a whole body on its feet; else why not ("short", "falls", "squashed"). */
    static String shotOk(Track tr, int a, int b, double stand) {
        if ((b - a) / KEYS_PER_S < 1.0) return "short";
        double lmax = -Double.MAX_VALUE, lmin = Double.MAX_VALUE, tmax = -Double.MAX_VALUE, tmin = Double.MAX_VALUE;
        for (int k = a; k < b; k++) {
            lmax = Math.max(lmax, tr.low[k]);
            lmin = Math.min(lmin, tr.low[k]);
            final double tall = tr.top[k] - tr.low[k];
            tmax = Math.max(tmax, tall);
            tmin = Math.min(tmin, tall);
        }
        if (lmax - lmin > 0.9 * stand) return "falls";
        if (tmin < 0.45 * stand || tmax > 2.0 * stand) return "squashed";
        return null;
    }

    /** good_parts: (from, to, why) as (int[2], String). */
    static List<Object[]> goodParts(Track tr, double stand) {
        final ArrayList<Object[]> out = new ArrayList<>();
        for (int[] s : shots(tr, stand))
            for (int[] cd : stillParts(tr, s[0], s[1], stand)) out.add(new Object[] {cd, shotOk(tr, cd[0], cd[1], stand)});
        return out;
    }

    /** A move: its kind (0 idle .. 6 upset), its clip's track, from / to keys. */
    static final class Seg {
        final int kind, a, b;
        final String label;
        final Track tr;

        Seg(int kind, String label, Track tr, int a, int b) {
            this.kind = kind;
            this.label = label;
            this.tr = tr;
            this.a = a;
            this.b = b;
        }
    }

    // ------------------------------------------------------------------ the loop + the pack (build)
    static byte[][] build(Cast cast, Pack p, LinkedHashMap<String, Clip> clips, Trig trig) throws IOException {
        final int nn = p.nn;
        final int hips = hipsOf(p)[1];
        final Skinner sk = new Skinner(p, 4);
        final double[][] Q0 = new double[nn][], T0 = new double[nn][];
        locals(p, clips.get("idle"), 0, Q0, T0);
        final double[] H = worlds(p, Q0, T0)[hips];
        final double[] ref = {H[0], H[1], H[2], H[4], H[5], H[6], H[8], H[9], H[10]};
        final String idle = cast.idleClip != null && clips.containsKey(cast.idleClip) ? cast.idleClip  // r141
                : (clips.containsKey("fe_idle") ? "fe_idle" : "idle");
        final Track trIdle = track(p, sk, clips.get(idle), hips, ref);
        if (trIdle.size() == 0) throw new IOException("empty idle");
        final double stand = trIdle.top[0] - trIdle.low[0];
        final HashMap<String, Track> trk = new HashMap<>();
        // its moves: the cut-scenes' good shots (celebrations, intros, the cup)
        final ArrayList<Seg> cel = new ArrayList<>();
        for (String kind : new String[] {"goal_winner", "capt_intro", "cup_win"}) {
            for (String cn : sortedNis(clips, kind, null)) {
                final Track tr = trackOf(trk, p, sk, clips, cn, hips, ref);
                for (Object[] g : goodParts(tr, stand))
                    if (g[1] == null) cel.add(new Seg(2, cn, tr, ((int[]) g[0])[0], ((int[]) g[0])[1]));
            }
        }
        final ArrayList<Seg> segs = new ArrayList<>();
        segs.add(new Seg(0, idle, trIdle, 0, trIdle.size()));
        for (String c : new String[] {"wave", "fe_idle_action_01"})
            if (clips.containsKey(c)) {
                final Track t = trackOf(trk, p, sk, clips, c, hips, ref);
                segs.add(new Seg(1, c, t, 0, t.size()));
            }
        // r141 (home.txt "nocel"): its cut-scene shots after its own moves (its first big move: a BLACK 1/1's throne loop)
        if (!cast.noCel) for (int i = 0; i < Math.min(10, cel.size()); i++) segs.add(cel.get(i));
        if (clips.containsKey("arm_powerup")) {
            final Track t = trackOf(trk, p, sk, clips, "arm_powerup", hips, ref);
            segs.add(new Seg(2, "arm_powerup", t, 0, t.size()));
        }
        for (String m : cast.moves)  // r141: a Charged sidekick's showy moves (its cards' action pose too)
            if (clips.containsKey(m)) {
                final Track t = trackOf(trk, p, sk, clips, m, hips, ref);
                segs.add(new Seg(2, m, t, 0, t.size()));
            }
        if (cast.noCel) for (int i = 0; i < Math.min(10, cel.size()); i++) segs.add(cel.get(i));  // (its big picture's still)
        String react = null;
        for (String c : new String[] {"hit_react_1_300", "hit_react_1_600", "hit_react_1_1200"})
            if (react == null && clips.containsKey(c)) react = c;
        if (react != null) {
            final Track t = trackOf(trk, p, sk, clips, react, hips, ref);
            segs.add(new Seg(3, react, t, 0, t.size()));
        }
        if (clips.containsKey("wave")) {
            final Track t = trackOf(trk, p, sk, clips, "wave", hips, ref);
            segs.add(new Seg(4, "wave", t, 0, t.size()));
        }
        // r129: 5 = cheer (its goal celebrations' on-the-spot shots, else its cup win's), 6 = upset
        ArrayList<Seg> cheer = new ArrayList<>();
        for (Seg s : cel) if (s.label.contains("goal_winner") && cheer.size() < 3) cheer.add(s);
        if (cheer.isEmpty()) for (Seg s : cel) if (s.label.contains("cup_win") && cheer.size() < 3) cheer.add(s);
        if (cast.noCel) cheer.clear();  // r141
        for (Seg s : cheer) segs.add(new Seg(5, s.label, s.tr, s.a, s.b));
        final ArrayList<Seg> upset = new ArrayList<>();
        for (String cn : sortedNis(clips, "defeated", "outraged")) {
            final Track tr = trackOf(trk, p, sk, clips, cn, hips, ref);
            for (Object[] g : goodParts(tr, stand))
                if (!"short".equals(g[1])) upset.add(new Seg(6, cn, tr, ((int[]) g[0])[0], ((int[]) g[0])[1]));
        }
        for (int i = 0; i < Math.min(2, upset.size()); i++) segs.add(upset.get(i));

        // each on the spot: its hips' middle on 0, turned to face as the idle does, its lowest point on the floor
        final ArrayList<double[][][]> frames = new ArrayList<>();  // {Q, T}
        final ArrayList<int[]> segTable = new ArrayList<>();
        final int half = (int) (KEYS_PER_S / 2);
        for (Seg sg : segs) {
            final int start = frames.size();
            final Track tr = sg.tr;
            final int a = sg.a, b = sg.b, n = b - a;
            final double[] smx = new double[n], smy = new double[n];
            for (int k = 0; k < n; k++) {
                final int lo = Math.max(0, k - half), hi = Math.min(n, k + half + 1);
                double sx = tr.x[a + lo], sy = tr.y[a + lo];
                for (int q = lo + 1; q < hi; q++) {
                    sx = sx + tr.x[a + q];
                    sy = sy + tr.y[a + q];
                }
                smx[k] = sx / (hi - lo);
                smy[k] = sy / (hi - lo);
            }
            final double[] sins = new double[n], coss = new double[n];
            double lift = Double.NaN;
            for (int k = 0; k < n; k++) {
                final double yw = tr.yaw(a + k, trig);
                sins[k] = trig.sin(yw);
                coss[k] = trig.cos(yw);
                if (k == 0 || tr.low[a + k] < lift) lift = tr.low[a + k];
            }
            lift = -lift;
            final double ywm = trig.atan2(GuestPack.pySum(sins), GuestPack.pySum(coss));
            final double cs = trig.cos(-ywm), sn = trig.sin(-ywm);
            final double[] Rz = {cs, sn, 0.0, -sn, cs, 0.0, 0.0, 0.0, 1.0};
            final int nOut = Math.max(2, (int) Math.rint(n / KEYS_PER_S * FPS));
            for (int j = 0; j < nOut; j++) {
                final int k = Math.min(b - 1, a + (int) Math.rint(j * KEYS_PER_S / FPS));
                final double[][] Q = new double[nn][], T = new double[nn][];
                locals(p, tr.clip, k, Q, T);
                final double[] Ht = T[hips].clone();
                Ht[0] -= smx[k - a];
                Ht[1] -= smy[k - a];
                final double[] qh = Q[hips];
                Q[hips] = matQuat(matmul(quatMat(qh[0], qh[1], qh[2], qh[3]), Rz, 3));
                final double h0 = Ht[0], h1 = Ht[1], h2 = Ht[2];
                for (int c = 0; c < 3; c++) Ht[c] = Fma.fma(h2, Rz[6 + c], Fma.fma(h1, Rz[3 + c], h0 * Rz[c]));
                Ht[2] += lift;
                T[hips] = Ht;
                frames.add(new double[][][] {Q, T});
            }
            segTable.add(new int[] {start, frames.size() - start, sg.kind});
        }

        // its head (it looks at you): the deepest node over every vertex at the top of it, standing
        final int headNode = headNode(p, sk, clips.get(idle));
        // life size
        final double s = cast.height / Math.max(1e-3, stand);
        final Out gev = new Out();
        gev.fixed("GEV1", 4).le32(nn).le32(frames.size()).lef32((float) FPS);
        for (double[][][] f : frames)
            for (double[] M : worlds(p, f[0], f[1]))
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 3; c++) gev.lef32((float) (M[r * 4 + c] * s));
        gev.fixed("SEG1", 4).le32(segTable.size()).le32(headNode);
        for (int[] t : segTable) gev.le32(t[0]).le32(t[1]).le32(t[2]);

        // the pack: GPK2, its pictures RGBA8, normals turned in (GPK2's convention), no clips
        final Out nodes = new Out();
        for (Node n : p.nodes) {
            final double[] bind = n.hasInv ? Inverse.inv(n.inv) : new double[] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            nodes.le32((int) n.id).le32(n.par);
            for (int j = 0; j < 3; j++) nodes.lef32((float) n.trans[j]);
            nodes.le32(n.hasInv ? 1 : 0);
            for (int j = 0; j < 16; j++) nodes.lef32((float) n.inv[j]);
            nodes.le32(0);
            for (int j = 0; j < 16; j++) nodes.lef32((float) bind[j]);
        }
        final Out texs = new Out();
        int nt = 0;
        for (int t = 0; t < p.texRec.size(); t++) {
            final byte[] rec = p.texRec.get(t);
            final byte[] rgba;
            final int w, hh;
            try {
                final long efmt = Bin.u32L(rec, 4);
                w = Bin.u16(rec, 0x0E);
                hh = Bin.u16(rec, 0x10);
                if (efmt < 0 || efmt >= ENGINE_FMT.length) throw new IOException("format " + efmt);
                final int fmt = ENGINE_FMT[(int) efmt];
                if ((fmt == GxTex.C8 || fmt == GxTex.C4) && w > 0 && hh > 0) throw new IOException("no palette");
                rgba = GxTex.decode(rec, 0x20, fmt, w, hh, null);
            } catch (IOException | RuntimeException e) {  // gxtex raises: the Python tool leaves the picture out
                continue;
            }
            final byte[] r8 = rgba8Record(rgba, w, hh);
            texs.le32(p.texHash.get(t)).le32(r8.length).bytes(r8).align(4);
            nt++;
        }
        final Out pks = new Out();
        int npk = 0;
        for (Packet q : p.pks) {
            if (!cast.hide.isEmpty()) {
                boolean allHidden = true;
                for (int v = 0; v < q.nv && allHidden; v++) {
                    final int o = v * 52;
                    int best = 0;
                    float bw = Bin.lef32(q.body, o + 36);
                    for (int j = 1; j < 4; j++) {
                        final float wj = Bin.lef32(q.body, o + 36 + 4 * j);
                        if (wj > bw) {
                            bw = wj;
                            best = j;
                        }
                    }
                    if (!cast.hide.contains(q.body[o + 32 + best] & 0xFF)) allHidden = false;
                }
                if (allHidden) continue;
            }
            npk++;
            final byte[] nb = q.body.clone();
            for (int v = 0; v < q.nv; v++)
                for (int j = 0; j < 3; j++) {
                    final int o = v * 52 + 12 + 4 * j;
                    final int bits = Bin.le32(nb, o) ^ 0x80000000;  // -x of a float: its sign bit
                    nb[o] = (byte) bits;
                    nb[o + 1] = (byte) (bits >> 8);
                    nb[o + 2] = (byte) (bits >> 16);
                    nb[o + 3] = (byte) (bits >> 24);
                }
            pks.le32(q.th).le32(q.nv).le32(q.ni).bytes(nb).bytes(q.strip).align(4);
        }
        final Out pack = new Out();
        pack.fixed("GPK2", 4).le32(2).le32(nn).le32(npk).le32(nt).le32(0).lef32((float) cast.height);
        pack.fixed(cast.pack, 31).zeros(1).le32(0);
        pack.bytes(nodes.toBytes()).bytes(texs.toBytes()).bytes(pks.toBytes());
        return new byte[][] {pack.toBytes(), gev.toBytes()};
    }

    static Track trackOf(Map<String, Track> trk, Pack p, Skinner sk, Map<String, Clip> clips, String c, int hips,
                         double[] ref) {
        Track t = trk.get(c);
        if (t == null) {
            t = track(p, sk, clips.get(c), hips, ref);
            trk.put(c, t);
        }
        return t;
    }

    /** sorted(c for c in clips if c.startswith("nis:") and (a in c or b in c)). */
    static List<String> sortedNis(Map<String, Clip> clips, String a, String b) {
        final ArrayList<String> out = new ArrayList<>();
        for (String c : clips.keySet())
            if (c.startsWith("nis:") && (c.contains(a) || (b != null && c.contains(b)))) out.add(c);
        Collections.sort(out);
        return out;
    }

    /** The deepest node shared by the chains of the top eighth of its vertices, standing. */
    static int headNode(Pack p, Skinner sk, Clip idle) {
        final double[][] Q = new double[p.nn][], T = new double[p.nn][];
        locals(p, idle, 0, Q, T);
        final double[] z = sk.heights(worlds(p, Q, T));
        final Integer[] order = new Integer[z.length];
        for (int i = 0; i < z.length; i++) order[i] = i;
        java.util.Arrays.sort(order, (x, y) -> Double.compare(z[x], z[y]));
        final int take = Math.max(1, z.length / 8);
        final ArrayList<int[]> chains = new ArrayList<>();
        int minLen = Integer.MAX_VALUE;
        for (int t = z.length - take; t < z.length; t++) {
            final int v = order[t];
            int best = 0;
            for (int j = 1; j < 4; j++) if (sk.WT[v * 4 + j] > sk.WT[v * 4 + best]) best = j;
            int n = sk.ND[v * 4 + best];
            final ArrayList<Integer> chain = new ArrayList<>();
            while (0 <= n && n < p.nn) {
                chain.add(n);
                final int par = p.nodes[n].par;
                n = par < n ? par : -1;
            }
            Collections.reverse(chain);
            final int[] c = new int[chain.size()];
            for (int i = 0; i < c.length; i++) c[i] = chain.get(i);
            chains.add(c);
            minLen = Math.min(minLen, c.length);
        }
        int head = 0;
        for (int depth = 0; depth < minLen; depth++) {
            final int v0 = chains.get(0)[depth];
            boolean same = true;
            for (int[] c : chains) if (c[depth] != v0) same = false;
            if (!same) break;
            head = v0;
        }
        return head;
    }

    /** meleepack.texture_record: GXTextureHeader (big-endian) + RGBA8 tiles; alpha bits 1 when any texel &lt; 250. */
    static byte[] rgba8Record(byte[] rgba, int w, int h) {
        boolean see = false;
        for (int i = 3; i < rgba.length; i += 4)
            if ((rgba[i] & 0xFF) < 250) {
                see = true;
                break;
            }
        final int bw = (w + 3) / 4, bh = (h + 3) / 4;
        final byte[] out = new byte[32 + bw * bh * 64];
        out[3] = 1;
        out[7] = 3;
        out[8] = 8;
        out[9] = 8;
        out[10] = 8;
        out[11] = (byte) (see ? 1 : 0);
        out[14] = (byte) (w >> 8);
        out[15] = (byte) w;
        out[16] = (byte) (h >> 8);
        out[17] = (byte) h;
        int p = 32;
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 4) {
                for (int i = 0; i < 16; i++) {  // (past the edge: the edge texel, as meleepack.gx_rgba8)
                    final int x = Math.min(bx + (i & 3), w - 1), y = Math.min(by + (i >> 2), h - 1);
                    final int o = (y * w + x) * 4;
                    out[p + i * 2] = rgba[o + 3];
                    out[p + i * 2 + 1] = rgba[o];
                    out[p + 32 + i * 2] = rgba[o + 1];
                    out[p + 32 + i * 2 + 1] = rgba[o + 2];
                }
                p += 64;
            }
        return out;
    }
}
