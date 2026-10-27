package io.github.newcoke.strikers.guests.charged;

import io.github.newcoke.strikers.guests.Bin;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Set;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * One Charged character -> a GPK1 guest pack (tools/outside/guestpack.py, the same bytes): skeleton, inverse binds,
 * mesh with 4 bone weights per vertex, the character's own texture records as stored, and its animations sampled per
 * key. Little-endian, 4-byte aligned:
 * <pre>
 *   'GPK1' u32 version(1) u32 nodes u32 packets u32 textures u32 anims f32 height char name[32]
 *   node:    u32 id, i32 parent, f32 trans[3], u32 hasInvBind, f32 invBind[16] (row vectors)
 *   texture: u32 hash, u32 size, bytes[size] (big-endian Charged record), pad to 4
 *   packet:  u32 texHash, u32 verts, u32 indices, then verts x (f32 pos[3] nrm[3] uv[2], u8 node[4], f32 w[4]),
 *            u16 strip indices, pad to 4
 *   anim:    char name[48], u32 keys, then keys x nodes x (f32 q[4] (x y z w), f32 t[3]),
 *            u8 hasTrans[nodes] padded to 4 (0: use the node's bind translation)
 * </pre>
 * Numbers: the Python tool's doubles are kept as doubles here, in its order of operations, and rounded to float only
 * where it packs ('&lt;f'); floats it only passes through are copied bit for bit.
 */
final class GuestPack {
    private GuestPack() {}

    static final int SKIN = 0x8001B008, INV_BIND = 0x1B00A, BONE_MAP = 0x1B00B;
    static final int PACKETS = 0x1B004, STREAMS = 0x1B005, VERTS = 0x1B006, INDICES = 0x1B007, PARAMS = 0x1B016;
    static final int ANIM_NODE = 0x80017100, ROT_KEYS = 0x17101, TRANS_KEYS = 0x17102;

    /** Disc paths (Charged's FST, case as on the disc). */
    static String rlg(String name) {
        return "Art/characters/" + name + "/" + name + ".rlg";
    }

    static String rlt(String name) {
        return "Art/characters/" + name + "/" + name + ".rlt";
    }

    static String shier(String name) {
        return "Art/animation/" + name + ".shier";
    }

    static String sanim(String name) {
        return "Art/animation/" + name + ".sanim.zlib";
    }

    /** The whole pack. */
    static Out build(Source src, String name, Set<String> skipAnims) throws IOException {
        final Shier h = Shier.read(src.read(shier(name)));
        if (h.trans == null) throw new IOException(name + ".shier: no bind translations");
        final int n = h.n;
        final HashMap<Integer, Integer> nodeOf = new HashMap<>();
        for (int i = 0; i < n; i++) nodeOf.put(h.ids[i], i);

        // model: the top chunk's parts (first of each id), the skin (last skin chunk)
        final byte[] b = src.read(rlg(name));
        final Chunks.C top = Chunks.first(b, 0, b.length);
        final HashMap<Integer, byte[]> parts = new HashMap<>();
        Chunks.C skin = null;
        for (Chunks.C c : Chunks.list(b, top.ds, top.end)) {
            if (!parts.containsKey(c.id)) parts.put(c.id, c.data(b));
            if (c.id == SKIN) skin = c;
        }
        if (skin == null) throw new IOException(name + ".rlg: no skin");
        final HashMap<Integer, double[]> inv = new HashMap<>();
        final ArrayList<int[]> maps = new ArrayList<>();
        for (Chunks.C c : Chunks.list(b, skin.ds, skin.end)) {
            if (c.id == INV_BIND) {
                final int count = (c.end - c.ds) / 0x44;
                for (int k = 0; k < count; k++) {
                    final int o = c.ds + k * 0x44;
                    final double[] m = new double[16];
                    for (int j = 0; j < 16; j++) m[j] = Bin.f32(b, o + 4 + 4 * j);
                    inv.put(Bin.u32(b, o), inverse(m));
                }
            } else if (c.id == BONE_MAP) {
                final int count = (c.end - c.ds) / 4;
                final int[] m = new int[count];
                for (int j = 0; j < count; j++) m[j] = Bin.u32(b, c.ds + 4 * j);
                maps.add(m);
            }
        }

        final Out o = new Out();
        // header, filled in at the end (packets, anims, height)
        o.fixed("GPK1", 4).le32(1).le32(n).le32(0);
        final int ntOff = o.size();
        o.le32(0).le32(0).le32(0).fixed(name, 31).zeros(1);

        // nodes
        for (int i = 0; i < n; i++) {
            o.le32(h.ids[i]).le32(h.parents[i]);
            for (int j = 0; j < 3; j++) o.le32(Bin.u32(h.trans, i * 12 + 4 * j));
            final double[] ib = inv.get(h.ids[i]);
            o.le32(ib != null ? 1 : 0);
            for (int j = 0; j < 16; j++) o.lef32(ib != null ? (float) ib[j] : 0f);
        }

        // textures: every record of the character's bundle, as stored
        final byte[] tb = src.read(rlt(name));
        final long nt = Bin.u32L(tb, 4);
        final long base = 16 + 16 * nt;
        for (long i = 0; i < nt; i++) {
            final int e = (int) (16 + 16 * i);
            final int hsh = Bin.u32(tb, e);
            final long off = Bin.u32L(tb, e + 4), size = Bin.u32L(tb, e + 8);
            Bin.u32(tb, e + 12);  // (the Python tool unpacks all four: a short dictionary fails there)
            final byte[] rec = Chunks.slice(tb, base + off, base + off + size);
            o.le32(hsh).le32(rec.length).bytes(rec).align(4);
        }

        // packets: GX triangle strips with positions
        final byte[] pk = need(parts, PACKETS, name), st = need(parts, STREAMS, name);
        final byte[] vtx = need(parts, VERTS, name), idx = need(parts, INDICES, name);
        final byte[] prm = parts.containsKey(PARAMS) ? parts.get(PARAMS) : new byte[0];
        int npk = 0;
        double zmin = 1e9, zmax = -1e9;
        final int[] sAddr = new int[256], sStride = new int[256];
        final boolean[] has = new boolean[256];
        for (int i = 0; i < pk.length / 0x30; i++) {
            final int q = i * 0x30;
            final long ibo = Bin.u32L(pk, q), nv = Bin.u32L(pk, q + 4);
            final int nu = Bin.u16(pk, q + 8), prim = Bin.u8(pk, q + 10), ns = Bin.u8(pk, q + 11);
            final long soff = Bin.u32L(pk, q + 12), par = Bin.u32L(pk, q + 32);
            java.util.Arrays.fill(has, false);
            for (int s = 0; s < ns; s++) {
                final int so = (int) (soff + s * 8L);
                final int addr = Bin.u32(st, so), stride = Bin.u8(st, so + 5), sid = Bin.u8(st, so + 6);
                Bin.u8(st, so + 7);
                if (!has[sid]) {
                    has[sid] = true;
                    sAddr[sid] = addr;
                    sStride[sid] = stride;
                }
            }
            if (!has[1] || prim != 1) continue;
            if (maps.isEmpty()) throw new IOException(name + ".rlg: no bone maps");
            final int[] bmap = i < maps.size() ? maps.get(i) : maps.get(maps.size() - 1);
            final int th = par + 4 <= prm.length ? Bin.u32(prm, (int) par) : 0;
            o.le32(th).le32(nu).le32((int) nv);
            final boolean skinned = has[7] && has[5];
            for (int v = 0; v < nu; v++) {
                final int pa = sAddr[1] + v * sStride[1];
                for (int j = 0; j < 3; j++) o.le32(Bin.u32(vtx, pa + 4 * j));
                final float z = Bin.f32(vtx, pa + 8);
                if (has[2]) {
                    final int na = sAddr[2] + v * sStride[2];
                    for (int j = 0; j < 3; j++) o.le32(Bin.u32(vtx, na + 4 * j));
                } else {
                    o.lef32(0f).lef32(0f).lef32(1f);
                }
                if (has[4]) {
                    final int ua = sAddr[4] + v * sStride[4];
                    o.lef32((float) (Bin.s16(vtx, ua) / 1024.0)).lef32((float) (Bin.s16(vtx, ua + 2) / 1024.0));
                } else {
                    o.lef32(0f).lef32(0f);
                }
                int bones = 0xFFFFFFFF;
                final int[] ws = new int[4];
                if (skinned) {
                    final int ia = sAddr[7] + v * sStride[7], wa = sAddr[5] + v * sStride[5];
                    for (int j = 0; j < 4; j++) {
                        final int bi = Bin.u8(vtx, ia + j);
                        final int wbits = Bin.u32(vtx, wa + 4 * j);
                        if (Float.intBitsToFloat(wbits) > 0f && bi < bmap.length) {
                            final Integer node = nodeOf.get(bmap[bi]);
                            if (node != null) {
                                if (node > 255) throw new IOException(name + ": node " + node + " past a byte");
                                bones = (bones & ~(0xFF << (8 * j))) | (node << (8 * j));
                                ws[j] = wbits;
                            }
                        }
                    }
                }
                if (z < zmin) zmin = z;
                if (z > zmax) zmax = z;
                o.le32(bones);
                for (int j = 0; j < 4; j++) o.le32(ws[j]);
            }
            for (long k = 0; k < nv; k++) o.le16(Bin.u16(idx, (int) (ibo + 2 * k)));
            o.align(4);
            npk++;
        }

        // animations
        final int nanims = sampleAnims(src.read(sanim(name)), n, skipAnims, o);
        o.patchLe32(12, npk);
        o.patchLe32(ntOff, (int) nt);
        o.patchLe32(ntOff + 4, nanims);
        o.patchLe32(ntOff + 8, Float.floatToRawIntBits((float) (zmax - zmin)));
        return o;
    }

    private static byte[] need(HashMap<Integer, byte[]> parts, int id, String name) throws IOException {
        final byte[] p = parts.get(id);
        if (p == null) throw new IOException(name + ".rlg: no chunk " + Integer.toHexString(id));
        return p;
    }

    /** zlib.decompress(file[4:]) */
    static byte[] inflate(byte[] z) throws IOException {
        final Inflater inf = new Inflater();
        inf.setInput(z, 4, z.length - 4);
        final long hint = Bin.u32L(z, 0);  // (the unpacked size, as far as seen; only a first guess here)
        final ByteArrayOutputStream bo = new ByteArrayOutputStream(
                hint > 0 && hint < (32 << 20) && hint <= 64L * z.length ? (int) hint : Math.max(1 << 16, z.length * 4));
        final byte[] buf = new byte[1 << 16];
        try {
            while (!inf.finished()) {
                final int k = inf.inflate(buf);
                if (k == 0 && (inf.needsInput() || inf.needsDictionary())) throw new IOException("sanim: truncated zlib");
                bo.write(buf, 0, k);
            }
        } catch (DataFormatException e) {
            throw new IOException("sanim: " + e.getMessage());
        } finally {
            inf.end();
        }
        return bo.toByteArray();
    }

    /** guestpack.py sample_anims: every clip but the skipped ones, appended to o; returns how many. */
    static int sampleAnims(byte[] file, int nnodes, Set<String> skip, Out o) throws IOException {
        final byte[] d = inflate(file);
        int count = 0;
        for (Chunks.C c : Chunks.list(d, 0, d.length)) {
            final ArrayList<Chunks.C> sub = Chunks.list(d, c.ds, c.end);
            final byte[] nmData = sub.get(1).data(d);
            int nl = 0;
            while (nl < nmData.length && nmData[nl] != 0) nl++;
            final String nm = new String(nmData, 0, nl, java.nio.charset.StandardCharsets.UTF_8);
            if (skip.contains(nm)) continue;
            final byte[] hdr = sub.get(0).data(d);
            final long nkeysL = Bin.u32L(hdr, 8), an = Bin.u32L(hdr, 12);
            if (nkeysL > 1_000_000) throw new IOException("anim " + nm + ": " + nkeysL + " keys");
            final int nkeys = (int) nkeysL;
            final ArrayList<Chunks.C> nodes = new ArrayList<>();
            for (Chunks.C s : sub) if (s.id == ANIM_NODE) nodes.add(s);
            final int propsAt = sub.get(sub.size() - 1).ds;
            if (propsAt + 4 * an > d.length) throw new IOException("anim " + nm + ": props past the end");
            if (nodes.size() > an) throw new IOException("anim " + nm + ": more nodes than props");
            final int nn = nodes.size();
            final double[][][] rot = new double[nn][][];   // per node chunk: keys x (x y z w), or null
            final int[][] tr = new int[nn][];              // per node chunk: offsets of 3-float keys, or null
            final int[] trCount = new int[nn];
            for (int i = 0; i < nn; i++) {
                final Chunks.C nd = nodes.get(i);
                final int pr = Bin.u32(d, propsAt + 4 * i);
                double[][] r = null;
                int[] t = null;
                for (Chunks.C s : Chunks.list(d, nd.ds, nd.end)) {
                    if (s.id == ROT_KEYS) {
                        final int nk = (pr & 2) != 0 ? 1 : nkeys;
                        final double[][] keys = new double[nk][];
                        final int sds = s.ds;
                        for (int k = 0; k < nk; k++) {
                            if ((pr & 1) != 0) {
                                keys[k] = angleKey(Bin.u16(d, sds + 2 * k));
                            } else if ((pr & 0x10) != 0) {
                                final int q = sds + 8 * k;
                                keys[k] = new double[] {Bin.s16(d, q) / 32768.0, Bin.s16(d, q + 2) / 32768.0,
                                        Bin.s16(d, q + 4) / 32768.0, Bin.s16(d, q + 6) / 32768.0};
                            } else if ((pr & 0x20) != 0) {
                                final int q = sds + 6 * k;
                                if (q + 6 > d.length) throw new IOException("anim " + nm + ": packed key past the end");
                                final int r0 = d[q] & 0xFF, r1 = d[q + 1] & 0xFF, r2 = d[q + 2] & 0xFF;
                                final int r3 = d[q + 3] & 0xFF, r4 = d[q + 4] & 0xFF, r5 = d[q + 5] & 0xFF;
                                keys[k] = new double[] {(short) ((r0 << 8) | (r1 & 0xF0)) / 32768.0,
                                        (short) ((r2 << 8) | ((r1 << 4) & 0xFF)) / 32768.0,
                                        (short) ((r3 << 8) | (r4 & 0xF0)) / 32768.0,
                                        (short) ((r5 << 8) | ((r4 << 4) & 0xFF)) / 32768.0};
                            } else {
                                final int q = sds + 4 * k;
                                keys[k] = new double[] {Bin.s8(d, q) / 128.0, Bin.s8(d, q + 1) / 128.0,
                                        Bin.s8(d, q + 2) / 128.0, Bin.s8(d, q + 3) / 128.0};
                            }
                        }
                        r = keys;
                    } else if (s.id == TRANS_KEYS) {
                        final int k3 = Math.floorDiv(s.end - s.ds, 12);
                        t = new int[Math.max(k3, 0)];
                        for (int k = 0; k < t.length; k++) {
                            t[k] = s.ds + 12 * k;
                            if (t[k] + 12 > d.length) throw new IOException("anim " + nm + ": key past the end");
                        }
                    }
                }
                rot[i] = r;
                tr[i] = t;
                trCount[i] = t == null ? 0 : t.length;
            }
            // the clip
            final int nb = Math.min(nl, 47);  // nm.encode()[:47] in '48s'
            o.bytes(nmData, 0, nb).zeros(48 - nb).le32(nkeys);
            final double[] sq = new double[4];
            for (int k = 0; k < nkeys; k++) {
                for (int i = 0; i < nnodes; i++) {
                    final double[][] r = i < nn ? rot[i] : null;
                    final double[] q = r != null && r.length > 0 ? r[Math.min(k, r.length - 1)] : IDENTITY;
                    for (int j = 0; j < 4; j++) sq[j] = q[j] * q[j];
                    double len = Math.sqrt(pySum(sq));
                    if (len == 0.0) len = 1.0;
                    for (int j = 0; j < 4; j++) o.lef32((float) (q[j] / len));
                    final int[] t = i < nn ? tr[i] : null;
                    if (t != null && t.length > 0) {
                        final int at = t[Math.min(k, t.length - 1)];
                        o.le32(Bin.u32(d, at)).le32(Bin.u32(d, at + 4)).le32(Bin.u32(d, at + 8));
                    } else {
                        o.le32(0).le32(0).le32(0);
                    }
                }
            }
            for (int i = 0; i < nnodes; i++) o.u8(i < nn && trCount[i] > 0 ? 1 : 0);
            o.align(4);
            count++;
        }
        return count;
    }

    private static final double[] IDENTITY = {0.0, 0.0, 0.0, 1.0};

    /**
     * A props &amp; 1 key: u16 angle about Z (Python: x * 2 * math.pi / 65536.0, then sin / cos of half). StrictMath
     * (fdlibm: the same doubles on every Java, the headset's ART too) is off by a last bit from the Windows CRT's for
     * 3265 of the 65536 angles, but after normalising and rounding to float all 65536 come out the same as the
     * Python tool's (tools/guestconv/test/ChargedAngleTest.java).
     */
    static double[] angleKey(int u) {
        final double a = (double) (u * 2) * Math.PI / 65536.0;
        final double half = a / 2;
        return new double[] {0.0, 0.0, StrictMath.sin(half), StrictMath.cos(half)};
    }

    /**
     * Python's sum() of floats from the default start 0 (CPython 3.12+: Neumaier-compensated, the compensation
     * added at the end when non-zero and finite).
     */
    static double pySum(double[] xs) {
        double f = 0.0 + xs[0];
        double c = 0.0;
        for (int i = 1; i < xs.length; i++) {
            final double x = xs[i];
            final double t = f + x;
            if (Math.abs(f) >= Math.abs(x)) c += (f - t) + x;
            else c += (x - t) + f;
            f = t;
        }
        if (c != 0.0 && Double.isFinite(c)) f += c;
        return f;
    }

    /** chargedpose.py inverse: 4x4 Gauss-Jordan with partial pivoting (first largest), row-major, in doubles. */
    static double[] inverse(double[] m) {
        final double[][] a = new double[4][8];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) a[i][j] = m[i * 4 + j];
            a[i][4 + i] = 1.0;
        }
        for (int c = 0; c < 4; c++) {
            int p = c;
            double best = Math.abs(a[c][c]);
            for (int r = c + 1; r < 4; r++) {
                final double v = Math.abs(a[r][c]);
                if (v > best) {
                    best = v;
                    p = r;
                }
            }
            final double[] sw = a[c];
            a[c] = a[p];
            a[p] = sw;
            final double pv = a[c][c];
            if (pv == 0.0) throw new ArithmeticException("singular bind matrix");
            for (int k = 0; k < 8; k++) a[c][k] = a[c][k] / pv;
            for (int r = 0; r < 4; r++) {
                if (r == c) continue;
                final double f = a[r][c];
                for (int k = 0; k < 8; k++) a[r][k] = a[r][k] - f * a[c][k];
            }
        }
        final double[] out = new double[16];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out[i * 4 + j] = a[i][4 + j];
        return out;
    }
}
