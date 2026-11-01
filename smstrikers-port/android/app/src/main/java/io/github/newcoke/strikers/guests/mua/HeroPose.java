package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.render.Gpk;
import io.github.newcoke.strikers.guests.render.HostRef;
import io.github.newcoke.strikers.guests.render.Nl;
import io.github.newcoke.strikers.guests.render.Py;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * retargetsim.guest_pose as hero_portraits.py ran it: a hero pack posed by its slot's Strikers clip (render.Retarget's
 * math), and r124's own standing turn when hostref_all is given (HOSTREF_ALL: each bone turned the way the Strikers
 * bone turns from ITS standing pose, starting from the hero's own idle, the hips at its own idle's height).
 */
final class HeroPose {
    private static final int HB = Py.lowerHash("bip01"), HP = Py.lowerHash("bip01 pelvis");
    private static final Set<Integer> ARM_BONES = new HashSet<>();

    static {
        for (String sd : new String[] {"l", "r"})
            for (String p : new String[] {"upperarm", "forearm", "hand"}) ARM_BONES.add(Py.lowerHash("bip01 " + sd + " " + p));
    }

    private static final double[][] K3 = {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};

    private final Nl.Host host;
    private final Gpk pack;
    private final HostRef.Ref ref, refAll;
    private final double armScale;
    private final Nl.Clip clip;
    private final Map<Integer, Integer> hostIndex = new HashMap<>();
    private final Map<Integer, double[][]> align = new HashMap<>();
    /** own_stand: per node {3 x 3 rotation rows, position}, null without hostref_all or own_idle. */
    private final double[][][] standR;
    private final double[][] standT;

    HeroPose(Nl.Host host, String anim, Gpk pack, HostRef.Ref ref, HostRef.Ref refAll, String standClip, double armScale) {
        this.host = host;
        this.pack = pack;
        this.ref = ref;
        this.armScale = armScale;
        this.clip = Nl.clip(host.sanim, anim);
        if (clip == null) throw new IllegalArgumentException("no clip " + anim + " for " + host.cls);
        for (int i = 0; i < host.shier.ids.length; i++) hostIndex.put(host.shier.ids[i], i);
        final Map<Integer, double[][]> binds = host.binds;
        final Gpk.Node[] nodes = pack.nodes;
        // own_stand (r124)
        double[][][] sr = null;
        double[][] st = null;
        if (refAll != null && standClip != null) {
            final Gpk.Anim a = pack.anim(standClip);
            if (a != null) {
                final int nn = nodes.length;
                final double[][][] world = new double[nn][][];
                for (int i = 0; i < nn; i++) {
                    final int at = a.at + i * 28;
                    final double[] v = new double[7];
                    for (int k = 0; k < 7; k++) v[k] = Float.intBitsToFloat(le32(pack.b, at + 4 * k));
                    final double[][] local = Py.quatRows(v[0], v[1], v[2], v[3]);
                    local[3] = new double[] {v[4], v[5], v[6], 1.0};
                    final int par = nodes[i].parent;
                    world[i] = par >= 0 && par < i ? Py.mul(local, world[par]) : Py.ident();
                }
                sr = new double[nn][][];
                st = new double[nn][];
                for (int i = 0; i < nn; i++) {
                    sr[i] = new double[][] {{world[i][0][0], world[i][0][1], world[i][0][2]},
                        {world[i][1][0], world[i][1][1], world[i][1][2]}, {world[i][2][0], world[i][2][1], world[i][2][2]}};
                    st[i] = new double[] {world[i][3][0], world[i][3][1], world[i][3][2]};
                }
            }
        }
        standR = sr;
        standT = st;
        this.refAll = sr != null ? refAll : null;
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

    private static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    /** guest_pose at normalised time t: the guest nodes' world matrices (4 x 4 rows each). */
    double[][][] pose(double t) {
        final Map<Integer, double[][]> binds = host.binds;
        final double[][][] hw = Nl.hostWorld(host.shier, clip.at(t));
        final Gpk.Node[] nodes = pack.nodes;
        final double[][] rootM = hw[0];
        final boolean haveRefs = ref != null && !ref.refs.isEmpty();
        final double[][][] world = new double[nodes.length][][];
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
            final double[][] r;
            if (standR != null && refAll.refs.containsKey(m)) {
                r = Py.mul3(standR[i], Py.mul3(Py.transpose3(refAll.refs.get(m)), hr));
            } else {
                r = Py.mul3(gr, Py.mul3(hinv, hr));
            }
            final double[][] w = new double[4][];
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
                double hz = hzD != null ? hzD : bind[3][2];
                double k = Math.abs(hz) > 1e-4 ? n.bindWorld[3][2] / hz : 1.0;
                if (standR != null && refAll.hips.containsKey(m) && Math.abs(refAll.hips.get(m)) > 1e-4) {
                    hz = refAll.hips.get(m);
                    k = standT[i][2] / hz;
                }
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
