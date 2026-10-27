package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Strikers' own character files off its disc, as the PC tools read them (shier.py, retargetsim.py): the skeleton
 * (art/animation/<class>.shier), the clips (.sanim) and the skin's bind pose (art/characters/<class>/<class>.glg).
 * Numbers stay doubles (the f32s as read), the way Python holds them.
 */
public final class Nl {
    private Nl() {}

    /** One chunk: id (& 0x80FFFFFF), where its data starts and where it ends. */
    static final class Chunk {
        final int id, ds, end;

        Chunk(int id, int ds, int end) {
            this.id = id;
            this.ds = ds;
            this.end = end;
        }
    }

    /** shier.chunks (Strikers, not Charged). */
    static List<Chunk> chunks(byte[] b, int start, int end) {
        final List<Chunk> out = new ArrayList<>();
        int p = start;
        while (p + 8 <= end) {
            final int cid = Bin.u32(b, p), size = Bin.u32(b, p + 4);
            final int a = (cid & 0x7F000000) >>> 24;
            final int ds = a != 0 ? ((p + (1 << a) + 7) & ~((1 << a) - 1)) : p + 8;
            out.add(new Chunk(cid & 0x80FFFFFF, ds, p + 8 + size));
            p = p + 8 + size;
        }
        return out;
    }

    /** A skeleton: node ids (lower hashes of the bone names), parents, rest translations. */
    public static final class Shier {
        public final int[] ids, parents;
        public final double[][] trans;

        Shier(int[] ids, int[] parents, double[][] trans) {
            this.ids = ids;
            this.parents = parents;
            this.trans = trans;
        }

        public int indexOf(int id) {
            for (int i = 0; i < ids.length; i++)
                if (ids[i] == id) return i;
            return -1;
        }
    }

    public static Shier shier(byte[] b) {
        final Chunk top = chunks(b, 0, b.length).get(0);
        final List<Chunk> parts = chunks(b, top.ds, top.end);
        final int n = Bin.u32(b, parts.get(0).ds + 8);
        final int[] ids = new int[n], parents = new int[n];
        for (int i = 0; i < n; i++) {
            ids[i] = Bin.u32(b, parts.get(2).ds + 4 * i);
            parents[i] = Bin.u32(b, parts.get(3).ds + 4 * i);
        }
        double[][] trans = null;
        for (Chunk c : parts) {
            if (c.end - c.ds == n * 12) {
                trans = new double[n][3];
                for (int i = 0; i < n; i++)
                    for (int k = 0; k < 3; k++) trans[i][k] = Bin.f32(b, c.ds + 12 * i + 4 * k);
            }
        }
        return new Shier(ids, parents, trans);
    }

    /** One clip at one key: per node its rotation (x y z w, or null) and translation (or null). */
    public static final class Pose {
        public final double[][] quat, trans;
        public final int keys;

        Pose(double[][] quat, double[][] trans, int keys) {
            this.quat = quat;
            this.trans = trans;
            this.keys = keys;
        }
    }

    /** The clip's raw records, found once (sanim_pose's walk), to be read at any key. */
    public static final class Clip {
        final byte[] b;
        final int keys, nodes;
        final List<Chunk> nodeChunks;
        final int[] props;

        Clip(byte[] b, int keys, int nodes, List<Chunk> nodeChunks, int[] props) {
            this.b = b;
            this.keys = keys;
            this.nodes = nodes;
            this.nodeChunks = nodeChunks;
            this.props = props;
        }

        public int keys() {
            return keys;
        }

        /** retargetsim.sanim_pose at normalised time t: the key int(t * (keys - 1)). */
        public Pose at(double t) {
            final int k = Math.min((int) (t * (keys - 1)), keys - 1);
            return key(k);
        }

        public Pose key(int k) {
            final int n = nodeChunks.size();
            final double[][] q = new double[n][], tr = new double[n][];
            for (int i = 0; i < n; i++) {
                final Chunk nc = nodeChunks.get(i);
                final int pr = i < props.length ? props[i] : 0;
                for (Chunk sc : chunks(b, nc.ds, nc.end)) {
                    if (sc.id == 0x17101) {
                        final int kk = (pr & 2) != 0 ? 0 : k;
                        if ((pr & 1) != 0) {
                            final double a = ((double) (Bin.u16(b, sc.ds + 2 * kk) * 2) * Math.PI) / 65536.0;
                            q[i] = new double[] {0.0, 0.0, Trig.sin(a / 2), Trig.cos(a / 2)};
                        } else {
                            q[i] = new double[4];
                            for (int c = 0; c < 4; c++) q[i][c] = Bin.s16(b, sc.ds + 8 * kk + 2 * c) / 16384.0;
                        }
                    } else if (sc.id == 0x17102) {
                        final int kk = (pr & 4) != 0 ? 0 : k;
                        tr[i] = new double[3];
                        for (int c = 0; c < 3; c++) tr[i][c] = Bin.f32(b, sc.ds + 12 * kk + 4 * c);
                    }
                }
            }
            return new Pose(q, tr, keys);
        }

