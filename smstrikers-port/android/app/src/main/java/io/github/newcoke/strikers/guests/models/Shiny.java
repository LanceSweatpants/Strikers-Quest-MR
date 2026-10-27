package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * colo_shiny.py's pack step: a shiny pack = its normal pack byte for byte with each texture picture recoloured by the
 * species' colour map (made on the PC from the game's two body pictures, shipped as numbers: shiny.txt). A texel
 * follows the paints it is near (Gaussian in OKLab chromaticity space, blended): each paint's HSV palette swap.
 */
final class Shiny {
    /** One paint of the normal picture: its clusters (feature space) and its N -> S move. */
    static final class Rule {
        boolean keep;
        double nH, nS, nV, sH, sS, sV;
        boolean nNeutral, sNeutral;
        double[][] clusters;
    }

    private static final double FLOOR = 0.12, WL = 0.2, SIGMA = 0.06;
    private static final double[] LIN = new double[256];

    static {
        for (int i = 0; i < 256; i++) {
            final double c = i / 255.0;
            LIN[i] = c <= 0.04045 ? c / 12.92 : StrictMath.pow((c + 0.055) / 1.055, 2.4);
        }
    }

    private final List<Rule> rules;
    private final Map<Integer, Integer> cache = new HashMap<>();

    Shiny(List<Rule> rules) {
        this.rules = rules;
    }

