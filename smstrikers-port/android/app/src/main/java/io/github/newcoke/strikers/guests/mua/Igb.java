package io.github.newcoke.strikers.guests.mua;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * An Alchemy IGB file from Marvel: Ultimate Alliance (Wii), the object graph (tools/outside/mua/igb.py layer 1, read
 * the same way). The container is little-endian even on the Wii; only the GX payloads inside memory blocks are
 * big-endian. Objects keep their fields by slot; a memory block is a slice of the file.
 */
public final class Igb {
    static final int FLAG_HAS_INFO = 0x80000000, FLAG_HAS_EXTERNAL = 0x40000000, FLAG_HAS_POOL_NAMES = 0x10000000;

    /** An igObject: its class and fields by slot (Integer, Long, Double, String, double[], int[], long[], byte[]). */
    public static final class Obj {
        public final int index;
        public final String type;
        final Map<Integer, Object> f = new HashMap<>();
        final Map<Integer, String> t = new HashMap<>();
        Igb g;

        Obj(int index, String type) {
            this.index = index;
            this.type = type;
        }

        public Object get(int slot) {
            return f.get(slot);
        }

        public String fieldType(int slot) {
            return t.get(slot);
        }

        /** An int field (null when missing or not a number). */
        public Integer i(int slot) {
            final Object v = f.get(slot);
            if (v instanceof Integer) return (Integer) v;
            if (v instanceof Long) return (int) (long) (Long) v;
            return null;
        }

        public int i(int slot, int dflt) {
            final Integer v = i(slot);
            return v != null ? v : dflt;
        }

        public String s(int slot) {
            final Object v = f.get(slot);
            return v instanceof String ? (String) v : null;
        }

        public double[] vec(int slot) {
            final Object v = f.get(slot);
            return v instanceof double[] ? (double[]) v : null;
        }

        /** Follows an ObjectRef / MemoryRef field (null for none). */
        public Object ref(int slot) {
            final Object v = f.get(slot);
            if (!(v instanceof Integer)) return null;
            final int k = (Integer) v;
            if (k < 0) return null;
            return g.objects[k];
        }

        public Obj obj(int slot) {
            final Object r = ref(slot);
            return r instanceof Obj ? (Obj) r : null;
        }

        public Mem mem(int slot) {
            final Object r = ref(slot);
            return r instanceof Mem ? (Mem) r : null;
        }

        /** (slot, target) of every ref field, in slot order of the class's fields. */
        public List<Object> refs() {
            final List<Object> out = new ArrayList<>();
            for (int s : g.slotOrder(this)) {
                final String ft = t.get(s);
                if ("igObjectRefMetaField".equals(ft) || "igMemoryRefMetaField".equals(ft)) {
                    final Object v = f.get(s);
                    if (v instanceof Integer && (Integer) v >= 0) out.add(g.objects[(Integer) v]);
                }
            }
            return out;
        }

        @Override
        public String toString() {
            return "<" + type + " #" + index + ">";
        }
    }

    /** A memory block: bytes [off, off + len) of the file, its element type (e.g. "igVec3f"). */
    public static final class Mem {
        public final int index, off, len;
        public final String type;

        Mem(int index, int off, int len, String type) {
            this.index = index;
            this.off = off;
            this.len = len;
            this.type = type;
        }
    }

    static final class MetaObj {
        final String name;
        final String[] fType;
        final int[] fSlot, fSize;
        final int parent;

        MetaObj(String name, int n, int parent) {
            this.name = name;
            fType = new String[n];
            fSlot = new int[n];
            fSize = new int[n];
            this.parent = parent;
        }
    }

    public final byte[] d;
    public final int version, flags;
    final List<String> names = new ArrayList<>();
    final List<String> metaFields = new ArrayList<>();
    final List<MetaObj> metaObjects = new ArrayList<>();
    public final Object[] objects;
    public final Obj info;
    private final Map<String, Integer> metaIndex = new HashMap<>();

