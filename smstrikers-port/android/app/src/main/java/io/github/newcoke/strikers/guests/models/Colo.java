package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Out;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.Set;

/**
 * Pokemon Colosseum's side: the HSD dat inside a pkx_<name>.fsys (colopack.py), and the steps the PC tools ran on the
 * built packs: the Charmander line's tail fire (colo_flame.py), a dropped packet (gpk_drop.py), the rare_ shiny
 * pictures put into the normal pack (colo_shiny.py rare).
 */
final class Colo {
    private Colo() {}

    /** colopack.dat_of: the dat, its model's root joint, the idle's MatAnimJoint tree (0 none). */
    static final class Dat {
        byte[] dat;
        int root, matanim;
    }

    static byte[] pkx(byte[] fsys) throws IOException {
        final byte[] pkx = Fsys.first(fsys, Fsys.TYPE_PKX);
        if (pkx == null) throw new IOException("no pkx in the archive");
        return pkx;
    }

    static Dat datOf(byte[] fsys) throws IOException {
        final byte[] pkx = pkx(fsys);
        final int n = Bin.u32(pkx, 0);
        if (n <= 0 || 0x40 + n > pkx.length) throw new IOException("bad pkx");
        final Dat r = new Dat();
        r.dat = Arrays.copyOfRange(pkx, 0x40, 0x40 + n);
        final Hsd d = new Hsd(r.dat);
        int scene = -1;
        for (int i = 0; i < d.rootNames.size(); i++) if (d.rootNames.get(i).equals("scene_data")) scene = d.rootOffs.get(i);
        if (scene < 0) throw new IOException("no scene_data");
        final int modelset = d.u32(d.u32(scene));
        final int matAnims = d.u32(modelset + 0x08);
        r.root = d.u32(modelset);
        r.matanim = matAnims != 0 ? d.u32(matAnims) : 0;
        return r;
    }

    // ------------------------------------------------------------------------------------------------ GPK2 parts
    static final int NODE_BYTES = 156;

    /** [start, end) of the texture section. */
    static int[] texSection(byte[] b) {
        final int nn = Bin.le32(b, 8), nt = Bin.le32(b, 16);
        int o = 64 + nn * NODE_BYTES;
        final int start = o;
        for (int i = 0; i < nt; i++) {
            final int n = Bin.le32(b, o + 4);
            o += 8 + ((n + 3) & ~3);
        }
        return new int[] {start, o};
    }

    /** The end of the packets (where the animations would start). */
    static int packetsEnd(byte[] b, int pkAt) {
        final int np = Bin.le32(b, 12);
        int o = pkAt;
        for (int i = 0; i < np; i++) {
            final int nv = Bin.le32(b, o + 4) & 0x0FFFFFFF, ni = Bin.le32(b, o + 8);
            o += 12 + nv * 52 + ((ni * 2 + 3) & ~3);
        }
        return o;
    }

    /** gpk_drop.py: the pack without these packets (their textures stay). */
    static byte[] drop(byte[] b, Set<Integer> drop) {
        final int version = Bin.le32(b, 4), nn = Bin.le32(b, 8), np = Bin.le32(b, 12), nt = Bin.le32(b, 16), na = Bin.le32(b, 20);
        int o = texSection(b)[1];
        final Out out = new Out();
        out.bytes(b, 0, o);
        int kept = 0;
        for (int i = 0; i < np; i++) {
            final int nv = Bin.le32(b, o + 4), ni = Bin.le32(b, o + 8);
            final int size = 12 + (nv & 0x0FFFFFFF) * 52 + ((ni * 2 + 3) & ~3);
            if (!drop.contains(i)) {
                out.bytes(b, o, size);
                kept++;
            }
            o += size;
        }
        out.bytes(b, o, b.length - o);
        out.patchLe32(4, version);
        out.patchLe32(8, nn);
        out.patchLe32(12, kept);
        out.patchLe32(16, nt);
        out.patchLe32(20, na);
        return out.toBytes();
    }

    /** colo_shiny.py rare: the normal pack with these texture records (in order) under the normal pack's hashes. */
    static byte[] transplant(byte[] normal, List<byte[]> records) throws IOException {
        final int nt = Bin.le32(normal, 16);
        if (records.size() != nt) throw new IOException("rare_ model: " + records.size() + " textures, normal " + nt);
        final int[] sec = texSection(normal);
        final Out out = new Out();
        out.bytes(normal, 0, sec[0]);
        int o = sec[0];
        for (int i = 0; i < nt; i++) {
            final int h = Bin.le32(normal, o), n = Bin.le32(normal, o + 4);
            final byte[] rec = records.get(i);
            out.le32(h).le32(rec.length).bytes(rec).align(4);
            o += 8 + ((n + 3) & ~3);
        }
        out.bytes(normal, sec[1], normal.length - sec[1]);
        return out.toBytes();
    }

