package io.github.newcoke.strikers.guests.models;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;
import io.github.newcoke.strikers.guests.render.Nl;
import io.github.newcoke.strikers.guests.render.Py;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * r178 (Lance's r173: the Strikers sidekicks' cards / awards showed a flat picture - no 3D figure of theirs existed): the
 * four Strikers SIDEKICKS' own 3D figures - Toad, Koopa, Birdo, Hammer Bro - made on the headset from the player's OWN
 * Super Mario Strikers disc, as tools/outside/sidefig173.py makes them on the PC (byte for byte: tools/guestconv
 * SideFigTest): the class's smooth-skinned model (art/characters/Class/Class_blend.glg + .glt, a kit from
 * guestdata/strikers/sidekicks.txt swapped in as a match does), its skeleton and clips (art/animation/class.shier /
 * .sanim) -> guests/home/strikers_name.gpk (GPK2, retarget mode, its own idle + its power shot as own_idle / own_run:
 * capgpk.py --own --run captain_shoot_to_score) and .gev (GEV1 + SEG1: its idle twice (move 0), its power shot (move 2);
 * each frame's lowest skinned point on the floor). Every figure loader finds them there (xr_cards.cpp load_figure,
 * xr_quest.cpp fr_fig_read, xr_big.cpp). Numbers as the Python holds them (doubles, the file's f32s as read).
 */
public final class SideFig {
    private SideFig() {}

    static final String BIG = "captain_shoot_to_score";
    static final double[][] RZ90_INV = {{0.0, -1.0, 0.0, 0.0}, {1.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 1.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};

    /** One sidekick: its class (the disc's folder), its pack name's tail, its kit's file (null: its default). */
    public static final class Kind {
        public final String cls, name, kit;

        Kind(String cls, String name, String kit) {
            this.cls = cls;
            this.name = name;
            this.kit = kit;
        }
    }

    /** guestdata/strikers/sidekicks.txt: "Class name kit|-" a line. */
    public static List<Kind> kinds(String text) {
        final List<Kind> out = new ArrayList<>();
        for (String line : text.split("\n")) {
            final String l = line.replace("\r", "").trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            final String[] c = l.split("\\s+");
            if (c.length < 3) continue;
            out.add(new Kind(c[0], c[1], c[2].equals("-") ? null : c[2]));
        }
        return out;
    }

    /** The converter's part: every sidekick it can make (one's failure a warning). */
    public static void run(Job job) {
        if (job.strikers == null) return;
        List<Kind> ks;
        try {
            ks = kinds(job.data.text("strikers/sidekicks.txt"));
        } catch (IOException e) {
            job.progress.warn("Strikers sidekicks: " + e.getMessage());
            return;
        }
        int done = 0;
        for (Kind k : ks) {
            job.progress.step("Strikers: " + k.cls, done, ks.size());
            try {
                final byte[][] f = make(job.strikers, k);
                Out.save(job.out("home/strikers_" + k.name + ".gpk"), f[0]);
                Out.save(job.out("home/strikers_" + k.name + ".gev"), f[1]);
            } catch (Exception e) {
                job.progress.warn("Strikers " + k.cls + ": " + e);
            }
            done++;
        }
        job.progress.step("Strikers sidekicks", ks.size(), ks.size());
    }

    // ------------------------------------------------------------------------------------------------ the .glg / .glt
    static final class Ch {
        final int id, ds, end, p;

        Ch(int id, int ds, int end, int p) {
            this.id = id;
            this.ds = ds;
            this.end = end;
            this.p = p;
        }
    }

    /** glgread.chunks */
    static List<Ch> chunks(byte[] b, int start, int end) {
        final List<Ch> out = new ArrayList<>();
        int p = start;
        while (p + 8 <= end) {
            final int cid = Bin.u32(b, p), size = Bin.u32(b, p + 4);
            final int a = (cid & 0x7F000000) >>> 24;
            final int ds = a != 0 ? ((p + (1 << a) + 7) & ~((1 << a) - 1)) : p + 8;
            out.add(new Ch(cid & 0x80FFFFFF, ds, p + 8 + size, p));
            p = p + 8 + size;
        }
        return out;
    }

    static void walk(byte[] b, int start, int end, List<Ch> out) {
        for (Ch c : chunks(b, start, end)) {
            out.add(c);
            if (c.id == 0x8001B100 || c.id == 0x8001B000 || c.id == 0x8001B008) walk(b, c.p + 8, c.end, out);
        }
    }

    static byte[] slice(byte[] b, int from, int to) {
        final int f = Math.min(from, b.length), t = Math.min(to, b.length);
        return t <= f ? new byte[0] : Bin.slice(b, f, t - f);
    }

    /** glgread.read_glt: {hash: record} in the file's order. */
    static LinkedHashMap<Integer, byte[]> glt(byte[] b) {
        final int n = Bin.u32(b, 4), base = 0x20 + 16 * n;
        final LinkedHashMap<Integer, byte[]> out = new LinkedHashMap<>();
        for (int i = 0; i < n; i++) {
            final int h = Bin.u32(b, 0x20 + 16 * i), off = Bin.u32(b, 0x20 + 16 * i + 4), size = Bin.u32(b, 0x20 + 16 * i + 8);
            out.put(h, slice(b, base + off, base + off + size));
        }
        return out;
    }

    static final class Packet {
        int prim, raster, tex0;
        final Map<Integer, int[]> streams = new HashMap<>();  // sid -> {addr, stride}
        int[] ind;
    }

    static final class Skin {
        final Map<Integer, double[][]> binds = new HashMap<>();
        final List<int[][]> maps = new ArrayList<>();       // each: {key bone, value} pairs
        final List<double[][]> verts = new ArrayList<>();   // {pos, normal}
        final List<int[][]> pairs = new ArrayList<>();      // each: {vertex, weight}
        final Map<Integer, byte[]> stitch = new HashMap<>();
    }

    static final class Glg {
        final List<Packet> packets = new ArrayList<>();
        byte[] vtx;
        Skin skin;
    }

    /** glgread.read_glg(...)[0]: the first model block with packets. */
    static Glg glg(byte[] b) {
        final List<Ch> tree = new ArrayList<>();
        walk(b, 0, b.length, tree);
        final List<Map<Integer, List<byte[]>>> models = new ArrayList<>();
        final List<int[]> skins = new ArrayList<>();
        Map<Integer, List<byte[]>> cur = null;
        for (Ch c : tree) {
            if (c.id == 0x8001B000) {
                cur = new HashMap<>();
                models.add(cur);
                skins.add(null);
            }
            if (cur == null) continue;
            if (c.id == 0x8001B008) {
                skins.set(skins.size() - 1, new int[] {c.p + 8, c.end});
            } else if (c.id == 0x1B002 || c.id == 0x1B003 || c.id == 0x1B004 || c.id == 0x1B005 || c.id == 0x1B006 || c.id == 0x1B007
                    || c.id == 0x1B00F || c.id == 0x1B011 || c.id == 0x1B012) {
                cur.computeIfAbsent(c.id, k -> new ArrayList<>()).add(slice(b, c.ds, c.end));
            }
        }
        for (int mi = 0; mi < models.size(); mi++) {
            final Map<Integer, List<byte[]>> parts = models.get(mi);
            if (!parts.containsKey(0x1B004)) continue;
            final byte[] pk = parts.get(0x1B004).get(0), st = parts.get(0x1B005).get(0), idx = parts.get(0x1B007).get(0);
            final Glg g = new Glg();
            g.vtx = parts.get(0x1B006).get(0);
            for (int i = 0; i < pk.length / 0x4A; i++) {
                final int o = i * 0x4A;
                final Packet p = new Packet();
                final int ib = Bin.u32(pk, o + 4), nv = Bin.u16(pk, o + 8), ns = Bin.u8(pk, o + 11), soff = Bin.u32(pk, o + 12);
                p.prim = Bin.u8(pk, o + 10);
                final int stt = o + 0x10;
                p.raster = Bin.u32(pk, stt + 16);
                p.tex0 = Bin.u32(pk, stt + 0x18);
                for (int s = 0; s < ns; s++) {
                    final int at = soff + s * 6;
                    p.streams.put(Bin.u8(st, at + 4), new int[] {Bin.u32(st, at), Bin.u8(st, at + 5)});
                }
                p.ind = new int[nv];
                for (int k = 0; k < nv; k++) p.ind[k] = Bin.u16(idx, ib + 2 * k);
                g.packets.add(p);
            }
            final int[] sk = skins.get(mi);
            if (sk != null) {
                final Skin s = new Skin();
                for (Ch c : chunks(b, sk[0], sk[1])) {
                    if (c.id == 0x1B00A) {
                        for (int k = 0; k < (c.end - c.ds) / 0x44; k++) {
                            final int bone = Bin.u32(b, c.ds + k * 0x44);
                            final double[][] m = new double[4][4];
                            for (int q = 0; q < 16; q++) m[q / 4][q % 4] = Bin.f32(b, c.ds + k * 0x44 + 4 + 4 * q);
                            s.binds.put(bone, m);
                        }
                    } else if (c.id == 0x1B00B) {
                        final int n = (c.end - c.ds) / 8;
                        final int[][] m = new int[n][2];
                        for (int k = 0; k < n; k++) m[k] = new int[] {Bin.u32(b, c.ds + 8 * k), Bin.u32(b, c.ds + 8 * k + 4)};
                        s.maps.add(m);
                    } else if (c.id == 0x1B00D) {
                        final int n = (c.end - c.ds) / 16;
                        s.verts.clear();
                        for (int k = 0; k < n; k++) {
                            final int at = c.ds + 16 * k;
                            s.verts.add(new double[][] {{Bin.f32(b, at), Bin.f32(b, at + 4), Bin.f32(b, at + 8)},
                                {Bin.s8(b, at + 12) / 64.0, Bin.s8(b, at + 13) / 64.0, Bin.s8(b, at + 14) / 64.0}});
                        }
                    } else if (c.id == 0x1B00E) {
                        final int n = (c.end - c.ds) / 4;
                        final int[][] l = new int[n][2];
                        for (int k = 0; k < n; k++) l[k] = new int[] {Bin.u16(b, c.ds + 4 * k), Bin.u16(b, c.ds + 4 * k + 2)};
                        s.pairs.add(l);
                    } else if (c.id == 0x1B010) {
                        final int pi = Bin.u32(b, c.ds + 4);
                        s.stitch.put(pi, slice(b, c.ds + 8, c.end));
                    }
                }
                g.skin = s;
            }
            return g;
        }
        throw new IllegalArgumentException("glg: no model");
    }

    /** glgread.stream_value */
    static double[] stream(byte[] vtx, int sid, int addr, int stride, int i) {
        final int o = addr + i * stride;
        if (sid == 0) {
            if (stride == 12) return new double[] {Bin.f32(vtx, o), Bin.f32(vtx, o + 4), Bin.f32(vtx, o + 8)};
            return new double[] {Bin.s16(vtx, o) / 256.0, Bin.s16(vtx, o + 2) / 256.0, Bin.s16(vtx, o + 4) / 256.0};
        }
        if (sid == 1) {
            if (stride == 12) return new double[] {Bin.f32(vtx, o), Bin.f32(vtx, o + 4), Bin.f32(vtx, o + 8)};
            return new double[] {Bin.s8(vtx, o) / 64.0, Bin.s8(vtx, o + 1) / 64.0, Bin.s8(vtx, o + 2) / 64.0};
        }
        if (sid >= 3 && sid <= 8) return new double[] {Bin.s16(vtx, o) / 1024.0, Bin.s16(vtx, o + 2) / 1024.0};
        return null;
    }

    // ------------------------------------------------------------------------------------------------ maths (as Python)
    /** capgpk.mat_mul: sum(a[i][k] * b[k][j] for k in range(4)) - CPython's sum of floats (compensated: Py.sum). */
    static double[][] matMul(double[][] a, double[][] b) {
        final double[][] r = new double[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                r[i][j] = Py.sum(a[i][0] * b[0][j], a[i][1] * b[1][j], a[i][2] * b[2][j], a[i][3] * b[3][j]);
            }
        return r;
    }

    /** meleepack.inv4: Gauss-Jordan, partial pivot (the first largest), a zero pivot 1e-12. */
    static double[][] inv4(double[][] m) {
        final double[][] a = new double[4][8];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) a[i][j] = m[i][j];
            for (int j = 0; j < 4; j++) a[i][4 + j] = i == j ? 1.0 : 0.0;
        }
        for (int c = 0; c < 4; c++) {
            int p = c;
            for (int r = c + 1; r < 4; r++)
                if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
            final double[] t = a[c];
            a[c] = a[p];
            a[p] = t;
            double pv = a[c][c];
            if (pv == 0.0) pv = 1e-12;
            for (int k = 0; k < 8; k++) a[c][k] = a[c][k] / pv;
            for (int r = 0; r < 4; r++) {
                if (r == c) continue;
                final double f = a[r][c];
                final double[] row = new double[8];
                for (int k = 0; k < 8; k++) row[k] = a[r][k] - f * a[c][k];
                a[r] = row;
            }
        }
        final double[][] out = new double[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) out[i][j] = a[i][4 + j];
        return out;
    }

    static int[][] primTris(int prim, int[] ind) {
        final List<int[]> tris = new ArrayList<>();
        if (prim == 0) {
            for (int k = 0; k + 2 < ind.length; k += 3) tris.add(new int[] {ind[k], ind[k + 1], ind[k + 2]});
        } else if (prim == 1) {
            for (int i = 0; i < ind.length - 2; i++) {
                final int a = ind[i], b = ind[i + 1], c = ind[i + 2];
                if (a == b || b == c || a == c) continue;
                tris.add(i % 2 == 0 ? new int[] {a, b, c} : new int[] {b, a, c});
            }
        } else if (prim == 2) {
            for (int i = 1; i < ind.length - 1; i++) tris.add(new int[] {ind[0], ind[i], ind[i + 1]});
        }
        final List<int[]> ok = new ArrayList<>();
        for (int[] t : tris)
            if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) ok.add(t);
        return ok.toArray(new int[0][]);
    }

    // ------------------------------------------------------------------------------------------------ the pack
    static final class Built {
        byte[] gpk;
        int nn;
        int[] parents;
        float[][] inv;                  // nodes x 16
        float[][] V;                    // written vertices' positions
        int[][] J;                      // bones (0 where none)
        float[][] W;
        float[][][] idle, big;          // keys x nodes x 7
    }

    static final class LE {
        final ByteArrayOutputStream o = new ByteArrayOutputStream();
        final ByteBuffer bb = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN);

        LE u32(int v) {
            bb.clear();
            bb.putInt(v);
            o.write(bb.array(), 0, 4);
            return this;
        }

        LE f32(double v) {
            return u32(Float.floatToRawIntBits((float) v));
        }

        LE u16(int v) {
            o.write(v & 0xFF);
            o.write((v >>> 8) & 0xFF);
            return this;
        }

        LE u8(int v) {
            o.write(v & 0xFF);
            return this;
        }

        LE bytes(byte[] b) {
            o.write(b, 0, b.length);
            return this;
        }

        LE pad4() {
            while ((o.size() & 3) != 0) o.write(0);
            return this;
        }

        int size() {
            return o.size();
        }

        byte[] get() {
            return o.toByteArray();
        }
    }

    /** sidefig173.py's two files for one sidekick: {.gpk, .gev}. */
    public static byte[][] make(Source s, Kind k) throws IOException {
        final String lc = k.cls.toLowerCase(Locale.ROOT);  // (lower case: as Nl.Host reads the disc on the headset)
        final Nl.Shier h = Nl.shier(s.read("art/animation/" + lc + ".shier"));
        final byte[] sanim = s.read("art/animation/" + lc + ".sanim");
        final Glg model = glg(s.read("art/characters/" + lc + "/" + lc + "_blend.glg"));
        final LinkedHashMap<Integer, byte[]> tex = glt(s.read("art/characters/" + lc + "/" + lc + ".glt"));
        if (k.kit != null) {  // (its kit: in for its default's texture of the same size, as sidefig173.py)
            final LinkedHashMap<Integer, byte[]> kt = glt(s.read("art/characters/" + lc + "/" + k.kit.toLowerCase(Locale.ROOT)));
            final byte[] rec = kt.values().iterator().next();
            for (Map.Entry<Integer, byte[]> e : tex.entrySet())
                if (e.getValue().length == rec.length) e.setValue(rec);
        }
        final Built bt = pack(h, sanim, model, tex, "strikers_" + k.name);
        return new byte[][] {bt.gpk, gev(bt)};
    }

    static Built pack(Nl.Shier h, byte[] sanim, Glg model, LinkedHashMap<Integer, byte[]> tex, String name) throws IOException {
        final int nn = h.ids.length;
        final Map<Integer, Integer> nodeOf = new HashMap<>();
        for (int i = 0; i < nn; i++) nodeOf.put(h.ids[i], i);  // (a later same id wins, as the dict)
        final Skin sk = model.skin;
        final byte[] vtx = model.vtx;
        // bones: bind world per node
        final double[][][] bw = new double[nn][][];
        for (int i = 0; i < nn; i++) {
            final int par = h.parents[i];
            if (sk.binds.containsKey(h.ids[i])) {
                bw[i] = copy(sk.binds.get(h.ids[i]));
            } else if (par < 0) {
                bw[i] = copy(RZ90_INV);
            } else {
                final double[] t = h.trans[i];
                final double[][] local = {{1.0, 0, 0, 0}, {0, 1.0, 0, 0}, {0, 0, 1.0, 0}, {t[0], t[1], t[2], 1.0}};
                bw[i] = matMul(local, bw[par]);
            }
        }
        // soft weights per pool vertex
        final Map<Integer, Integer> order = new HashMap<>();
        for (int[] bv : sk.maps.get(0)) order.put(bv[1], bv[0]);
        final Map<Integer, List<double[]>> softW = new HashMap<>();
        for (int k = 0; k < sk.pairs.size(); k++) {
            final Integer bone = order.get(k);
            if (bone == null || !nodeOf.containsKey(bone)) continue;
            for (int[] vw : sk.pairs.get(k))
                softW.computeIfAbsent(vw[0], x -> new ArrayList<>()).add(new double[] {nodeOf.get(bone), vw[1] / 65535.0});
        }
        final int pelvis = nodeOf.get(Py.lowerHash("bip01 pelvis"));
        final List<double[][]> pP = new ArrayList<>(), pN = new ArrayList<>(), pUV = new ArrayList<>();
        final List<List<List<double[]>>> pW = new ArrayList<>();
        final List<int[][]> pT = new ArrayList<>();
        final List<Integer> pTex = new ArrayList<>();
        for (int pi = 0; pi < model.packets.size(); pi++) {
            final Packet p = model.packets.get(pi);
            final boolean stitched = ((p.raster >>> 20) & 1) != 0;
            final int[] ind = p.ind;
            Integer[] boneOfEntry = null;
            if (stitched) {
                final byte[] data = sk.stitch.getOrDefault(pi, new byte[0]);
                final Map<Integer, Integer> pmap = new HashMap<>();
                for (int[] bv : sk.maps.get(pi + 1)) pmap.put(bv[1], bv[0]);
                boneOfEntry = new Integer[ind.length];
                for (int k = 0; k < ind.length; k++) boneOfEntry[k] = k < data.length ? pmap.get(data[k] & 0xFF) : null;
            }
            final Map<Long, Integer> remap = new HashMap<>();
            final List<double[]> P = new ArrayList<>(), N = new ArrayList<>(), UV = new ArrayList<>();
            final List<List<double[]>> W = new ArrayList<>();
            final int[] local = new int[ind.length];
            for (int k = 0; k < ind.length; k++) {
                final int v = ind[k];
                List<double[]> wl;
                long key;
                if (stitched) {
                    final Integer b = boneOfEntry[k];
                    wl = new ArrayList<>();
                    if (b != null && nodeOf.containsKey(b)) wl.add(new double[] {nodeOf.get(b), 1.0});
                    key = b == null ? (((long) v) << 34) | 3L : (((long) v) << 34) | ((b & 0xFFFFFFFFL) << 2);
                } else {
                    wl = softW.getOrDefault(v, new ArrayList<>());
                    key = (((long) v) << 34) | 1L;
                }
                Integer at = remap.get(key);
                if (at == null) {
                    at = P.size();
                    remap.put(key, at);
                    final int[] s0 = p.streams.get(0);
                    double[] pos;
                    if (!stitched && v < sk.verts.size()) pos = sk.verts.get(v)[0];
                    else pos = stream(vtx, 0, s0[0], s0[1], v);
                    final int[] s1 = p.streams.get(1);
                    final double[] nrm = s1 != null ? stream(vtx, 1, s1[0], s1[1], v) : new double[] {0.0, 0.0, 1.0};
                    final int[] s3 = p.streams.get(3);
                    final double[] uv = s3 != null ? stream(vtx, 3, s3[0], s3[1], v) : new double[] {0.0, 0.0};
                    if (wl.isEmpty()) {
                        wl = new ArrayList<>();
                        wl.add(new double[] {pelvis, 1.0});
                    }
                    P.add(pos.clone());
                    double ln = Math.sqrt(Py.sum(nrm[0] * nrm[0], nrm[1] * nrm[1], nrm[2] * nrm[2]));
                    if (ln == 0.0) ln = 1.0;
                    N.add(new double[] {nrm[0] / ln, nrm[1] / ln, nrm[2] / ln});
                    UV.add(uv);
                    W.add(wl);
                }
                local[k] = at;
            }
            pP.add(P.toArray(new double[0][]));
            pN.add(N.toArray(new double[0][]));
            pUV.add(UV.toArray(new double[0][]));
            pW.add(W);
            pT.add(primTris(p.prim, local));
            pTex.add(p.tex0);
        }
        // winding: GX order vs the model's own normals
        int agree = 0, tot = 0;
        for (int q = 0; q < pP.size(); q++) {
            final double[][] P = pP.get(q), N = pN.get(q);
            for (int[] t : pT.get(q)) {
                final double[] u = new double[3], w = new double[3];
                for (int j = 0; j < 3; j++) {
                    u[j] = P[t[1]][j] - P[t[0]][j];
                    w[j] = P[t[2]][j] - P[t[0]][j];
                }
                final double[] cr = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                final double[] ns = new double[3];
                for (int j = 0; j < 3; j++) ns[j] = N[t[0]][j] + N[t[1]][j] + N[t[2]][j];
                final double d = Py.sum(cr[0] * ns[0], cr[1] * ns[1], cr[2] * ns[2]);
                tot++;
                if (d > 0) agree++;
            }
        }
        final double gxCcw = (double) agree / Math.max(tot, 1);
        // stand it on the floor
        double zmin = Double.POSITIVE_INFINITY;
        for (double[][] P : pP)
            for (double[] v : P) zmin = Math.min(zmin, v[2]);
        final double dz = -zmin;
        for (int i = 0; i < nn; i++)
            if (h.parents[i] >= 0) bw[i][3][2] += dz;
        for (double[][] P : pP)
            for (double[] v : P) v[2] = v[2] + dz;
        // write
        final Built bt = new Built();
        bt.nn = nn;
        bt.parents = h.parents.clone();
        bt.inv = new float[nn][16];
        final LE nodes = new LE();
        for (int i = 0; i < nn; i++) {
            final int par = h.parents[i];
            final double[] trans;
            if (par >= 0) trans = matMul(bw[i], inv4(bw[par]))[3];
            else trans = bw[i][3];
            final double[][] ib = inv4(bw[i]);
            nodes.u32(Py.lowerHash(String.format(Locale.ROOT, "cap/%s/%08x", name, h.ids[i]))).u32(par);
            nodes.f32(trans[0]).f32(trans[1]).f32(trans[2]).u32(1);
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) {
                    nodes.f32(ib[r][c]);
                    bt.inv[i][r * 4 + c] = (float) ib[r][c];
                }
            nodes.u32(h.ids[i]);
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) nodes.f32(bw[i][r][c]);
        }
        final List<Integer> used = new ArrayList<>();
        for (int t : pTex)
            if (!used.contains(t)) used.add(t);
        final LE texs = new LE();
        int ntex = 0;
        for (int t : used) {
            final byte[] rec = tex.get(t);
            if (rec == null) continue;
            texs.u32(t).u32(rec.length).bytes(rec).pad4();
            ntex++;
        }
        final LE pk = new LE();
        int npk = 0;
        double zmax = Double.NEGATIVE_INFINITY, zlo = Double.POSITIVE_INFINITY;
        final List<float[]> V = new ArrayList<>(), Wt = new ArrayList<>();
        final List<int[]> J = new ArrayList<>();
        for (int q = 0; q < pP.size(); q++) {
            int[][] tris = pT.get(q);
            if (tris.length == 0) continue;
            if (gxCcw > 0.5) {
                final int[][] r = new int[tris.length][];
                for (int i = 0; i < tris.length; i++) r[i] = new int[] {tris[i][0], tris[i][2], tris[i][1]};
                tris = r;
            }
            final int[] flat = new int[tris.length * 3];
            for (int i = 0; i < tris.length; i++) System.arraycopy(tris[i], 0, flat, i * 3, 3);
            final int[] strip = Geo.stripify(flat);
            if (!Geo.checkStrip(strip, flat)) throw new IOException("strip check failed");
            final double[][] P = pP.get(q), N = pN.get(q), UV = pUV.get(q);
            final List<List<double[]>> W = pW.get(q);
            final LE body = new LE();
            for (int i = 0; i < P.length; i++) {
                final int[] bones = {0xFF, 0xFF, 0xFF, 0xFF};
                final double[] wts = new double[4];
                final List<double[]> ws = new ArrayList<>(W.get(i));
                ws.sort((x, y) -> Double.compare(-x[1], -y[1]));  // (stable: ties keep their order)
                final List<double[]> top = ws.subList(0, Math.min(4, ws.size()));
                final double[] tw = new double[top.size()];
                for (int qq = 0; qq < tw.length; qq++) tw[qq] = top.get(qq)[1];
                double totw = Py.sum(tw);
                if (totw == 0.0) totw = 1.0;
                for (int qq = 0; qq < top.size(); qq++) {
                    bones[qq] = (int) top.get(qq)[0];
                    wts[qq] = top.get(qq)[1] / totw;
                }
                final double[] n = N[i];
                body.f32(P[i][0]).f32(P[i][1]).f32(P[i][2]).f32(-n[0]).f32(-n[1]).f32(-n[2]).f32(UV[i][0]).f32(UV[i][1]);
                for (int bb : bones) body.u8(bb);
                for (double w : wts) body.f32(w);
                zmax = Math.max(zmax, P[i][2]);
                zlo = Math.min(zlo, P[i][2]);
                V.add(new float[] {(float) P[i][0], (float) P[i][1], (float) P[i][2]});
                final int[] jj = new int[4];
                final float[] ww = new float[4];
                for (int qq = 0; qq < 4; qq++) {
                    jj[qq] = bones[qq] < nn ? bones[qq] : 0;
                    ww[qq] = bones[qq] < nn ? (float) wts[qq] : 0.0f;
                }
                J.add(jj);
                Wt.add(ww);
            }
            for (int v : strip) body.u16(v);
            body.pad4();
            pk.u32(pTex.get(q)).u32(P.length).u32(strip.length).bytes(body.get());
            npk++;
        }
        // its own idle and its big move (own_idle / own_run)
        final LE anims = new LE();
        final String[][] clips = {{"own_idle", "idle"}, {"own_run", BIG}};
        final float[][][][] rowsOut = new float[2][][][];
        for (int a = 0; a < 2; a++) {
            final Nl.Clip clip = Nl.clip(sanim, clips[a][1]);
            if (clip == null) throw new IOException("no clip " + clips[a][1]);
            final int nkeys = clip.keys();
            final float[][][] rows = new float[nkeys][nn][7];
            final LE body = new LE();
            for (int kk = 0; kk < nkeys; kk++) {
                final double t = nkeys > 1 ? (double) kk / (nkeys - 1) : 0.0;
                final Nl.Pose pose = clip.at(t);
                for (int i = 0; i < nn; i++) {
                    double[] q = i < pose.quat.length ? pose.quat[i] : null;
                    final double[] tr = i < pose.trans.length ? pose.trans[i] : null;
                    if (q == null) q = new double[] {0.0, 0.0, 0.0, 1.0};
                    double n = Math.sqrt(Py.sum(q[0] * q[0], q[1] * q[1], q[2] * q[2], q[3] * q[3]));
                    if (n == 0.0) n = 1.0;
                    final double[] tv = tr != null ? tr : h.trans[i];
                    final double[] key = {q[0] / n, q[1] / n, q[2] / n, q[3] / n, tv[0], tv[1], tv[2]};
                    for (int c = 0; c < 7; c++) {
                        body.f32(key[c]);
                        rows[kk][i][c] = (float) key[c];
                    }
                }
            }
            final byte[] nm = new byte[48];
            final byte[] an = clips[a][0].getBytes(StandardCharsets.US_ASCII);
            System.arraycopy(an, 0, nm, 0, an.length);
            anims.bytes(nm).u32(nkeys).bytes(body.get());
            final byte[] has = new byte[nn];
            java.util.Arrays.fill(has, (byte) 1);
            anims.bytes(has);
            for (int z = nn; (z & 3) != 0; z++) anims.u8(0);
            rowsOut[a] = rows;
        }
        final LE head = new LE();
        head.bytes("GPK2".getBytes(StandardCharsets.US_ASCII)).u32(2).u32(nn).u32(npk).u32(ntex).u32(2).f32(zmax - zlo);
        final byte[] nb = new byte[32];
        final byte[] nmb = name.getBytes(StandardCharsets.UTF_8);
        System.arraycopy(nmb, 0, nb, 0, Math.min(31, nmb.length));
        head.bytes(nb).u32(1);
        final LE all = new LE();
        all.bytes(head.get()).bytes(nodes.get()).bytes(texs.get()).bytes(pk.get()).bytes(anims.get());
        bt.gpk = all.get();
        bt.V = V.toArray(new float[0][]);
        bt.J = J.toArray(new int[0][]);
        bt.W = Wt.toArray(new float[0][]);
        bt.idle = rowsOut[0];
        bt.big = rowsOut[1];
        return bt;
    }

    static double[][] copy(double[][] m) {
        final double[][] r = new double[m.length][];
        for (int i = 0; i < m.length; i++) r[i] = m[i].clone();
        return r;
    }

    // ------------------------------------------------------------------------------------------------ the pose file
    /** capsim.sample: a clip at t (its keys either side, rotations blended the short way and normalised). */
    static double[][] sample(float[][][] data, double t) {
        final int keys = data.length, nn = data[0].length;
        t = Math.min(Math.max(t, 0.0), 1.0);
        final double f = t * (keys - 1);
        final int k0 = Math.min((int) f, keys - 1), k1 = k0 + 1 < keys ? k0 + 1 : k0;
        final double fr = f - k0;
        final double[][] out = new double[nn][7];
        for (int i = 0; i < nn; i++) {
            final float[] a = data[k0][i], b = data[k1][i];
            final double dot = NpLike.dot4(a, b);
            final double s1 = dot < 0 ? -fr : fr;
            final double[] q = new double[4];
            for (int c = 0; c < 4; c++) q[c] = a[c] * (1 - fr) + b[c] * s1;
            double n = NpLike.norm4(q);
            if (n == 0.0) n = 1.0;
            for (int c = 0; c < 4; c++) out[i][c] = q[c] / n;
            for (int c = 4; c < 7; c++) out[i][c] = a[c] * (1 - fr) + b[c] * fr;
        }
        return out;
    }

    /** capsim.chain with an identity root. */
    static double[][][] chain(int[] parents, double[][] rows) {
        final int n = parents.length;
        final double[][][] world = new double[n][][];
        for (int i = 0; i < n; i++) {
            if (parents[i] < 0) {
                world[i] = new double[][] {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
                continue;
            }
            final double[] q = rows[i];
            final double[][] local = Py.quatRows(q[0], q[1], q[2], q[3]);
            local[3][0] = q[4];
            local[3][1] = q[5];
            local[3][2] = q[6];
            world[i] = NpLike.matmul(local, world[parents[i]]);
        }
        return world;
    }

    static byte[] gev(Built bt) {
        final int nn = bt.nn;
        final float[][][][] plan = {bt.idle, bt.idle, bt.big};
        final int[] kinds = {0, 0, 2};
        final LE frames = new LE();
        final List<int[]> segs = new ArrayList<>();
        int nf = 0;
        final double[][][] inv = new double[nn][4][4];
        for (int i = 0; i < nn; i++)
            for (int q = 0; q < 16; q++) inv[i][q / 4][q % 4] = bt.inv[i][q];
        for (int p = 0; p < 3; p++) {
            final float[][][] clip = plan[p];
            final int keys = clip.length, start = nf;
            for (int k = 0; k < keys; k++) {
                final double[][][] world = chain(bt.parents, sample(clip, keys > 1 ? k / (keys - 1.0) : 0.0));
                final double[][][] S = new double[nn][][];
                for (int i = 0; i < nn; i++) S[i] = NpLike.matmul(inv[i], world[i]);
                double low = Double.POSITIVE_INFINITY;
                for (int v = 0; v < bt.V.length; v++) {
                    double z = 0.0;
                    for (int kk = 0; kk < 4; kk++) {
                        final double[][] m = S[bt.J[v][kk]];
                        final double e = NpLike.einsumZ(bt.V[v], m);
                        z = z + bt.W[v][kk] * e;
                    }
                    if (z < low) low = z;
                }
                for (int i = 0; i < nn; i++) {
                    final double[][] m = world[i];
                    frames.f32(m[0][0]).f32(m[0][1]).f32(m[0][2]).f32(m[1][0]).f32(m[1][1]).f32(m[1][2]);
                    frames.f32(m[2][0]).f32(m[2][1]).f32(m[2][2]).f32(m[3][0]).f32(m[3][1]).f32(m[3][2] - low);
                }
                nf++;
            }
            if (!segs.isEmpty() && segs.get(segs.size() - 1)[2] == kinds[p]) segs.get(segs.size() - 1)[1] += keys;
            else segs.add(new int[] {start, keys, kinds[p]});
        }
        final LE g = new LE();
        g.bytes("GEV1".getBytes(StandardCharsets.US_ASCII)).u32(nn).u32(nf).f32(30.0).bytes(frames.get());
        g.bytes("SEG1".getBytes(StandardCharsets.US_ASCII)).u32(segs.size()).u32(0xFFFFFFFF);
        for (int[] s : segs) g.u32(s[0]).u32(s[1]).u32(s[2]);
        return g.get();
    }

    /**
     * numpy's small float64 products as the PC's numpy does them: its 4 x 4 products (OpenBLAS) the first product plain,
     * the rest fused multiply-adds - matched byte for byte by SideFigTest (Fma: exact on every device, Math.fma is API 33+); the
     * blend's dot and norm plainly in order (only the dot's sign matters; the norm matched as is).
     */
    static final class NpLike {
        static double dot4(float[] a, float[] b) {
            double s = 0.0;
            for (int c = 0; c < 4; c++) s += (double) a[c] * b[c];
            return s;
        }

        static double norm4(double[] q) {
            double s = 0.0;
            for (double v : q) s = s + v * v;
            return Math.sqrt(s);
        }

        static double[][] matmul(double[][] a, double[][] b) {
            final double[][] r = new double[4][4];
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    double s = a[i][0] * b[0][j];
                    for (int k = 1; k < 4; k++) s = Fma.fma(a[i][k], b[k][j], s);
                    r[i][j] = s;
                }
            return r;
        }

        /** einsum("ni,nij->nj") for column 2 (z): sum over i of (v, 1)[i] * m[i][2]. */
        static double einsumZ(float[] v, double[][] m) {
            double s = 0.0;
            s += v[0] * m[0][2];
            s += v[1] * m[1][2];
            s += v[2] * m[2][2];
            s += 1.0 * m[3][2];
            return s;
        }
    }
}