    public Igb(byte[] data) throws IOException {
        d = data;
        if (data.length < 48 || le32(0x28) != 0xFADA) throw new IOException("not an IGB (no 0xFADA cookie)");
        final int entryBufSize = le32(0), entryCount = le32(4), moBufSize = le32(8), moCount = le32(12);
        final int objBufSize = le32(16), memBufSize = le32(24), mfBufSize = le32(32), mfCount = le32(36);
        final int vf = le32(44);
        version = vf & 0xFFFF;
        flags = vf & 0xFFFF0000;
        int p = 48;
        if (version >= 8) {
            final int size = le32(p), cnt = le32(p + 4);
            int q = p + 8;
            for (int k = 0; k < cnt; k++) {
                final int ln = le32(q);
                names.add(cstr(q + 4, ln));
                q += 4 + ln;
            }
            p += size;
        }
        // meta fields
        int q = p + 12 * mfCount;
        for (int k = 0; k < mfCount; k++) {
            final int ln = le32(p + 12 * k);
            metaFields.add(cstr(q, ln));
            q += ln;
        }
        p += mfBufSize;
        // alignment
        p += le32(p);
        // meta objects
        q = p + 24 * moCount;
        for (int k = 0; k < moCount; k++) {
            final int r = p + 24 * k;
            final int ln = le32(r), nf = le32(r + 12), parent = le32(r + 16);
            final MetaObj mo = new MetaObj(cstr(q, ln), nf, parent);
            q += ln;
            for (int j = 0; j < nf; j++) {
                mo.fType[j] = metaFields.get(le16(q));
                mo.fSlot[j] = le16(q + 2);
                mo.fSize[j] = le16(q + 4);
                q += 6;
            }
            metaIndex.putIfAbsent(mo.name, metaObjects.size());
            metaObjects.add(mo);
        }
        p += moBufSize;
        if ((flags & FLAG_HAS_EXTERNAL) != 0) p += le32(p);
        if ((flags & FLAG_HAS_POOL_NAMES) != 0) p += le32(p);
        // entries
        final String[] eName = new String[entryCount];
        final List<Map<Integer, Integer>> eVals = new ArrayList<>();
        q = p;
        for (int k = 0; k < entryCount; k++) {
            final int ti = le32(q), size = le32(q + 4);
            final MetaObj mo = metaObjects.get(ti);
            eName[k] = mo.name;
            final Map<Integer, Integer> vals = new HashMap<>();
            int r = q + 8;
            for (int j = 0; j < mo.fType.length; j++) {
                final int z = mo.fSize[j];
                if (r + z > q + size) break;
                vals.put(mo.fSlot[j], z == 4 ? le32(r) : z == 2 ? (int) (short) le16(r) : (d[r] & 0xFF));
                r += (z + 3) & ~3;
            }
            eVals.add(vals);
            q += size;
        }
        p += entryBufSize;
        // index buffer
        final int[] index;
        if (version >= 6) {
            final int size = le32(p), cnt = le32(p + 4);
            index = new int[cnt];
            for (int k = 0; k < cnt; k++) index[k] = le16(p + 8 + 2 * k);
            p += size;
        } else {
            index = new int[entryCount];
            for (int k = 0; k < entryCount; k++) index[k] = k;
        }
        int infoSlot = -1;
        if ((flags & FLAG_HAS_INFO) != 0 && version >= 6) {
            infoSlot = le32(p);
            p += 4;
        }
        objects = new Object[index.length];
        q = p;
        for (int slot = 0; slot < index.length; slot++) {
            final String en = eName[index[slot]];
            if (!en.equals("igObjectDirEntry") && !en.endsWith("ObjectDirEntry")) continue;
            final int ti = le32(q), size = le32(q + 4);
            final MetaObj mo = metaObjects.get(ti);
            final Obj o = new Obj(slot, mo.name);
            o.g = this;
            int r = q + 8;
            final int end = q + size;
            for (int j = 0; j < mo.fType.length; j++) {
                final int z = mo.fSize[j];
                if (r + z > end) break;
                o.f.put(mo.fSlot[j], field(mo.fType[j], r, z));
                o.t.put(mo.fSlot[j], mo.fType[j]);
                r += (z + 3) & ~3;
            }
            objects[slot] = o;
            q += size;
        }
        if (q - p != objBufSize) throw new IOException("IGB object buffer size mismatch");
        p += objBufSize;
        q = p;
        for (int slot = 0; slot < index.length; slot++) {
            final String en = eName[index[slot]];
            if (!en.equals("igMemoryDirEntry")) continue;
            final Map<Integer, Integer> ev = eVals.get(index[slot]);
            final int size = ev.getOrDefault(7, 0);
            final int mt = ev.getOrDefault(10, -1);
            final String tname = mt >= 0 && mt < metaFields.size() ? metaFields.get(mt).replace("MetaField", "") : null;
            objects[slot] = new Mem(slot, q, size, tname);
            q += (size + 3) & ~3;
        }
        if (q - p != memBufSize) throw new IOException("IGB memory buffer size mismatch");
        info = infoSlot >= 0 && objects[infoSlot] instanceof Obj ? (Obj) objects[infoSlot] : null;
    }

    private List<Integer> slotOrder(Obj o) {
        final MetaObj mo = metaObjects.get(metaIndex.get(o.type));
        final List<Integer> out = new ArrayList<>();
        for (int s : mo.fSlot) if (o.t.containsKey(s) && !out.contains(s)) out.add(s);
        return out;
    }

    /** An object's field slots in its class's field order (igb.py's fields dict order). */
    List<Integer> metaSlots(Obj o) {
        return slotOrder(o);
    }