    // ------------------------------------------------------------------------------------------------ flame
    /** colo_flame.gpt1_frames: the model's particle bank's flame flipbook (32 x 32 I4 frames) as intensities. */
    private static int[][] gpt1Frames(byte[] pkx, int[] wh) throws IOException {
        final int gsize = Bin.u32(pkx, 4);
        int at = -1;
        for (int i = 0; i + 4 <= pkx.length; i++) {
            if (pkx[i] == 'G' && pkx[i + 1] == 'P' && pkx[i + 2] == 'T' && pkx[i + 3] == '1') {
                at = i;
                break;
            }
        }
        if (at < 0) throw new IOException("no particle bank");
        final byte[] g = Arrays.copyOfRange(pkx, at, Math.min(pkx.length, at + gsize));
        final int texg = Bin.u32(g, 8);
        final int count = Bin.u32(g, texg);
        final int entry = texg + Bin.u32(g, texg + 4);
        final int nframes = Bin.u32(g, entry), fmt = Bin.u32(g, entry + 4), w = Bin.u32(g, entry + 12), h = Bin.u32(g, entry + 16);
        if (!(fmt == 0 && w == 32 && h == 32 && count >= 1)) throw new IOException("flame: unexpected particle texture");
        final int[][] frames = new int[nframes][];
        for (int f = 0; f < nframes; f++) {
            final int off = Bin.u32(g, entry + 0x18 + 4 * f);
            final int base = texg + off;
            if (base + w * h / 2 > g.length) throw new IOException("flame frame past the bank");
            final int[] img = new int[w * h];
            int k = 0;
            for (int ty = 0; ty < h; ty += 8)
                for (int tx = 0; tx < w; tx += 8)
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x += 2) {
                            final int bb = g[base + k++] & 0xFF;
                            img[(ty + y) * w + tx + x] = (bb >> 4) * 17;
                            img[(ty + y) * w + tx + x + 1] = (bb & 15) * 17;
                        }
            frames[f] = img;
        }
        wh[0] = w;
        wh[1] = h;
        return frames;
    }

    /** colo_flame.colour: the game's flame particle colours; brightness = see-through. */
    static int[] flameColour(int i) {
        final double t = i / 255.0;
        final double[] t0s = {0.0, 0.35, 0.65, 1.0};
        final int[][] cs = {{200, 40, 0}, {255, 110, 0}, {255, 175, 30}, {255, 245, 170}};
        int[] rgb = null;
        for (int s = 0; s < 3; s++) {
            final double ta = t0s[s], tb = t0s[s + 1];
            if (t <= tb) {
                final double f = tb > ta ? (t - ta) / (tb - ta) : 0.0;
                rgb = new int[3];
                for (int k = 0; k < 3; k++) rgb[k] = (int) (cs[s][k] + (cs[s + 1][k] - cs[s][k]) * f);
                break;
            }
        }
        if (rgb == null) rgb = new int[] {0, 0, 0};  // (t > 1 cannot happen)
        final int a = (int) (255 * StrictMath.pow(MeleePack.pyMin(1.0, t * 1.25), 0.9));
        return new int[] {rgb[0], rgb[1], rgb[2], a};
    }

    /** colo_flame.py: the tail fire packets (and their textures) added to a built pack. */
    static byte[] flame(byte[] pkxFsys, byte[] b, int joint, double size) throws IOException {
        if (!(b[0] == 'G' && b[1] == 'P' && b[2] == 'K' && b[3] == '2')) throw new IOException("not a GPK2 pack");
        final int version = Bin.le32(b, 4), nn = Bin.le32(b, 8), np = Bin.le32(b, 12), nt = Bin.le32(b, 16), na = Bin.le32(b, 20);
        int e = 28;
        while (e < 60 && b[e] != 0) e++;
        final String name = new String(b, 28, e - 28, StandardCharsets.ISO_8859_1);
        final int nodesAt = 64;
        final int[] sec = texSection(b);
        final int texAt = sec[0], pkAt = sec[1];
        final double[] lo = {1e9, 1e9, 1e9}, hi = {-1e9, -1e9, -1e9};
        int o = pkAt;
        for (int i = 0; i < np; i++) {
            final int nv = Bin.le32(b, o + 4) & 0x0FFFFFFF, ni = Bin.le32(b, o + 8);
            o += 12;
            for (int v = 0; v < nv; v++) {
                for (int k = 0; k < 3; k++) {
                    final double p = Bin.lef32(b, o + v * 52 + k * 4);
                    lo[k] = MeleePack.pyMin(lo[k], p);
                    hi[k] = MeleePack.pyMax(hi[k], p);
                }
            }
            o += nv * 52 + ((ni * 2 + 3) & ~3);
        }
        final int animAt = o;
        int up = 0;
        double best = 0;
        for (int k = 0; k < 3; k++) {
            final double key = lo[k] > -0.05 * (hi[k] - lo[k]) ? hi[k] - lo[k] : -1;
            if (k == 0 || key > best) {
                best = key;
                up = k;
            }
        }
        final double height = hi[up] - lo[up];
        if (joint < 0 || joint >= nn) throw new IOException("flame joint " + joint);
        final int bwAt = nodesAt + joint * NODE_BYTES + 92;
        final double[] centre = {Bin.lef32(b, bwAt + 48), Bin.lef32(b, bwAt + 52), Bin.lef32(b, bwAt + 56)};
        final double H = size * height;
        final double W = 0.6 * H;
        final int[] wh = new int[2];
        final int[][] frames = gpt1Frames(pkx(pkxFsys), wh);
        final Out newTex = new Out(), newPk = new Out();
        final int[] axes = new int[2];
        int ai = 0;
        for (int k = 0; k < 3; k++) if (k != up) axes[ai++] = k;
        final int[] hashes = new int[frames.length];
        final int[][] lut = new int[256][];
        for (int f = 0; f < frames.length; f++) {
            final byte[] rgba = new byte[wh[0] * wh[1] * 4];
            for (int i = 0; i < frames[f].length; i++) {
                final int v = frames[f][i];
                if (lut[v] == null) lut[v] = flameColour(v);
                for (int c = 0; c < 4; c++) rgba[i * 4 + c] = (byte) lut[v][c];
            }
            final int th = Py.lowerHash(name + "/flame/" + f);
            hashes[f] = th;
            final byte[] rec = Tex.record(rgba, wh[0], wh[1]);
            newTex.le32(th).le32(rec.length).bytes(rec);
            while (newTex.size() % 4 != 0) newTex.u8(0);
        }
        final double[][] layers = {{0.0, 1.0}, {0.38, 0.72}, {0.68, 0.46}};
        int npk = 0;
        for (int f = 0; f < frames.length; f++) {
            for (int k = 0; k < 3; k++) {
                final double rise = layers[k][0], sz = layers[k][1];
                final int th = hashes[(f + k) % frames.length];
                final Out verts = new Out();
                final Geo.IntList strip = new Geo.IntList(16);
                final double side = W * sz;
                int nverts = 0;
                for (int q = 0; q < 3; q++) {
                    final double a = Trig.radians(60 * q + 20 * f + 35 * k);
                    final double dx = Trig.cos(a), dz = Trig.sin(a);
                    final double[] n = {0.0, 0.0, 0.0};
                    n[axes[0]] = -dz;
                    n[axes[1]] = dx;
                    final int base = nverts;
                    final int[][] st = {{-1, 0}, {1, 0}, {-1, 1}, {1, 1}};
                    for (int[] c : st) {
                        final int s = c[0], t = c[1];
                        final double[] p = centre.clone();
                        p[axes[0]] += s * 0.5 * side * dx;
                        p[axes[1]] += s * 0.5 * side * dz;
                        p[up] += -0.25 * H + rise * H + (t - 0.0) * side;
                        final double u = (s + 1) * 0.5, v = 1.0 - t;
                        for (int i = 0; i < 3; i++) verts.lef32((float) p[i]);
                        for (int i = 0; i < 3; i++) verts.lef32((float) n[i]);
                        verts.lef32((float) u).lef32((float) v);
                        verts.u8(joint).u8(0).u8(0).u8(0);
                        verts.lef32(1.0f).lef32(0.0f).lef32(0.0f).lef32(0.0f);
                        nverts++;
                    }
                    if (strip.n > 0) strip.add(strip.a[strip.n - 1]).add(base);
                    strip.add(base).add(base + 1).add(base + 2).add(base + 3);
                }
                final int flags = 0x40000000 | (f << 28);
                newPk.le32(th).le32(nverts | flags).le32(strip.n).bytes(verts.toBytes());
                for (int i = 0; i < strip.n; i++) newPk.le16(strip.a[i]);
                while (newPk.size() % 4 != 0) newPk.u8(0);
                npk++;
            }
        }
        final Out out = new Out();
        out.bytes(b, 0, texAt).bytes(b, texAt, pkAt - texAt).bytes(newTex.toBytes()).bytes(b, pkAt, animAt - pkAt)
                .bytes(newPk.toBytes()).bytes(b, animAt, b.length - animAt);
        out.patchLe32(4, version);
        out.patchLe32(8, nn);
        out.patchLe32(12, np + npk);
        out.patchLe32(16, nt + frames.length);
        out.patchLe32(20, na);
        return out.toBytes();
    }
}