        /** True when any node turns about Z only (the sin / cos path). */
        public boolean usesAngles() {
            for (int i = 0; i < nodeChunks.size() && i < props.length; i++)
                if ((props[i] & 1) != 0) return true;
            return false;
        }
    }

    /** The named clip (any case), or null. */
    public static Clip clip(byte[] b, String anim) {
        final List<Chunk> clips = new ArrayList<>();
        for (Chunk c : chunks(b, 0, b.length)) {
            if (c.id == 0x80017000) {
                clips.add(c);
            } else if ((c.id & 0x80000000) != 0) {
                for (Chunk s : chunks(b, c.ds, c.end))
                    if (s.id == 0x80017000) clips.add(s);
            }
        }
        for (Chunk c : clips) {
            final List<Chunk> sub = chunks(b, c.ds, c.end);
            final String name = Bin.cstr(b, sub.get(1).ds, sub.get(1).end - sub.get(1).ds);
            if (!name.toLowerCase(Locale.ROOT).equals(anim.toLowerCase(Locale.ROOT))) continue;
            final Chunk hdr = sub.get(0);
            final int keys = Bin.u32(b, hdr.ds + 8), nodes = Bin.u32(b, hdr.ds + 12);
            final List<Chunk> nodeChunks = new ArrayList<>();
            for (Chunk s : sub)
                if (s.id == 0x80017100) nodeChunks.add(s);
            final int[] props = new int[nodes];
            final Chunk last = sub.get(sub.size() - 1);
            for (int i = 0; i < nodes; i++) props[i] = Bin.u32(b, last.ds + 4 * i);
            return new Clip(b, keys, nodes, nodeChunks, props);
        }
        return null;
    }

    /** retargetsim.host_bind: {bone id: bind world matrix} from the .glg skin records (0x1B00A), first wins. */
    public static Map<Integer, double[][]> hostBind(byte[] b) {
        final Map<Integer, double[][]> out = new HashMap<>();
        walkBind(b, 0, b.length, out);
        return out;
    }

    private static void walkBind(byte[] b, int s, int e, Map<Integer, double[][]> out) {
        for (Chunk c : chunks(b, s, e)) {
            if (c.id == 0x1B00A) {
                for (int k = 0; k < (c.end - c.ds) / 0x44; k++) {
                    final int at = c.ds + k * 0x44;
                    final int bone = Bin.u32(b, at);
                    if (out.containsKey(bone)) continue;
                    final double[][] m = new double[4][4];
                    for (int i = 0; i < 16; i++) m[i / 4][i % 4] = Bin.f32(b, at + 4 + 4 * i);
                    out.put(bone, m);
                }
            } else if ((c.id & 0x80000000) != 0) {
                walkBind(b, c.ds, c.end, out);
            }
        }
    }

    /** The host's world matrices for one pose (hostref.idle_world / guest_pose's accumulator). */
    public static double[][][] hostWorld(Shier h, Pose pose) {
        final int n = h.parents.length;
        final double[][][] hw = new double[n][][];
        for (int i = 0; i < n; i++) {
            final double[] q = pose != null && i < pose.quat.length ? pose.quat[i] : null;
            final double[] tr = pose != null && i < pose.trans.length ? pose.trans[i] : null;
            final double[][] local = q != null ? Py.quatRows(q[0], q[1], q[2], q[3]) : Py.ident();
            final double[] tx = tr != null ? tr : h.trans[i];
            local[3] = new double[] {tx[0], tx[1], tx[2], 1.0};
            final int par = h.parents[i];
            hw[i] = par >= 0 ? Py.mul(local, hw[par]) : local;
        }
        return hw;
    }

    /** A Strikers class's files on its disc ("Mario" -> art/animation/mario.shier ...). */
    public static final class Host {
        public final String cls;
        public final Shier shier;
        public final byte[] sanim;
        public final Map<Integer, double[][]> binds;

        public Host(Source strikers, String cls) throws IOException {
            this.cls = cls;
            final String lc = cls.toLowerCase(Locale.ROOT);
            shier = shier(strikers.read("art/animation/" + lc + ".shier"));
            sanim = strikers.read("art/animation/" + lc + ".sanim");
            binds = hostBind(strikers.read("art/characters/" + lc + "/" + lc + ".glg"));
        }
    }

    static String ascii(byte[] b, int o, int n) {
        return new String(b, o, n, StandardCharsets.ISO_8859_1);
    }
}