    // ------------------------------------------------------------------------------------------------ fields
    private Object field(String ft, int r, int z) {
        switch (ft) {
            case "igStringMetaField": {
                if (version >= 8) {
                    final int i = le32(r);
                    return i >= 0 && i < names.size() ? names.get(i) : null;
                }
                return null;
            }
            case "igUnsignedCharMetaField":
            case "igBoolMetaField":
                return scalar(r, z, 1, false);
            case "igCharMetaField":
                return scalar(r, z, 1, true);
            case "igShortMetaField":
                return scalar(r, z, 2, true);
            case "igUnsignedShortMetaField":
                return scalar(r, z, 2, false);
            case "igIntMetaField":
            case "igEnumMetaField":
            case "igLongMetaField":
            case "igObjectRefMetaField":
            case "igMemoryRefMetaField":
            case "igRawRefMetaField":
            case "igStringMetaField2":
                if (z == 8 && ft.equals("igLongMetaField")) return le64(r);
                return scalar(r, z, 4, true);
            case "igUnsignedIntMetaField":
            case "igUnsignedLongMetaField":
                if (z == 8) return le64(r);
                return scalar(r, z, 4, false);
            case "igFloatMetaField":
                if (z == 4) return (double) Float.intBitsToFloat(le32(r));
                return scalar(r, z, 4, true);
            case "igDoubleMetaField":
                if (z == 8) return Double.longBitsToDouble(le64(r));
                return scalarOther(r, z);
            case "igVec2fMetaField":
                if (z == 8) return floats(r, 2);
                break;
            case "igVec3fMetaField":
                if (z == 12) return floats(r, 3);
                break;
            case "igVec4fMetaField":
                if (z == 16) return floats(r, 4);
                break;
            case "igMatrix44fMetaField":
                if (z == 64) return floats(r, 16);
                break;
            case "igVec3ucMetaField":
                if (z == 3) return new int[] {d[r] & 0xFF, d[r + 1] & 0xFF, d[r + 2] & 0xFF};
                break;
            case "igVec4ucMetaField":
                if (z == 4) return new int[] {d[r] & 0xFF, d[r + 1] & 0xFF, d[r + 2] & 0xFF, d[r + 3] & 0xFF};
                break;
            case "igVec4iMetaField":
                if (z == 16) return new int[] {le32(r), le32(r + 4), le32(r + 8), le32(r + 12)};
                break;
            default:
                break;
        }
        if (ft.endsWith("ArrayMetaField")) {
            // (only kept as raw ints / floats: nothing here needs them)
            final byte[] b = new byte[z];
            System.arraycopy(d, r, b, 0, z);
            return b;
        }
        final byte[] b = new byte[z];
        System.arraycopy(d, r, b, 0, z);
        return b;
    }

    /** A scalar whose struct size is `want` bytes; igb.py's fallbacks when the stored size differs. */
    private Object scalar(int r, int z, int want, boolean signed) {
        if (z == want) {
            if (want == 1) return signed ? (int) d[r] : (d[r] & 0xFF);
            if (want == 2) return signed ? (int) (short) le16(r) : le16(r);
            return signed ? le32(r) : (Object) (long) (le32(r) & 0xFFFFFFFFL);
        }
        return scalarOther(r, z);
    }

    private Object scalarOther(int r, int z) {
        if (z == 1) return d[r] & 0xFF;
        if (z == 2) return (int) (short) le16(r);
        if (z == 4) return le32(r);
        if (z == 8) return le64(r);
        final byte[] b = new byte[z];
        System.arraycopy(d, r, b, 0, z);
        return b;
    }

    private double[] floats(int r, int n) {
        final double[] v = new double[n];
        for (int k = 0; k < n; k++) v[k] = Float.intBitsToFloat(le32(r + 4 * k));
        return v;
    }

    // ------------------------------------------------------------------------------------------------ helpers
    public int le32(int o) {
        return (d[o] & 0xFF) | ((d[o + 1] & 0xFF) << 8) | ((d[o + 2] & 0xFF) << 16) | ((d[o + 3] & 0xFF) << 24);
    }

    public int le16(int o) {
        return (d[o] & 0xFF) | ((d[o + 1] & 0xFF) << 8);
    }

    public long le64(int o) {
        return (le32(o) & 0xFFFFFFFFL) | ((long) le32(o + 4) << 32);
    }

    public float lef32(int o) {
        return Float.intBitsToFloat(le32(o));
    }

    private String cstr(int o, int max) {
        int e = o;
        while (e < o + max && e < d.length && d[e] != 0) e++;
        return new String(d, o, e - o, StandardCharsets.ISO_8859_1);
    }

