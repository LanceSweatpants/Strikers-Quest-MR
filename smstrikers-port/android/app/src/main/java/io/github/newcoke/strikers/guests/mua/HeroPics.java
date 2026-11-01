package io.github.newcoke.strikers.guests.mua;

import io.github.newcoke.strikers.guests.render.Gpk;
import io.github.newcoke.strikers.guests.render.HostRef;
import io.github.newcoke.strikers.guests.render.Mesh;
import io.github.newcoke.strikers.guests.render.Nl;
import io.github.newcoke.strikers.guests.render.Portrait;
import io.github.newcoke.strikers.guests.render.Py;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The heroes' pictures:
 * <ul>
 * <li>portraits (hero_portraits.py): the pack posed by its slot's idle (retargetsim), drawn by the CPU stand-in for
 * portrait_render.py's Cycles (render.Portrait, 256 x 256, the whole figure, turned -70 degrees);</li>
 * <li>the home figure posed by one frame of its loop (gpk2obj.py --gev): the outline for the banners' emblem
 * (hero_icons.py: its silhouette from the front, filled as Pillow fills polygons) and for the etch atlas.</li>
 * </ul>
 */
final class HeroPics {
    private HeroPics() {}

    static final Map<String, String> CLS = new HashMap<>();

    static {
        for (String c : new String[] {"Mario", "Luigi", "Peach", "Daisy", "Yoshi", "Wario", "Waluigi", "DonkeyKong"})
            CLS.put(c.toLowerCase(Locale.ROOT), c);
    }

    /** "key value..." lines -> key -> numbers (render/portrait.txt). */
    static Map<String, double[]> numbers(String text) {
        final Map<String, double[]> out = new HashMap<>();
        for (String line : text.split("\n")) {
            final int hash = line.indexOf('#');
            final String s = (hash >= 0 ? line.substring(0, hash) : line).trim();
            if (s.isEmpty()) continue;
            final String[] p = s.split("\\s+");
            final double[] v = new double[p.length - 1];
            for (int i = 1; i < p.length; i++) v[i - 1] = Double.parseDouble(p[i]);
            out.put(p[0], v);
        }
        return out;
    }

    /** A portrait, RGBA 256 x 256: refAll / stand: the r124 own standing turn (null: the plain retarget). */
    static byte[] portrait(Gpk pack, Nl.Host host, HostRef.Ref ref, HostRef.Ref refAll, String stand, Portrait.Look look) {
        final double[][][] idle = new HeroPose(host, "idle", pack, ref, refAll, stand, 1.0).pose(0.0);
        final Mesh m = Mesh.skinned(pack, idle, look.flames);
        final Portrait.Scene s = Portrait.scene(m, 256, 256, true, -70, look);
        return Portrait.render(s, m, look);
    }

