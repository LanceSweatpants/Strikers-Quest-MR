package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Png;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;

/**
 * strikers-quest r137 (Lance OK'd "the cover star rendered big on the headset when each issue comes out"): a character's
 * BIG picture, for STRIKERS ILLUSTRATED's covers and pages and the trading cards, instead of its 256 x 256 portrait
 * stretched 3-4x. Its own model (the pack the 3D figures use) standing in the first frame of its idle (the pose file the
 * hub's 3D figures use: guests/evo/<pack>.gev, or a home figure's loop file's first idle move), drawn by Portrait just
 * as the portraits are (portrait_render.py's --full --turn -70: orthographic, a little from the right, the whole figure,
 * the key and rim lights and the sky, the same numbers as assets/guestdata/render/portrait.txt), only big: size x size,
 * 2 x 2 samples a pixel, the light's rays on a grid of a cell per (size / 256)^2 pixels (as many rays as a portrait's)
 * smoothed onto the pixels, on two low-priority threads. Transparent background, straight alpha.
 * <p>
 * Called by the game's native code through JNI (src/platform/xr_big.cpp, on its own worker thread), so it is the same on
 * every headset and only needs the packs on it (Lance's PC-made ones, or the on-headset converter's). PC check:
 * tools/guestconv (BigTest: the same call on the PC's copies of the files).
 */
public final class Big {
    private Big() {}

    /** The look: portrait.txt's numbers (the portraits' fit to Blender's Cycles), sampled for a big picture. */
    public static Portrait.Look look(int size) {
        final Portrait.Look l = new Portrait.Look();
        l.skyRgb = new double[] {0.275, 0.3, 0.35};
        l.sky = 1.123;
        l.bounce = 0.717;
        l.key = 0.0966;
        l.rim = 0.0963;
        l.spec = 0.0;
        l.roughness = 0.7;
        l.keyFull = 380;
        l.rimFull = 220;
        l.keyBust = 900;
        l.rimBust = 500;
        l.skyRays = 16;
        l.lightRays = 9;
        l.vertexSkyRays = 12;
        l.vertexLightRays = 4;
        l.denoise = 2;
        l.exposure = 1.0;
        l.flames = true;  // (a Charmander's tail flame: the 3D figure shows it too)
        l.ss = size <= 256 ? 4 : 2;
        l.lightDiv = Math.max(1, size / 256);
        l.threads = 2;
        return l;
    }

    /**
     * The picture (straight RGBA, size x size), also saved as a PNG at `png` (null: not saved); null when the pack and
     * pose file don't fit together or can't be read.
     */
    public static byte[] render(String gpk, String gev, int size, String png) {
        return draw(gpk, gev, size, png, false);
    }

    /**
     * r137 (Lance: every character "posed mid-celebration or mid-shot, not standing in idle"): the same, in its action pose
     * from its own motion. r144 (Lance: "contorted as fuck or dont show their eyes"): the key the reviewed list gives it
     * (STRIKERS_HOME/franchise/big_poses.txt, every one looked at on the PC), else its idle - the portraits' pose. home:
     * STRIKERS_HOME (the list, the roster, the arm sizes); data: the game's disc image or files folder (STRIKERS_DATA; ""
     * to look in home) for its classes' clips.
     */
    public static byte[] renderAction(String gpk, String gev, int size, String png, String home, String data) {
        BigPose.setup(home, data);
        return draw(gpk, gev, size, png, true);
    }

    /** The PC check (tools/guestconv BigTest): the Strikers files, guests/roster.txt and guests/limbs.txt as text. */
    public static void configure(io.github.newcoke.strikers.guests.Source strikers, String roster, String limbs) {
        BigPose.configure(strikers, roster, limbs);
    }

    /** The last picture's pose (BigTest's sheets). */
    public static volatile String lastPose = "";

    /** The PC check: BigPose prints every candidate it measures. */
    public static void debug(boolean on) {
        BigPose.debug = on;
    }

    /** The PC check: the first candidate key whose label has this in it (null: BigPose's choice). */
    public static void force(String label) {
        BigPose.force = label;
    }

    /** r144, the PC check: the reviewed list (assets/franchise/big_poses.txt) as text, as a headset reads it. */
    public static void poses(String text) {
        BigPose.poses(text);
    }

    /** r144, the PC check: BigPose's chooser proposes (for the review) instead of following the list. */
    public static void propose(boolean on) {
        BigPose.propose = on;
    }

    /** r144b, the PC check: this camera ({yaw, pitch}) instead of the list's (null: the list's / the default). */
    public static void view(double[] v) {
        viewForce = v;
    }

    private static volatile double[] viewForce;

    /**
     * r144b: the mesh turned for a big picture's own camera (big_poses.txt's "view <yaw> <pitch>"): about the vertical by yaw
     * (as Portrait.Scene's turn), then tipped towards the camera (it looks along +Y) by pitch - seen from above.
     */
    static void turn(Mesh m, double yawDeg, double pitchDeg) {
        final double a = Math.toRadians(yawDeg), cs = Math.cos(a), sn = Math.sin(a);
        final double b = Math.toRadians(pitchDeg), cb = Math.cos(b), sb = Math.sin(b);
        for (int i = 0; i < m.nv; i++) {
            for (float[] v : new float[][] {m.p, m.n}) {
                final double x = v[i * 3], y = v[i * 3 + 1], z = v[i * 3 + 2];
                final double x1 = x * cs - y * sn, y1 = x * sn + y * cs;
                v[i * 3] = (float) x1;
                v[i * 3 + 1] = (float) (y1 * cb - z * sb);
                v[i * 3 + 2] = (float) (y1 * sb + z * cb);
            }
        }
    }

