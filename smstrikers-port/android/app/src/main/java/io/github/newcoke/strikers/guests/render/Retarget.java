package io.github.newcoke.strikers.guests.render;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * retargetsim.guest_pose (src/platform/xr_guest.cpp retarget_pose on the PC tools' side), number for number: a
 * Strikers class's clip posing a retarget pack (GPK2) -> each guest node's world matrix (row vectors, the
 * Strikers model space: Z up, facing +X, feet at z 0).
 */
public final class Retarget {
    private static final int HB = Py.lowerHash("bip01"), HP = Py.lowerHash("bip01 pelvis");
    private static final Set<Integer> ARM_BONES = new HashSet<>();

    static {
        for (String sd : new String[] {"l", "r"})
            for (String p : new String[] {"upperarm", "forearm", "hand"}) ARM_BONES.add(Py.lowerHash("bip01 " + sd + " " + p));
    }

    private static final double[][] K3 = {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    private final Nl.Host host;
    private final Gpk pack;
    private final HostRef.Ref ref;
    private final double armScale;
    private final Nl.Clip clip;
    private final Map<Integer, Integer> hostIndex = new HashMap<>();
    private final Map<Integer, double[][]> align = new HashMap<>();
    public int mapped;

    /** ref: the class's hostref (null: none); clip: the class's clip (null: its bind, "BIND"). */
    public Retarget(Nl.Host host, String anim, Gpk pack, HostRef.Ref ref, double armScale) {
        this.host = host;
        this.pack = pack;
        this.ref = ref;
        this.armScale = armScale;
        this.clip = anim == null ? null : Nl.clip(host.sanim, anim);
        if (anim != null && clip == null) throw new IllegalArgumentException("no clip " + anim + " for " + host.cls);
        for (int i = 0; i < host.shier.ids.length; i++) hostIndex.put(host.shier.ids[i], i);
        final Map<Integer, double[][]> binds = host.binds;
        final Gpk.Node[] nodes = pack.nodes;
        for (String sd : new String[] {"l", "r"}) {
            final int[] hs = new int[3];
            final String[] parts = {"upperarm", "forearm", "hand"};
            for (int k = 0; k < 3; k++) hs[k] = Py.lowerHash("bip01 " + sd + " " + parts[k]);
            final int[] gi = new int[3];
            for (int k = 0; k < 3; k++) {
                gi[k] = -1;
                if (binds.containsKey(hs[k]) && hostIndex.containsKey(hs[k])) {
                    for (int i = 0; i < nodes.length; i++) {
                        if (nodes[i].map == hs[k]) {
                            gi[k] = i;
                            break;
                        }
                    }
                }
            }
            double[][] last = null;
            for (int k = 0; k < 3; k++) {
                if (gi[k] < 0) continue;
                double[][] a = null;
                if (k < 2 && gi[k + 1] >= 0 && binds.containsKey(hs[k + 1])) {
                    double[] dh = new double[3], dg = new double[3];
                    for (int c = 0; c < 3; c++) {
                        dh[c] = binds.get(hs[k + 1])[3][c] - binds.get(hs[k])[3][c];
                        dg[c] = nodes[gi[k + 1]].bindWorld[3][c] - nodes[gi[k]].bindWorld[3][c];
                    }
                    final double lh = Math.sqrt(Py.sum(dh[0] * dh[0], dh[1] * dh[1], dh[2] * dh[2]));
                    final double lg = Math.sqrt(Py.sum(dg[0] * dg[0], dg[1] * dg[1], dg[2] * dg[2]));
                    if (lh > 1e-5 && lg > 1e-5) {
                        for (int c = 0; c < 3; c++) {
                            dh[c] = dh[c] / lh;
                            dg[c] = dg[c] / lg;
                        }
                        final double[] v = {dg[1] * dh[2] - dg[2] * dh[1], dg[2] * dh[0] - dg[0] * dh[2],
                            dg[0] * dh[1] - dg[1] * dh[0]};
                        final double cs = Py.sum(dg[0] * dh[0], dg[1] * dh[1], dg[2] * dh[2]);
                        if (cs > -0.95) {
                            final double[][] kx = {{0, -v[2], v[1]}, {v[2], 0, -v[0]}, {-v[1], v[0], 0}};
                            final double[][] kk = Py.mul3(kx, kx);
                            final double[][] col = new double[3][3];
                            for (int r = 0; r < 3; r++)
                                for (int c = 0; c < 3; c++)
                                    col[r][c] = (r == c ? 1.0 : 0.0) + kx[r][c] + kk[r][c] / (1.0 + cs);
                            a = Py.transpose3(col);
                        }
                    }
                }
                if (a == null && k == 2) a = last;
                if (a != null) align.put(gi[k], a);
                last = a;
            }
        }
    }

    public int keys() {
        return clip == null ? 1 : clip.keys();
    }

    /** guest_pose at normalised time t: the guest nodes' world matrices (4 x 4 rows each). */
    public double[][][] pose(double t) {
        final Nl.Shier h = host.shier;
        final Map<Integer, double[][]> binds = host.binds;
        final double[][][] hw;
        if (clip == null) {
            hw = new double[h.ids.length][][];
            for (int i = 0; i < h.ids.length; i++)
                hw[i] = binds.containsKey(h.ids[i]) ? Py.mul(binds.get(h.ids[i]), Py.rz90()) : Py.ident();
            hw[0] = Py.ident();
        } else {
            hw = Nl.hostWorld(h, clip.at(t));
        }
        final Gpk.Node[] nodes = pack.nodes;
        final double[][] rootM = hw[0];
        final boolean haveRefs = ref != null && !ref.refs.isEmpty();
        final double[][][] world = new double[nodes.length][][];
        mapped = 0;
        for (int i = 0; i < nodes.length; i++) {
            final Gpk.Node n = nodes[i];
            final int par = n.parent;
            if (par < 0) {
                world[i] = Py.mul(Py.mul(n.bindWorld, Py.rz90()), rootM);
                continue;
            }
            final int m = n.map;
            final Integer hnI = m != 0 ? hostIndex.get(m) : null;
            final int hn = hnI != null ? hnI : -1;
            if (hn < 0 || !binds.containsKey(m)) {
                final int pm = nodes[par].map;
                if (haveRefs && (pm == HB || pm == HP) && binds.containsKey(pm) && hostIndex.containsKey(pm)) {
                    final double[][] w = Py.mul(Py.mul(n.bindWorld, Py.rz90()), rootM);
                    final double[] tl = n.bindLocal[3];
                    final double[][] pw = world[par];
                    final double[] pos = new double[4];
                    for (int c = 0; c < 3; c++) pos[c] = tl[0] * pw[0][c] + tl[1] * pw[1][c] + tl[2] * pw[2][c] + pw[3][c];
                    pos[3] = 1.0;
                    w[3] = pos;
                    world[i] = w;
                    continue;
                }
                world[i] = Py.mul(n.bindLocal, world[par]);
                continue;
            }
            mapped++;
            final double[][] bind = binds.get(m);
            final double[] hbsA = new double[1];
            final double[][] br = Py.rotOf(bind, hbsA);
            final double hbs = hbsA[0] > 1e-6 ? hbsA[0] : 1.0;
            final double[] gsc = new double[3];
            for (int row = 0; row < 3; row++) {
                final double[] r = n.bindWorld[row];
                gsc[row] = Math.sqrt(Py.sum(r[0] * r[0], r[1] * r[1], r[2] * r[2]));
            }
            double[][] hinv = Py.transpose3(br);
            if (ref != null && ref.refs.containsKey(m)) hinv = Py.mul3(K3, Py.transpose3(ref.refs.get(m)));
            if (align.containsKey(i)) hinv = Py.mul3(align.get(i), hinv);
            final double[][] hm = hw[hn];
            final double[] hsA = new double[1];
            final double[][] hr = Py.rotOf(hm, hsA);
            final double hs = hsA[0];
            final double[][] gr = Py.rotOf(n.bindWorld, null);
            final double[][] r = Py.mul3(gr, Py.mul3(hinv, hr));
            double[][] w = new double[4][];
            for (int row = 0; row < 3; row++) {
                w[row] = new double[4];
                for (int c = 0; c < 3; c++) w[row][c] = r[row][c] * gsc[row] * hs / hbs;
                w[row][3] = 0.0;
            }
            if (armScale != 1.0 && ARM_BONES.contains(m)) {
                for (int row = 0; row < 3; row++)
                    for (int c = 0; c < 3; c++) w[row][c] = w[row][c] * armScale;
            }
            final double[] pos = new double[4];
            if (m == HB || m == HP) {
                final Double hzD = ref != null ? ref.hips.get(m) : null;
                final double hz = hzD != null ? hzD : bind[3][2];
                final double k = Math.abs(hz) > 1e-4 ? n.bindWorld[3][2] / hz : 1.0;
                for (int c = 0; c < 3; c++) pos[c] = rootM[3][c] + (hm[3][c] - rootM[3][c]) * k;
                pos[2] = rootM[3][2] + hz * k + (hm[3][2] - rootM[3][2] - hz) * Math.min(k, 1.0);
            } else {
                final double[] tl = n.bindLocal[3];
                final double[][] pw = world[par];
                for (int c = 0; c < 3; c++) pos[c] = tl[0] * pw[0][c] + tl[1] * pw[1][c] + tl[2] * pw[2][c] + pw[3][c];
            }
            pos[3] = 1.0;
            w[3] = pos;
            world[i] = w;
        }
        return world;
    }
}
