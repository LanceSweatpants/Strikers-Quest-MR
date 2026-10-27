package io.github.newcoke.strikers.guests.render;

/** A bounding volume tree over a mesh's triangles, for "is anything in the way" rays (shadows, sky). */
final class Bvh {
    private final float[] p;
    private final int[] tri;
    private int[] order;  // triangle ids, leaves hold runs of it
    // per node: bounds (6 floats), then left child / first triangle, count (0: inner node)
    private float[] box;
    private int[] left, count;
    private int nodes;

    /** use[t]: whether triangle t blocks rays (null: all). */
    Bvh(float[] p, int[] tri, int nt, boolean[] use) {
        this.p = p;
        this.tri = tri;
        int n = 0;
        for (int t = 0; t < nt; t++)
            if (use == null || use[t]) n++;
        order = new int[n];
        n = 0;
        for (int t = 0; t < nt; t++)
            if (use == null || use[t]) order[n++] = t;
        final float[] cen = new float[nt * 3];
        for (int t = 0; t < nt; t++)
            for (int c = 0; c < 3; c++)
                cen[t * 3 + c] = (p[tri[t * 3] * 3 + c] + p[tri[t * 3 + 1] * 3 + c] + p[tri[t * 3 + 2] * 3 + c]) / 3f;
        final int cap = Math.max(1, 2 * n);
        box = new float[cap * 6];
        left = new int[cap];
        count = new int[cap];
        nodes = 0;
        if (n > 0) build(cen, 0, n);
    }

    private int build(float[] cen, int from, int to) {
        final int id = nodes++;
        float x0 = Float.MAX_VALUE, y0 = x0, z0 = x0, x1 = -x0, y1 = -x0, z1 = -x0;
        float cx0 = x0, cy0 = x0, cz0 = x0, cx1 = -x0, cy1 = -x0, cz1 = -x0;
        for (int i = from; i < to; i++) {
            final int t = order[i];
            for (int k = 0; k < 3; k++) {
                final int v = tri[t * 3 + k] * 3;
                x0 = Math.min(x0, p[v]);
                y0 = Math.min(y0, p[v + 1]);
                z0 = Math.min(z0, p[v + 2]);
                x1 = Math.max(x1, p[v]);
                y1 = Math.max(y1, p[v + 1]);
                z1 = Math.max(z1, p[v + 2]);
            }
            cx0 = Math.min(cx0, cen[t * 3]);
            cy0 = Math.min(cy0, cen[t * 3 + 1]);
            cz0 = Math.min(cz0, cen[t * 3 + 2]);
            cx1 = Math.max(cx1, cen[t * 3]);
            cy1 = Math.max(cy1, cen[t * 3 + 1]);
            cz1 = Math.max(cz1, cen[t * 3 + 2]);
        }
        box[id * 6] = x0;
        box[id * 6 + 1] = y0;
        box[id * 6 + 2] = z0;
        box[id * 6 + 3] = x1;
        box[id * 6 + 4] = y1;
        box[id * 6 + 5] = z1;
        if (to - from <= 4) {
            left[id] = from;
            count[id] = to - from;
            return id;
        }
        final float ex = cx1 - cx0, ey = cy1 - cy0, ez = cz1 - cz0;
        final int axis = ex >= ey && ex >= ez ? 0 : (ey >= ez ? 1 : 2);
        final float mid = axis == 0 ? (cx0 + cx1) / 2 : axis == 1 ? (cy0 + cy1) / 2 : (cz0 + cz1) / 2;
        int i = from, j = to - 1;
        while (i <= j) {
            if (cen[order[i] * 3 + axis] < mid) {
                i++;
            } else {
                final int s = order[i];
                order[i] = order[j];
                order[j] = s;
                j--;
            }
        }
        if (i == from || i == to) i = (from + to) >>> 1;  // all on one side: halve
        count[id] = 0;
        build(cen, from, i);
        left[id] = build(cen, i, to);  // the left child is id + 1
        return id;
    }

    /** A see-through triangle's alpha where a ray crosses it (b1, b2: the weights of its 2nd and 3rd corners). */
    interface Alpha {
        float at(int tri, float b1, float b2);
    }

