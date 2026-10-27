package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.math.BigDecimal;
import java.math.RoundingMode;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * guests/hostref.txt (tools/outside/hostref.py, the same numbers and text): each Strikers character's own
 * standing pose (its "idle" clip, first key) for the trunk bones, relative to the hierarchy's root node, as
 * the retarget's reference for a guest standing in for it (xr_guest.cpp read_hostref). From the Strikers disc's
 * art/animation/<Class>.shier + .sanim.
 */
final class HostRef {
    private HostRef() {}

    static final String[][] CLASSES = {{"Birdo", "birdo"}, {"Daisy", "daisy"}, {"DonkeyKong", "donkeykong"},
            {"HammerBro", "hammerbros"}, {"Koopa", "koopa"}, {"Luigi", "luigi"}, {"Mario", "mario"}, {"Peach", "peach"},
            {"Toad", "toad"}, {"Waluigi", "waluigi"}, {"Wario", "wario"}, {"Yoshi", "yoshi"}};
    static final String[] TRUNK = {"bip01", "bip01 pelvis", "bip01 spine", "bip01 spine1", "bip01 spine2", "bip01 spine3",
            "bip01 neck", "bip01 head"};

    /** The text file's bytes. */
    static byte[] make(Source strikers) throws IOException {
        final StringBuilder out = new StringBuilder();
        out.append("# Quest 3 Strikers guest retarget: each Strikers character's idle (first key), trunk bones\n");
        out.append("# relative to the root node (tools/outside/hostref.py). class bone r00 r01 r02 r10 ... r22 x y z\n");
        for (String[] c : CLASSES) {
            final byte[] sh = strikers.read("art/animation/" + c[0] + ".shier");
            final byte[] sa = strikers.read("art/animation/" + c[0] + ".sanim");
            final Skeleton h = skeleton(sh);
            final double[][][] hw = idleWorld(h, sa);
            final double[][] root = rotOf(hw[0]);
            final double[][] rootT = new double[3][3];
            for (int r = 0; r < 3; r++)
                for (int k = 0; k < 3; k++) rootT[r][k] = root[k][r];
            for (String name : TRUNK) {
                final int nid = Res.nameHash(name.toLowerCase());
                int at = -1;
                for (int i = 0; i < h.ids.length && at < 0; i++) if (h.ids[i] == nid) at = i;
                if (at < 0) continue;
                final double[][] bw = hw[at];
                final double[][] r = rotOf(bw);
                final double[][] rel = mul(r, rootT, 3);
                final double[] d = new double[3];
                for (int k = 0; k < 3; k++) d[k] = bw[3][k] - hw[0][3][k];
                out.append(c[1]).append(' ').append(String.format("%08x", nid));
                for (int a = 0; a < 3; a++)
                    for (int b = 0; b < 3; b++) out.append(' ').append(f6(rel[a][b]));
                for (int k = 0; k < 3; k++) {
                    double s = 0;  // sum(d[c] * rootT[c][k])
                    for (int cc = 0; cc < 3; cc++) s += d[cc] * rootT[cc][k];
                    out.append(' ').append(f6(s));
                }
                out.append('\n');
            }
        }
        return out.toString().getBytes(StandardCharsets.US_ASCII);
    }

    /** Python's "%.6f" (the exact value rounded half-even; a minus sign kept on a rounded-away negative). */
    static String f6(double v) {
        final boolean neg = v < 0 || (v == 0 && 1 / v < 0);
        final String s = new BigDecimal(Math.abs(v)).setScale(6, RoundingMode.HALF_EVEN).toPlainString();
        return neg ? "-" + s : s;
    }

    // ------------------------------------------------------------------ NL chunks (shier.chunks, Strikers)

    /** {id & 0x80FFFFFF, data start, end} of each chunk in [start, end). */
    static List<int[]> chunks(byte[] b, int start, int end) {
        final List<int[]> out = new ArrayList<>();
        int p = start;
        while (p + 8 <= end) {
            final int cid = Bin.u32(b, p), size = Bin.u32(b, p + 4);
            final int a = (cid & 0x7F000000) >>> 24;
            final int ds = a != 0 ? (p + (1 << a) + 7) & ~((1 << a) - 1) : p + 8;
            out.add(new int[] {cid & 0x80FFFFFF, ds, p + 8 + size});
            p = p + 8 + size;
        }
        return out;
    }

    static final class Skeleton {
        int[] ids, parents;
        float[][] trans;
    }

    /** shier.read: node ids, parents, bind translations. */
    static Skeleton skeleton(byte[] b) throws IOException {
        final int[] top = chunks(b, 0, b.length).get(0);
        final List<int[]> parts = chunks(b, top[1], top[2]);
        final int[] head = parts.get(0);
        final int n = Bin.u32(b, head[1] + 8);
        final Skeleton s = new Skeleton();
        s.ids = new int[n];
        s.parents = new int[n];
        for (int i = 0; i < n; i++) {
            s.ids[i] = Bin.u32(b, parts.get(2)[1] + 4 * i);
            s.parents[i] = Bin.u32(b, parts.get(3)[1] + 4 * i);
        }
        for (int[] p : parts) {
            if (Math.min(p[2], b.length) - p[1] == n * 12) {
                s.trans = new float[n][3];
                for (int i = 0; i < n; i++)
                    for (int k = 0; k < 3; k++) s.trans[i][k] = Bin.f32(b, p[1] + i * 12 + 4 * k);
            }
        }
        if (s.trans == null) throw new IOException("shier: no bind translations");
        return s;
    }