    static byte[] draw(String gpk, String gev, int size, String png, boolean action) {
        try {
            final long t0 = System.nanoTime();
            size = Math.max(32, Math.min(1024, size));
            final Gpk pack = new Gpk(Files.readAllBytes(new File(gpk).toPath()));
            final byte[] g = Files.readAllBytes(new File(gev).toPath());
            double[][][] world = pose(pack, g);
            if (world == null) return null;
            String label = "idle";
            double[] view = null;
            if (action) {
                String name = new File(gpk).getName();
                if (name.endsWith(".gpk")) name = name.substring(0, name.length() - 4);
                final BigPose.Choice ch = BigPose.choose(pack, name, g, world);
                world = ch.world;
                label = ch.label;
                view = viewForce != null ? viewForce : BigPose.view(name);
            }
            lastPose = label + (view != null ? String.format(java.util.Locale.ROOT, ", view %.0f %.0f", view[0], view[1]) : "");
            final Portrait.Look look = look(size);
            final Mesh m = Mesh.skinned(pack, world, look.flames);
            if (m.nt == 0) return null;
            if (action) {  // its lowest point on the floor (a kick's foot, a jump: the picture has no floor to go through)
                float lo = Float.MAX_VALUE;
                for (int i = 0; i < m.nv; i++) lo = Math.min(lo, m.p[i * 3 + 2]);
                for (int i = 0; i < m.nv; i++) m.p[i * 3 + 2] -= lo;
            } else {
                // xr_evo's pose: nothing under the floor it stands on (a cape folded round a kneel lies on it)
                for (int i = 0; i < m.nv; i++)
                    if (m.p[i * 3 + 2] < 0) m.p[i * 3 + 2] = 0;
            }
            final Portrait.Scene s;
            if (view == null) {
                s = Portrait.scene(m, size, size, true, -70, look);  // (the portraits' camera)
            } else {  // r144b: its own camera (turned here; the scene's own turn left out)
                turn(m, view[0], view[1]);
                s = Portrait.scene(m, size, size, true, Double.NaN, look);
            }
            final byte[] rgba = Portrait.render(s, m, look);
            if (png != null) Png.write(new File(png), rgba, size, size);
            System.out.println("Big: " + new File(gpk).getName() + " " + size + " px in " + (System.nanoTime() - t0) / 1000000
                    + " ms (" + m.nv + " vertices, " + m.nt + " triangles), pose: " + label);
            return rgba;
        } catch (Throwable e) {
            System.out.println("Big: " + gpk + ": " + e);
            return null;
        }
    }

    /** Frame f of a pose file (each node's world matrix, rows), as pose() reads one. */
    static double[][][] frame(Gpk pack, ByteBuffer b, int f) {
        final int nodes = pack.nodes.length;
        final double[][][] w = new double[nodes][4][4];
        for (int i = 0; i < nodes; i++) {
            final int o = 16 + (f * nodes + i) * 48;
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 3; c++) w[i][r][c] = b.getFloat(o + (r * 3 + c) * 4);
            w[i][3][3] = 1;
        }
        return w;
    }

    /**
     * Each node's world matrix (rows) in the first frame of the pose file's idle: its first idle move (SEG1 kind 0), else
     * its first move, else frame 0 (xr_quest.cpp fr_fig_bake's). Skinned as xr_evo.cpp does: a node without an inverse
     * bind takes the world as it is (its stored matrix, which Mesh.skinned would use, set to the identity in this
     * loaded copy). null: it doesn't fit.
     */
    static double[][][] pose(Gpk pack, byte[] g) {
        if (g.length < 16 || g[0] != 'G' || g[1] != 'E' || g[2] != 'V' || g[3] != '1') return null;
        final ByteBuffer b = ByteBuffer.wrap(g).order(ByteOrder.LITTLE_ENDIAN);
        final int nodes = b.getInt(4), frames = b.getInt(8);
        if (nodes != pack.nodes.length || nodes <= 0 || frames <= 0 || g.length < 16 + (long) frames * nodes * 48) return null;
        int frame = 0;
        final int at = 16 + frames * nodes * 48;
        if (g.length >= at + 12 && g[at] == 'S' && g[at + 1] == 'E' && g[at + 2] == 'G' && g[at + 3] == '1') {
            final int count = b.getInt(at + 4);
            int first = -1, idle = -1;
            for (int i = 0; i < count && i < 256 && g.length >= at + 24 + i * 12; i++) {
                final int start = b.getInt(at + 12 + i * 12), n = b.getInt(at + 16 + i * 12), kind = b.getInt(at + 20 + i * 12);
                if (n <= 0 || start < 0 || start + n > frames) continue;
                if (first < 0) first = start;
                if (idle < 0 && kind == 0) idle = start;
            }
            frame = idle >= 0 ? idle : Math.max(0, first);
        }
        final double[][][] w = new double[nodes][4][4];
        for (int i = 0; i < nodes; i++) {
            final int o = 16 + (frame * nodes + i) * 48;
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 3; c++) w[i][r][c] = b.getFloat(o + (r * 3 + c) * 4);
            w[i][3][3] = 1;
            if (!pack.nodes[i].hasInv) pack.nodes[i].inv = new double[][] {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        }
        return w;
    }
}