    static double[] toLab(int r8, int g8, int b8) {
        final double r = LIN[r8], g = LIN[g8], b = LIN[b8];
        final double third = 1.0 / 3;
        final double l = StrictMath.pow(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b, third);
        final double m = StrictMath.pow(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b, third);
        final double s = StrictMath.pow(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b, third);
        return new double[] {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
    }

    private static double srgb(double x) {
        x = MeleePack.pyMax(0.0, MeleePack.pyMin(1.0, x));
        return x <= 0.0031308 ? 12.92 * x : 1.055 * StrictMath.pow(x, 1 / 2.4) - 0.055;
    }

    private static double[] labToLin(double L, double a, double b) {
        final double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
        final double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
        final double s_ = L - 0.0894841775 * a - 1.2914855480 * b;
        final double l = StrictMath.pow(l_, 3), m = StrictMath.pow(m_, 3), s = StrictMath.pow(s_, 3);
        return new double[] {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
    }

    /** from_lab: OKLab -> sRGB 0-255, chroma pulled in (hue and lightness kept) until it fits. */
    static int[] fromLab(double L, double a, double b) {
        L = MeleePack.pyMax(0.0, MeleePack.pyMin(1.0, L));
        double[] rgb = null;
        for (int k = 0; k < 12; k++) {
            final double f = 1.0 - k / 11.0;
            rgb = labToLin(L, a * f, b * f);
            boolean ok = true;
            for (double c : rgb) if (!(-0.002 <= c && c <= 1.002)) ok = false;
            if (ok) break;
        }
        return new int[] {(int) Math.rint(srgb(rgb[0]) * 255), (int) Math.rint(srgb(rgb[1]) * 255), (int) Math.rint(srgb(rgb[2]) * 255)};
    }

    private static double[] feat(double[] c) {
        final double d = MeleePack.pyMax(c[0], FLOOR);
        return new double[] {c[1] / d, c[2] / d, WL * c[0]};
    }

    private static double fdist(double[] p, double[] q) {
        final double a = p[0] - q[0], b = p[1] - q[1], c = p[2] - q[2];
        return Math.sqrt(a * a + b * b + c * c);
    }

    private static double hueDiff(double a, double b) {
        return Py.mod(a - b + 180.0, 360.0) - 180.0;
    }

    /** colorsys.rgb_to_hsv */
    static double[] rgbToHsv(double r, double g, double b) {
        final double maxc = MeleePack.pyMax(MeleePack.pyMax(r, g), b), minc = MeleePack.pyMin(MeleePack.pyMin(r, g), b);
        final double rangec = maxc - minc;
        final double v = maxc;
        if (minc == maxc) return new double[] {0.0, 0.0, v};
        final double s = rangec / maxc;
        final double rc = (maxc - r) / rangec, gc = (maxc - g) / rangec, bc = (maxc - b) / rangec;
        double h;
        if (r == maxc) h = bc - gc;
        else if (g == maxc) h = 2.0 + rc - bc;
        else h = 4.0 + gc - rc;
        h = Py.mod(h / 6.0, 1.0);
        return new double[] {h, s, v};
    }

    /** colorsys.hsv_to_rgb */
    static double[] hsvToRgb(double h, double s, double v) {
        if (s == 0.0) return new double[] {v, v, v};
        int i = (int) (h * 6.0);
        final double f = (h * 6.0) - i;
        final double p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
        i = Math.floorMod(i, 6);
        switch (i) {
            case 0: return new double[] {v, t, p};
            case 1: return new double[] {q, v, p};
            case 2: return new double[] {p, v, t};
            case 3: return new double[] {p, q, v};
            case 4: return new double[] {t, p, v};
            default: return new double[] {v, p, q};
        }
    }

    /** transfer(): a colour moved as paint N -> paint S (a palette swap in HSV). */
    private static int[] transfer(int r8, int g8, int b8, Rule R) {
        final double[] hsv = rgbToHsv(r8 / 255.0, g8 / 255.0, b8 / 255.0);
        final double H = hsv[0], Sa = hsv[1], V = hsv[2];
        final double V2;
        if (R.sV <= R.nV) V2 = V * (R.sV / MeleePack.pyMax(R.nV, 0.02));
        else V2 = 1.0 - (1.0 - V) * ((1.0 - R.sV) / MeleePack.pyMax(1.0 - R.nV, 0.02));
        double H2, S2;
        if (!R.nNeutral && !R.sNeutral) {
            H2 = H + hueDiff(R.sH, R.nH) / 360.0;
            if (R.sS <= R.nS) S2 = Sa * R.sS / MeleePack.pyMax(R.nS, 0.02);
            else S2 = 1.0 - (1.0 - Sa) * ((1.0 - R.sS) / MeleePack.pyMax(1.0 - R.nS, 0.02));
        } else if (!R.nNeutral) {
            H2 = H;
            S2 = Sa * R.sS / MeleePack.pyMax(R.nS, 0.02);
        } else if (!R.sNeutral) {
            H2 = R.sH / 360.0;
            S2 = R.sS + (Sa - R.nS);
        } else {
            H2 = H;
            S2 = Sa * (R.sS + 0.02) / (R.nS + 0.02);
        }
        final double[] rgb = hsvToRgb(Py.mod(H2, 1.0), MeleePack.pyMax(0.0, MeleePack.pyMin(1.0, S2)),
                MeleePack.pyMax(0.0, MeleePack.pyMin(1.0, V2)));
        return new int[] {(int) Math.rint(rgb[0] * 255), (int) Math.rint(rgb[1] * 255), (int) Math.rint(rgb[2] * 255)};
    }

    /** Recolour.rgb -> packed 0xRRGGBB. */
    int rgb(int r, int g, int b) {
        final int key = (r << 16) | (g << 8) | b;
        final Integer have = cache.get(key);
        if (have != null) return have;
        final double[] fx = feat(toLab(r, g, b));
        final double[] ds = new double[rules.size()];
        double d0 = 0;
        for (int i = 0; i < ds.length; i++) {
            double best = 0;
            final double[][] cl = rules.get(i).clusters;
            for (int c = 0; c < cl.length; c++) {
                final double dd = fdist(fx, cl[c]);
                if (c == 0 || dd < best) best = dd;
            }
            ds[i] = best;
            if (i == 0 || best < d0) d0 = best;
        }
        final double[] out = {0.0, 0.0, 0.0};
        double wsum = 0.0;
        final double den = 2 * (SIGMA * SIGMA);
        for (int i = 0; i < ds.length; i++) {
            final double d = ds[i];
            final double w = StrictMath.exp(-((d * d - d0 * d0) / den));
            if (w < 1e-3) continue;
            final Rule R = rules.get(i);
            final double[] y;
            if (R.keep) {
                y = toLab(r, g, b);
            } else {
                final int[] t = transfer(r, g, b, R);
                y = toLab(t[0], t[1], t[2]);
            }
            for (int q = 0; q < 3; q++) out[q] += w * y[q];
            wsum += w;
        }
        final int[] v = fromLab(out[0] / wsum, out[1] / wsum, out[2] / wsum);
        final int packed = (clamp(v[0]) << 16) | (clamp(v[1]) << 8) | clamp(v[2]);
        cache.put(key, packed);
        return packed;
    }

    private static int clamp(int v) {
        if (v < 0 || v > 255) throw new IllegalStateException("colour out of range " + v);
        return v;
    }

    /** colo_shiny.recolour_pack: every texture record's picture recoloured, the rest byte for byte. */
    byte[] recolourPack(byte[] b) throws IOException {
        final byte[] out = b.clone();
        final int nn = Bin.le32(b, 8), nt = Bin.le32(b, 16);
        int p = 64 + nn * Colo.NODE_BYTES;
        for (int i = 0; i < nt; i++) {
            final int sz = Bin.le32(b, p + 4);
            final int off = p + 8;
            final int w = Bin.u16(b, off + 0x0E), h = Bin.u16(b, off + 0x10);
            if (Bin.u32(b, off + 4) != 3) throw new IOException("texture format " + Bin.u32(b, off + 4));
            final byte[] rgba = Tex.rgba8(b, off + 0x20, w, h);
            for (int q = 0; q < rgba.length; q += 4) {
                final int c = rgb(rgba[q] & 0xFF, rgba[q + 1] & 0xFF, rgba[q + 2] & 0xFF);
                rgba[q] = (byte) (c >> 16);
                rgba[q + 1] = (byte) (c >> 8);
                rgba[q + 2] = (byte) c;
            }
            final byte[] tiles = Tex.gxRgba8(rgba, w, h);
            if (tiles.length != sz - 0x20) throw new IOException("texture size");
            System.arraycopy(tiles, 0, out, off + 0x20, tiles.length);
            p += 8 + ((sz + 3) & ~3);
        }
        return out;
    }

    /** shiny.txt -> pack name -> rules. */
    static Map<String, List<Rule>> parse(String text) {
        final Map<String, List<Rule>> all = new HashMap<>();
        List<Rule> cur = null;
        for (String line : text.split("\n")) {
            final String l = line.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] f = l.split("\\s+");
            if (f[0].equals("shiny")) {
                cur = new ArrayList<>();
                all.put(f[1], cur);
            } else if (f[0].equals("rule") && cur != null) {
                final Rule r = new Rule();
                r.keep = f[1].equals("1");
                int k = 2;
                if (!f[k++].equals("N")) throw new IllegalArgumentException("shiny rule: " + l);
                r.nH = Py.num(f[k++]);
                r.nS = Py.num(f[k++]);
                r.nV = Py.num(f[k++]);
                r.nNeutral = f[k++].equals("1");
                if (!f[k++].equals("S")) throw new IllegalArgumentException("shiny rule: " + l);
                r.sH = Py.num(f[k++]);
                r.sS = Py.num(f[k++]);
                r.sV = Py.num(f[k++]);
                r.sNeutral = f[k++].equals("1");
                if (!f[k++].equals("C")) throw new IllegalArgumentException("shiny rule: " + l);
                final int n = (f.length - k) / 3;
                r.clusters = new double[n][];
                for (int i = 0; i < n; i++)
                    r.clusters[i] = new double[] {Py.num(f[k + 3 * i]), Py.num(f[k + 3 * i + 1]), Py.num(f[k + 3 * i + 2])};
                cur.add(r);
            }
        }
        return all;
    }
}