    /** idle_world: world 4 x 4 (row-vector) per node of the idle clip's first key. */
    static double[][][] idleWorld(Skeleton h, byte[] b) throws IOException {
        // retargetsim.sanim_pose(path, "idle", 0.0)
        final List<int[]> clips = new ArrayList<>();
        for (int[] c : chunks(b, 0, b.length)) {
            if (c[0] == 0x80017000) clips.add(c);
            else if ((c[0] & 0x80000000) != 0)
                for (int[] s : chunks(b, c[1], c[2])) if (s[0] == 0x80017000) clips.add(s);
        }
        double[][] q = null;
        float[][] tr = null;
        for (int[] clip : clips) {
            final List<int[]> sub = chunks(b, clip[1], clip[2]);
            final String name = Bin.cstr(b, sub.get(1)[1], sub.get(1)[2] - sub.get(1)[1]);
            if (!name.equalsIgnoreCase("idle")) continue;
            final int[] hdr = sub.get(0);
            final int nkeys = Bin.u32(b, hdr[1] + 8), nnodes = Bin.u32(b, hdr[1] + 12);
            final List<int[]> nodes = new ArrayList<>();
            for (int[] s : sub) if (s[0] == 0x80017100) nodes.add(s);
            final int[] props = new int[nnodes];
            for (int i = 0; i < nnodes; i++) props[i] = Bin.u32(b, sub.get(sub.size() - 1)[1] + 4 * i);
            final int k = Math.min(0, nkeys - 1);
            q = new double[nodes.size()][];
            tr = new float[nodes.size()][];
            for (int i = 0; i < nodes.size(); i++) {
                final int pr = i < props.length ? props[i] : 0;
                for (int[] s : chunks(b, nodes.get(i)[1], nodes.get(i)[2])) {
                    if (s[0] == 0x17101) {
                        final int kk = (pr & 2) != 0 ? 0 : k;
                        if ((pr & 1) != 0) {
                            final double a = Bin.u16(b, s[1] + 2 * kk) * 2 * Math.PI / 65536.0;
                            q[i] = new double[] {0.0, 0.0, StrictMath.sin(a / 2), StrictMath.cos(a / 2)};
                        } else {
                            q[i] = new double[4];
                            for (int j = 0; j < 4; j++) q[i][j] = Bin.s16(b, s[1] + 8 * kk + 2 * j) / 16384.0;
                        }
                    } else if (s[0] == 0x17102) {
                        final int kk = (pr & 4) != 0 ? 0 : k;
                        tr[i] = new float[3];
                        for (int j = 0; j < 3; j++) tr[i][j] = Bin.f32(b, s[1] + 12 * kk + 4 * j);
                    }
                }
            }
            break;
        }
        if (q == null) throw new IOException("sanim: no idle clip");
        final int n = h.parents.length;
        final double[][][] hw = new double[n][][];
        for (int i = 0; i < n; i++) {
            final double[] qi = i < q.length ? q[i] : null;
            final float[] ti = i < tr.length ? tr[i] : null;
            final double[][] local = qi != null ? quatRows(qi[0], qi[1], qi[2], qi[3]) : identity();
            final float[] tx = ti != null ? ti : h.trans[i];
            local[3] = new double[] {tx[0], tx[1], tx[2], 1.0};
            hw[i] = h.parents[i] >= 0 ? mul(local, hw[h.parents[i]], 4) : local;
        }
        return hw;
    }

    private static double[][] identity() {
        return new double[][] {{1.0, 0, 0, 0}, {0, 1.0, 0, 0}, {0, 0, 1.0, 0}, {0, 0, 0, 1.0}};
    }

    /** retargetsim.quat_rows: row-vector rotation of (x, y, z, w). */
    static double[][] quatRows(double x, double y, double z, double w) {
        double n = Math.sqrt(x * x + y * y + z * z + w * w);
        if (n == 0) n = 1.0;
        x = x / n;
        y = y / n;
        z = z / n;
        w = w / n;
        return new double[][] {{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0},
                {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0},
                {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0}, {0.0, 0.0, 0.0, 1.0}};
    }

    /** a b (n x n), each entry summed from 0 first to last (Python's sum). */
    static double[][] mul(double[][] a, double[][] b, int n) {
        final double[][] o = new double[n][n];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                double s = 0;
                for (int k = 0; k < n; k++) s += a[i][k] * b[k][j];
                o[i][j] = s;
            }
        return o;
    }

    /** retargetsim.rot_of: rows 0-2 unit length. */
    static double[][] rotOf(double[][] m) {
        final double[][] r = new double[3][3];
        for (int row = 0; row < 3; row++) {
            double s = 0;
            for (int c = 0; c < 3; c++) s += m[row][c] * m[row][c];
            final double ln = Math.sqrt(s);
            for (int c = 0; c < 3; c++) r[row][c] = ln > 1e-12 ? m[row][c] / ln : 0.0;
        }
        return r;
    }
}