    /**
     * How much light gets along the segment o + t d, t in (tmin, tmax): 0 at a solid triangle, else the product of
     * (1 - alpha) over the see-through ones it crosses (Cycles' transparent shadows). see[t]: triangle t is see-through
     * (null: none are). stack: scratch (128 ints).
     */
    float transmit(float ox, float oy, float oz, float dx, float dy, float dz, float tmin, float tmax, int[] stack, boolean[] see,
            Alpha alpha) {
        if (see == null) return occluded(ox, oy, oz, dx, dy, dz, tmin, tmax, stack) ? 0f : 1f;
        if (nodes == 0) return 1f;
        final float ix = 1f / dx, iy = 1f / dy, iz = 1f / dz;
        float keep = 1f;
        int sp = 0;
        stack[sp++] = 0;
        while (sp > 0) {
            final int id = stack[--sp];
            final int b = id * 6;
            float t0 = (box[b] - ox) * ix, t1 = (box[b + 3] - ox) * ix;
            float lo = Math.min(t0, t1), hi = Math.max(t0, t1);
            t0 = (box[b + 1] - oy) * iy;
            t1 = (box[b + 4] - oy) * iy;
            lo = Math.max(lo, Math.min(t0, t1));
            hi = Math.min(hi, Math.max(t0, t1));
            t0 = (box[b + 2] - oz) * iz;
            t1 = (box[b + 5] - oz) * iz;
            lo = Math.max(lo, Math.min(t0, t1));
            hi = Math.min(hi, Math.max(t0, t1));
            if (!(hi >= Math.max(lo, tmin)) || lo > tmax) continue;
            if (count[id] > 0) {
                for (int i = left[id], e = left[id] + count[id]; i < e; i++) {
                    final int t = order[i];
                    final int a = tri[t * 3] * 3, c = tri[t * 3 + 1] * 3, d = tri[t * 3 + 2] * 3;
                    final float e1x = p[c] - p[a], e1y = p[c + 1] - p[a + 1], e1z = p[c + 2] - p[a + 2];
                    final float e2x = p[d] - p[a], e2y = p[d + 1] - p[a + 1], e2z = p[d + 2] - p[a + 2];
                    final float px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
                    final float det = e1x * px + e1y * py + e1z * pz;
                    if (det > -1e-12f && det < 1e-12f) continue;
                    final float inv = 1f / det;
                    final float sx = ox - p[a], sy = oy - p[a + 1], sz = oz - p[a + 2];
                    final float u = (sx * px + sy * py + sz * pz) * inv;
                    if (u < 0 || u > 1) continue;
                    final float qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
                    final float v = (dx * qx + dy * qy + dz * qz) * inv;
                    if (v < 0 || u + v > 1) continue;
                    final float t2 = (e2x * qx + e2y * qy + e2z * qz) * inv;
                    if (!(t2 > tmin && t2 < tmax)) continue;
                    if (!see[t]) return 0f;
                    keep *= 1f - alpha.at(t, u, v);
                    if (keep < 0.004f) return 0f;
                }
            } else if (sp + 2 <= stack.length) {
                stack[sp++] = id + 1;
                stack[sp++] = left[id];
            }
        }
        return keep;
    }

    /** True when the segment o + t d, t in (tmin, tmax), hits a triangle. stack: scratch (128 ints). */
    boolean occluded(float ox, float oy, float oz, float dx, float dy, float dz, float tmin, float tmax, int[] stack) {
        if (nodes == 0) return false;
        final float ix = 1f / dx, iy = 1f / dy, iz = 1f / dz;
        int sp = 0;
        stack[sp++] = 0;
        while (sp > 0) {
            final int id = stack[--sp];
            final int b = id * 6;
            float t0 = (box[b] - ox) * ix, t1 = (box[b + 3] - ox) * ix;
            float lo = Math.min(t0, t1), hi = Math.max(t0, t1);
            t0 = (box[b + 1] - oy) * iy;
            t1 = (box[b + 4] - oy) * iy;
            lo = Math.max(lo, Math.min(t0, t1));
            hi = Math.min(hi, Math.max(t0, t1));
            t0 = (box[b + 2] - oz) * iz;
            t1 = (box[b + 5] - oz) * iz;
            lo = Math.max(lo, Math.min(t0, t1));
            hi = Math.min(hi, Math.max(t0, t1));
            if (!(hi >= Math.max(lo, tmin)) || lo > tmax) continue;
            if (count[id] > 0) {
                for (int i = left[id], e = left[id] + count[id]; i < e; i++) {
                    final int t = order[i];
                    final int a = tri[t * 3] * 3, c = tri[t * 3 + 1] * 3, d = tri[t * 3 + 2] * 3;
                    final float e1x = p[c] - p[a], e1y = p[c + 1] - p[a + 1], e1z = p[c + 2] - p[a + 2];
                    final float e2x = p[d] - p[a], e2y = p[d + 1] - p[a + 1], e2z = p[d + 2] - p[a + 2];
                    final float px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
                    final float det = e1x * px + e1y * py + e1z * pz;
                    if (det > -1e-12f && det < 1e-12f) continue;
                    final float inv = 1f / det;
                    final float sx = ox - p[a], sy = oy - p[a + 1], sz = oz - p[a + 2];
                    final float u = (sx * px + sy * py + sz * pz) * inv;
                    if (u < 0 || u > 1) continue;
                    final float qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
                    final float v = (dx * qx + dy * qy + dz * qz) * inv;
                    if (v < 0 || u + v > 1) continue;
                    final float t2 = (e2x * qx + e2y * qy + e2z * qz) * inv;
                    if (t2 > tmin && t2 < tmax) return true;
                }
            } else if (sp + 2 <= stack.length) {
                stack[sp++] = id + 1;
                stack[sp++] = left[id];
            }
        }
        return false;
    }
}