    /** One frame of a GEV1 loop as world matrices (rows; the 4th column 0 0 0 1). */
    static double[][][] gevWorld(byte[] gev, int frame) {
        final int nn = le32(gev, 4), frames = le32(gev, 8);
        frame = Math.max(0, Math.min(frames - 1, frame));
        final double[][][] w = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            final int o = 16 + (frame * nn + i) * 48;
            final double[] r = new double[12];
            for (int k = 0; k < 12; k++) r[k] = Float.intBitsToFloat(le32(gev, o + 4 * k));
            w[i] = new double[][] {{r[0], r[1], r[2], 0.0}, {r[3], r[4], r[5], 0.0}, {r[6], r[7], r[8], 0.0}, {r[9], r[10], r[11], 1.0}};
        }
        return w;
    }

    /** The home figure posed by a loop frame, for the etch atlas (render.Mesh: its skin as the game's). */
    static Mesh homeMesh(Gpk home, byte[] gev, int frame) {
        return Mesh.skinned(home, gevWorld(gev, frame), true);
    }

    static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    // ------------------------------------------------------------------------------------------------ silhouette
    /**
     * gpk2obj.py --gev <frame>: every packet's vertices posed (skin = inverse bind x world, Python's sums; the weighted
     * sum over the weights), as the OBJ prints them ("%.5f"), and the strips' triangles (global vertex indices).
     */
    static Object[] posedObj(byte[] pack, byte[] gev, int frame) {
        final int nn = le32(pack, 8), npk = le32(pack, 12), nt = le32(pack, 16), na = le32(pack, 20);
        int p = 64;
        final double[][][] inv = new double[nn][][];
        final boolean[] hasInv = new boolean[nn];
        final int[] parents = new int[nn];
        final double[][] trans = new double[nn][3];
        for (int i = 0; i < nn; i++) {
            parents[i] = le32(pack, p + 4);
            for (int c = 0; c < 3; c++) trans[i][c] = Float.intBitsToFloat(le32(pack, p + 8 + 4 * c));
            hasInv[i] = le32(pack, p + 20) != 0;
            inv[i] = new double[4][4];
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) inv[i][r][c] = Float.intBitsToFloat(le32(pack, p + 24 + 4 * (4 * r + c)));
            p += 156;
        }
        for (int i = 0; i < nt; i++) p += 8 + ((le32(pack, p + 4) + 3) & ~3);
        final double[][][] world = gev != null ? gevWorld(gev, frame) : ownIdle(pack, p, npk, na, nn, parents, trans);
        final double[][][] skin = new double[nn][][];
        for (int i = 0; i < nn; i++) skin[i] = hasInv[i] ? Py.mul(inv[i], world[i]) : world[i];
        final List<double[]> vs = new ArrayList<>();
        final List<int[]> faces = new ArrayList<>();
        int base = 0;
        for (int k = 0; k < npk; k++) {
            final int nv = le32(pack, p + 4) & 0x0FFFFFFF, ni = le32(pack, p + 8);
            p += 12;
            for (int v = 0; v < nv; v++) {
                final int o = p + v * 52;
                final double x = Float.intBitsToFloat(le32(pack, o)), y = Float.intBitsToFloat(le32(pack, o + 4)),
                        z = Float.intBitsToFloat(le32(pack, o + 8));
                final double[] acc = new double[3];
                double tw = 0.0;
                for (int q = 0; q < 4; q++) {
                    final double wgt = Float.intBitsToFloat(le32(pack, o + 36 + 4 * q));
                    final int nd = pack[o + 32 + q] & 0xFF;
                    if (wgt <= 0.0 || nd >= nn) continue;
                    final double[][] m = skin[nd];
                    for (int c = 0; c < 3; c++) acc[c] += wgt * (x * m[0][c] + y * m[1][c] + z * m[2][c] + m[3][c]);
                    tw += wgt;
                }
                if (tw == 0.0) tw = 1.0;
                vs.add(new double[] {f5(acc[0] / tw), f5(acc[1] / tw), f5(acc[2] / tw)});
            }
            p += nv * 52;
            final int[] strip = new int[ni];
            for (int i = 0; i < ni; i++) strip[i] = (pack[p + 2 * i] & 0xFF) | ((pack[p + 2 * i + 1] & 0xFF) << 8);
            p += (ni * 2 + 3) & ~3;
            for (int i = 0; i + 2 < ni; i++) {
                int a = strip[i], c = strip[i + 1];
                final int d = strip[i + 2];
                if (a == c || c == d || a == d) continue;
                if (i % 2 == 1) {
                    final int x = a;
                    a = c;
                    c = x;
                }
                faces.add(new int[] {a + base, c + base, d + base});
            }
            base += nv;
        }
        return new Object[] {vs, faces};
    }

    /** gpk2obj.py --anim own_idle 0 (posed_skin): the pack's own idle, its first key, the root where it stands. */
    private static double[][][] ownIdle(byte[] b, int p, int npk, int na, int nn, int[] parents, double[][] trans) {
        for (int k = 0; k < npk; k++) {
            final int nv = le32(b, p + 4) & 0x0FFFFFFF, ni = le32(b, p + 8);
            p += 12 + nv * 52 + ((ni * 2 + 3) & ~3);
        }
        for (int a = 0; a < na; a++) {
            final int keys = le32(b, p + 48);
            int e = p;
            while (e < p + 48 && b[e] != 0) e++;
            final String name = new String(b, p, e - p, java.nio.charset.StandardCharsets.ISO_8859_1);
            if (name.equalsIgnoreCase("own_idle")) {
                final int data = p + 52;
                final int has = data + keys * nn * 28;
                final double[][][] world = new double[nn][][];
                for (int i = 0; i < nn; i++) {
                    final double[] v = new double[7];
                    for (int q = 0; q < 7; q++) v[q] = Float.intBitsToFloat(le32(b, data + i * 28 + 4 * q));
                    double n = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
                    if (n == 0.0) n = 1.0;
                    final double x = v[0] / n, y = v[1] / n, z = v[2] / n, w = v[3] / n;
                    final double[] t = b[has + i] != 0 ? new double[] {v[4], v[5], v[6]} : trans[i];
                    final double[][] local = {
                        {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0},
                        {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0},
                        {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0},
                        {t[0], t[1], t[2], 1.0}};
                    final int par = parents[i];
                    world[i] = par < 0 || par >= i ? new double[][] {{1.0, 0, 0, 0}, {0, 1.0, 0, 0}, {0, 0, 1.0, 0}, {0, 0, 0, 1.0}}
                            : Py.mul(local, world[par]);
                }
                return world;
            }
            p += 52 + keys * nn * 28 + ((nn + 3) & ~3);
        }
        throw new IllegalArgumentException("no own_idle clip");
    }

    /** float("%.5f" % x) */
    static double f5(double x) {
        return new BigDecimal(x).setScale(5, RoundingMode.HALF_EVEN).doubleValue();
    }

    /** hero_icons.silhouette: the figure seen across its widest spread on the floor, px x px RGBA (white, alpha = shape). */
    @SuppressWarnings("unchecked")
    static byte[] silhouette(Object[] obj, int px) {
        final List<double[]> vs0 = (List<double[]>) obj[0];
        final List<int[]> faces = (List<int[]>) obj[1];
        final int n = vs0.size();
        final double[] a0 = new double[n], a1 = new double[n];
        for (int i = 0; i < n; i++) {
            a0[i] = vs0.get(i)[0];
            a1[i] = vs0.get(i)[1];
        }
        final double mx = Np.pySum(a0, n) / n, my = Np.pySum(a1, n) / n;
        final double[] t = new double[n];
        for (int i = 0; i < n; i++) t[i] = (a0[i] - mx) * (a0[i] - mx);
        final double sxx = Np.pySum(t, n);
        for (int i = 0; i < n; i++) t[i] = (a1[i] - my) * (a1[i] - my);
        final double syy = Np.pySum(t, n);
        for (int i = 0; i < n; i++) t[i] = (a0[i] - mx) * (a1[i] - my);
        final double sxy = Np.pySum(t, n);
        final double ang = 0.5 * Libm.atan2(2 * sxy, sxx - syy);
        final double ca = Libm.cos(ang), sa = Libm.sin(ang);
        final double[] xs = new double[n], ys = new double[n];
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (int i = 0; i < n; i++) {
            final double[] v = vs0.get(i);
            xs[i] = v[0] * ca + v[1] * sa;
            ys[i] = v[2];
            if (i == 0 || xs[i] < x0) x0 = xs[i];
            if (i == 0 || xs[i] > x1) x1 = xs[i];
            if (i == 0 || ys[i] < y0) y0 = ys[i];
            if (i == 0 || ys[i] > y1) y1 = ys[i];
        }
        final double dxs = x1 - x0, dys = y1 - y0;
        final double s = (px - 8) / (dys > dxs ? dys : dxs);
        final byte[] mask = new byte[px * px];
        final double ox = (px - (x1 - x0) * s) / 2, oy = (px - (y1 - y0) * s) / 2;
        for (int[] f : faces) {
            final double[] xy = new double[f.length * 2];
            for (int k = 0; k < f.length; k++) {
                xy[2 * k] = ox + (xs[f[k]] - x0) * s;
                xy[2 * k + 1] = px - (oy + (ys[f[k]] - y0) * s);
            }
            PilPoly.fill(mask, px, px, xy, 255);
        }
        final byte[] rgba = new byte[px * px * 4];
        for (int i = 0; i < px * px; i++) {
            rgba[i * 4] = (byte) 255;
            rgba[i * 4 + 1] = (byte) 255;
            rgba[i * 4 + 2] = (byte) 255;
            rgba[i * 4 + 3] = mask[i];
        }
        return rgba;
    }

    /** hero_icons.py's colours: the main colour (the trim one when the main is near black or too dark), its outline. */
    static int[][] bannerColours(String[] colors) {
        final int[] main = rgb(colors[0]), trim = rgb(colors[1]);
        final double lum = 0.299 * main[0] + 0.587 * main[1] + 0.114 * main[2];
        final int[] fill = main[0] + main[1] + main[2] > 150 && lum >= 50 ? main : trim;
        final int[] ln = {(int) (fill[0] * 0.18), (int) (fill[1] * 0.18), (int) (fill[2] * 0.18)};
        return new int[][] {fill, ln};
    }

    static int[] rgb(String h) {
        return new int[] {Integer.parseInt(h.substring(0, 2), 16), Integer.parseInt(h.substring(2, 4), 16),
            Integer.parseInt(h.substring(4, 6), 16)};
    }
}
