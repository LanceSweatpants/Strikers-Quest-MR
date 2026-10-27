package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

/**
 * hostref.py: each Strikers character's standing pose (its "idle" clip, first key) for the trunk bones, relative to
 * the root: the retarget's reference (guests/hostref.txt, xr_guest.cpp retarget_pose). Made from the player's
 * own Strikers disc, as the text the PC tool writes (the numbers go through '%.6f', as the poses read them back).
 */
public final class HostRef {
    private HostRef() {}

    static final String[][] CLASSES = {{"Birdo", "birdo"}, {"Daisy", "daisy"}, {"DonkeyKong", "donkeykong"},
        {"HammerBro", "hammerbros"}, {"Koopa", "koopa"}, {"Luigi", "luigi"}, {"Mario", "mario"}, {"Peach", "peach"},
        {"Toad", "toad"}, {"Waluigi", "waluigi"}, {"Wario", "wario"}, {"Yoshi", "yoshi"}};
    static final String[] TRUNK = {"bip01", "bip01 pelvis", "bip01 spine", "bip01 spine1", "bip01 spine2",
        "bip01 spine3", "bip01 neck", "bip01 head"};

    /** The whole hostref.txt (all 12 classes). */
    public static String text(Source strikers) throws IOException {
        final StringBuilder sb = new StringBuilder();
        sb.append("# Quest 3 Strikers guest retarget: each Strikers character's idle (first key), trunk bones\n");
        sb.append("# relative to the root node (tools/outside/hostref.py). class bone r00 r01 r02 r10 ... r22 x y z\n");
        for (String[] c : CLASSES) {
            final String lc = c[0].toLowerCase(java.util.Locale.ROOT);
            final Nl.Shier h = Nl.shier(strikers.read("art/animation/" + lc + ".shier"));
            sb.append(lines(h, strikers.read("art/animation/" + lc + ".sanim"), c[1]));
        }
        return sb.toString();
    }

    /** One class's lines. */
    static String lines(Nl.Shier h, byte[] sanim, String cname) {
        final Nl.Clip clip = Nl.clip(sanim, "idle");
        final double[][][] hw = Nl.hostWorld(h, clip != null ? clip.at(0.0) : null);
        final double[][] root = Py.rotOf(hw[0], null);
        final double[][] rootT = Py.transpose3(root);
        final StringBuilder sb = new StringBuilder();
        for (String name : TRUNK) {
            final int nid = Py.lowerHash(name);
            final int idx = h.indexOf(nid);
            if (idx < 0) continue;
            final double[][] bw = hw[idx];
            final double[][] r = Py.rotOf(bw, null);
            final double[][] rel = Py.mul3(r, rootT);
            final double[] d = new double[3];
            for (int c = 0; c < 3; c++) d[c] = bw[3][c] - hw[0][3][c];
            sb.append(cname).append(' ').append(String.format("%08x", nid));
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) sb.append(' ').append(Py.f6(rel[a][b]));
            for (int k = 0; k < 3; k++)
                sb.append(' ').append(Py.f6(Py.sum(d[0] * rootT[0][k], d[1] * rootT[1][k], d[2] * rootT[2][k])));
            sb.append('\n');
        }
        return sb.toString();
    }

    /** retargetsim.read_hostref for one class: trunk references (3 x 3) and the hips' standing heights. */
    public static final class Ref {
        public final Map<Integer, double[][]> refs = new HashMap<>();
        public final Map<Integer, Double> hips = new HashMap<>();
    }

    public static Ref read(String text, String cls) {
        String want = cls.toLowerCase(java.util.Locale.ROOT);
        if (want.equals("hammerbro")) want = "hammerbros";
        final Ref r = new Ref();
        for (String line : text.split("\n")) {
            final String[] p = line.trim().split("\\s+");
            if (p.length >= 11 && !p[0].startsWith("#") && p[0].equals(want)) {
                final double[] v = new double[p.length - 2];
                for (int i = 0; i < v.length; i++) v[i] = Double.parseDouble(p[i + 2]);
                final int id = (int) Long.parseLong(p[1], 16);
                r.refs.put(id, new double[][] {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {v[6], v[7], v[8]}});
                if (v.length >= 12) r.hips.put(id, v[11]);
            }
        }
        return r;
    }
}