    /** Every object of these classes, in slot order. */
    public List<Obj> ofType(String... types) {
        final List<Obj> out = new ArrayList<>();
        for (Object o : objects) {
            if (!(o instanceof Obj)) continue;
            for (String t : types) {
                if (((Obj) o).type.equals(t)) {
                    out.add((Obj) o);
                    break;
                }
            }
        }
        return out;
    }

    /** True if this class derives from igObjectList (through the meta objects' parents). */
    boolean isObjectList(String type) {
        for (int i = 0; i < metaObjects.size(); i++) {
            if (!metaObjects.get(i).name.equals(type)) continue;
            int k = i;
            while (k >= 0) {
                if (metaObjects.get(k).name.equals("igObjectList")) return true;
                k = metaObjects.get(k).parent;
            }
            return false;
        }
        return false;
    }

    /** igb.list_items for an object list: its items (null entries kept); null when it isn't an object list. */
    public List<Obj> items(Obj list) {
        final List<Obj> out = new ArrayList<>();
        if (list == null) return out;
        final int cnt = list.i(2, 0);
        final Mem m = list.mem(4);
        if (m == null || cnt <= 0) return out;
        if (list.type.equals("igObjectList") || (list.type.endsWith("List") && isObjectList(list.type))) {
            for (int k = 0; k < cnt; k++) {
                final int id = le32(m.off + 4 * k);
                out.add(id >= 0 && objects[id] instanceof Obj ? (Obj) objects[id] : null);
            }
            return out;
        }
        return null;
    }

    /** A data list's elements (igVec3fList ...) as doubles: count x k (floats as float32 values, ints as ints). */
    public double[][] listArray(Obj list) throws IOException {
        if (list == null) return null;
        final String et;
        final int k;
        switch (list.type) {
            case "igVec3fList": et = "f"; k = 3; break;
            case "igVec2fList": et = "f"; k = 2; break;
            case "igVec4fList": case "igQuaternionfList": et = "f"; k = 4; break;
            case "igFloatList": et = "f"; k = 1; break;
            case "igIntList": et = "i"; k = 1; break;
            case "igUnsignedIntList": et = "I"; k = 1; break;
            case "igUnsignedCharList": et = "B"; k = 1; break;
            case "igCharList": et = "b"; k = 1; break;
            case "igShortList": et = "h"; k = 1; break;
            case "igUnsignedShortList": et = "H"; k = 1; break;
            case "igLongList": et = "q"; k = 1; break;
            case "igVec4ucList": et = "B"; k = 4; break;
            case "igVec3ucList": et = "B"; k = 3; break;
            case "igMatrix44fList": et = "f"; k = 16; break;
            default: throw new IOException("unknown list type " + list.type);
        }
        final int cnt = list.i(2, 0);
        final Mem m = list.mem(4);
        if (m == null || cnt <= 0) return new double[0][k];
        final double[][] out = new double[cnt][k];
        int o = m.off;
        for (int i = 0; i < cnt; i++) {
            for (int j = 0; j < k; j++) {
                switch (et) {
                    case "f": out[i][j] = lef32(o); o += 4; break;
                    case "i": out[i][j] = le32(o); o += 4; break;
                    case "I": out[i][j] = le32(o) & 0xFFFFFFFFL; o += 4; break;
                    case "B": out[i][j] = d[o] & 0xFF; o += 1; break;
                    case "b": out[i][j] = d[o]; o += 1; break;
                    case "h": out[i][j] = (short) le16(o); o += 2; break;
                    case "H": out[i][j] = le16(o); o += 2; break;
                    default: out[i][j] = le64(o); o += 8; break;
                }
            }
        }
        return out;
    }

    /** A memory block as count x k floats (float32 values). */
    public double[][] memFloats(Mem m, int k) {
        final int n = m.len / (4 * k);
        final double[][] out = new double[n][k];
        for (int i = 0; i < n; i++) for (int j = 0; j < k; j++) out[i][j] = lef32(m.off + 4 * (i * k + j));
        return out;
    }

    public long[] memLongs(Mem m) {
        final int n = m.len / 8;
        final long[] out = new long[n];
        for (int i = 0; i < n; i++) out[i] = le64(m.off + 8 * i);
        return out;
    }

    public int[] memInts(Mem m) {
        final int n = m.len / 4;
        final int[] out = new int[n];
        for (int i = 0; i < n; i++) out[i] = le32(m.off + 4 * i);
        return out;
    }

    public int[] memU16(Mem m) {
        final int n = m.len / 2;
        final int[] out = new int[n];
        for (int i = 0; i < n; i++) out[i] = le16(m.off + 2 * i);
        return out;
    }

    public byte[] bytes(Mem m) {
        final byte[] b = new byte[m.len];
        System.arraycopy(d, m.off, b, 0, m.len);
        return b;
    }
}
