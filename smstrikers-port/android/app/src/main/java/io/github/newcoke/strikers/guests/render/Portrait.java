package io.github.newcoke.strikers.guests.render;

import java.util.Arrays;
import java.util.Map;

/**
 * A guest's picture without Blender: portrait_render.py's framing (orthographic camera from the front, a little to the
 * right, fitted to the model's bounds; --full: the whole figure; --turn: the posed model turned to face the camera)
 * drawn by a CPU rasteriser, supersampled, with a look made to match its Cycles render: textured (their alpha
 * see-through, every layer in depth order), smooth-shaded, the bluish-grey sky with its occlusion, the key and rim
 * area lights with soft shadows (rays per pixel of the picture, smoothed edge-aware like Cycles' denoiser), a broad
 * highlight, transparent background, and Blender's 4x downscale of its big render (the straight-alpha box that gives
 * the game's pictures their soft dark rim).
 */
public final class Portrait {
    /** Our numbers (assets/guestdata/render/portrait.txt). */
    public static final class Look {
        public int ss = 4;
        public double[] skyRgb = {0.275, 0.3, 0.35};
        public double sky = 1.278, bounce = 0.4, key = 0.087, rim = 0.092, spec = 0.0, roughness = 0.7;
        public double keyFull = 380, rimFull = 220, keyBust = 900, rimBust = 500;
        public int skyRays = 16, lightRays = 9, vertexSkyRays = 12, vertexLightRays = 4, denoise = 2;
        public double exposure = 1.0;
        public boolean flames = false;

        public static Look from(Map<String, double[]> v) {
            final Look l = new Look();
            if (v.containsKey("supersample")) l.ss = (int) v.get("supersample")[0];
            if (v.containsKey("sky_rgb")) l.skyRgb = v.get("sky_rgb");
            if (v.containsKey("sky")) l.sky = v.get("sky")[0];
            if (v.containsKey("bounce")) l.bounce = v.get("bounce")[0];
            if (v.containsKey("key")) l.key = v.get("key")[0];
            if (v.containsKey("rim")) l.rim = v.get("rim")[0];
            if (v.containsKey("spec")) l.spec = v.get("spec")[0];
            if (v.containsKey("roughness")) l.roughness = v.get("roughness")[0];
            if (v.containsKey("energy_full")) {
                l.keyFull = v.get("energy_full")[0];
                l.rimFull = v.get("energy_full")[1];
            }
            if (v.containsKey("energy_bust")) {
                l.keyBust = v.get("energy_bust")[0];
                l.rimBust = v.get("energy_bust")[1];
            }
            if (v.containsKey("sky_rays")) l.skyRays = (int) v.get("sky_rays")[0];
            if (v.containsKey("light_rays")) l.lightRays = (int) v.get("light_rays")[0];
            if (v.containsKey("vertex_rays")) {
                l.vertexSkyRays = (int) v.get("vertex_rays")[0];
                l.vertexLightRays = (int) v.get("vertex_rays")[1];
            }
            if (v.containsKey("denoise")) l.denoise = (int) v.get("denoise")[0];
            if (v.containsKey("exposure")) l.exposure = v.get("exposure")[0];
            if (v.containsKey("flames")) l.flames = v.get("flames")[0] != 0;
            return l;
        }
    }

    /** The posed model in front of the camera with its lights: what the normal and shiny pictures share. */
    public static final class Scene {
        public final int w, h, sw, sh, ss;
        final int nv, nt;
        final float[] p, n, uv;
        final int[] tri;
        // camera: target, view direction, right, up, the view's width / height in metres
        final double[] t = new double[3], d = new double[3], x = new double[3], y = new double[3];
        final double viewW, viewH;
        final double[] keyPos = new double[3], keyDir = new double[3], rimPos = new double[3], rimDir = new double[3];
        final double keyP, rimP, height, size;
        // per vertex: sample-grid position and depth, sky / key / rim visibility (see-through layers, thin bits)
        final float[] sx, sy, sz, visSky, visKey, visRim;
        // per triangle: +1 when the camera sees its front (its winding), -1 its back
        final byte[] facing;
        final Bvh bvh;
        final boolean[] see;  // per triangle: a see-through picture (null: none)
        final Bvh.Alpha alphaFn;
        final java.util.HashMap<Long, float[]> layerLight = new java.util.HashMap<>();  // see-through layers' light
        final Look look;
        // per picture pixel (once, the first picture drawn): the opaque surface seen at its centre and its light
        int[] gTri;
        float[] gDepth, gVis, gNrm;

        Scene(Mesh m, int w, int h, boolean full, double turnDeg, Look look) {
            this.look = look;
            this.w = w;
            this.h = h;
            ss = look.ss;
            sw = w * ss;
            sh = h * ss;
            nv = m.nv;
            nt = m.nt;
            tri = m.tri;
            uv = m.uv;
            p = Arrays.copyOf(m.p, nv * 3);
            n = Arrays.copyOf(m.n, nv * 3);
            if (!Double.isNaN(turnDeg)) {  // the object turned about Z, applied (Blender's rotation_euler.z += ang)
                final double a = Math.toRadians(turnDeg), cs = Math.cos(a), sn = Math.sin(a);
                for (int i = 0; i < nv; i++) {
                    final double px = p[i * 3], py = p[i * 3 + 1], nx = n[i * 3], ny = n[i * 3 + 1];
                    p[i * 3] = (float) (px * cs - py * sn);
                    p[i * 3 + 1] = (float) (px * sn + py * cs);
                    n[i * 3] = (float) (nx * cs - ny * sn);
                    n[i * 3 + 1] = (float) (nx * sn + ny * cs);
                }
            }
            final boolean[] used = new boolean[nv];
            for (int i = 0; i < nt * 3; i++) used[tri[i]] = true;
            final double[] lo = {1e9, 1e9, 1e9}, hi = {-1e9, -1e9, -1e9};
            for (int i = 0; i < nv; i++) {
                if (!used[i]) continue;
                for (int c = 0; c < 3; c++) {
                    lo[c] = Math.min(lo[c], p[i * 3 + c]);
                    hi[c] = Math.max(hi[c], p[i * 3 + c]);
                }
            }
            if (lo[0] > hi[0]) {
                Arrays.fill(lo, 0);
                Arrays.fill(hi, 1);
            }
            height = Math.max(1e-4, hi[2] - lo[2]);
            size = Math.sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + height * height);
            final double top = hi[2] + 0.04 * height;
            final double bottom = full ? lo[2] - 0.03 * height : hi[2] - 0.45 * height;
            final double cz = 0.5 * (top + bottom), cx = 0.5 * (lo[0] + hi[0]);
            double scale = (top - bottom) * Math.max(1.0, (double) w / h);
            if (full) {
                final double span = Math.max(hi[0] - lo[0], 0.8 * (hi[1] - lo[1])) * 1.12;
                scale = Math.max(scale, span * Math.max(1.0, (double) h / w));
            }
            // Blender's sensor fit (auto): the orthographic scale is the longer side
            viewW = w >= h ? scale : scale * w / h;
            viewH = w >= h ? scale * h / w : scale;
            final double[] cam = {cx + 0.15 * height, lo[1] - 3.0 * height, cz};
            t[0] = cx;
            t[1] = 0.5 * (lo[1] + hi[1]);
            t[2] = cz;
            for (int c = 0; c < 3; c++) d[c] = t[c] - cam[c];
            norm(d);
            // the camera's up is the world's Z as near as it goes (to_track_quat("-Z", "Y")); right = d x up
            cross(d, new double[] {0, 0, 1}, x);
            norm(x);
            cross(x, d, y);
            norm(y);
            keyP = full ? look.keyFull * height * height : look.keyBust * height * height;
            rimP = full ? look.rimFull * height * height : look.rimBust * height * height;
            keyPos[0] = cx - 2 * height;
            keyPos[1] = lo[1] - 2 * height;
            keyPos[2] = hi[2] + height;
            rimPos[0] = cx + 2 * height;
            rimPos[1] = hi[1] + 2 * height;
            rimPos[2] = hi[2] + 0.5 * height;
            final double[] aim = {cx, 0, cz};  // both lights aim at (cx, 0, cz)
            for (int c = 0; c < 3; c++) {
                keyDir[c] = aim[c] - keyPos[c];
                rimDir[c] = aim[c] - rimPos[c];
            }
            norm(keyDir);
            norm(rimDir);
            sx = new float[nv];
            sy = new float[nv];
            sz = new float[nv];
            for (int i = 0; i < nv; i++) {
                final double qx = p[i * 3] - t[0], qy = p[i * 3 + 1] - t[1], qz = p[i * 3 + 2] - t[2];
                sx[i] = (float) (((qx * x[0] + qy * x[1] + qz * x[2]) / viewW + 0.5) * sw);
                sy[i] = (float) ((0.5 - (qx * y[0] + qy * y[1] + qz * y[2]) / viewH) * sh);
                sz[i] = (float) (qx * d[0] + qy * d[1] + qz * d[2]);
            }
            facing = new byte[nt];
            for (int k = 0; k < nt; k++) {
                final int a = tri[k * 3], b = tri[k * 3 + 1], c = tri[k * 3 + 2];
                // screen y grows downward: a counter-clockwise (front-facing) triangle has a negative area here
                final float area = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a]);
                facing[k] = (byte) (area < 0 ? 1 : -1);
            }
            // see-through pictures (gas, cut-outs) let the light by as much as they show through (their alpha)
            final Tex[] tex0 = new Tex[m.mats.size()];
            for (int i = 0; i < tex0.length; i++) tex0[i] = new Tex(m.mats.get(i));
            boolean anySee = false;
            final boolean[] see = new boolean[nt];
            for (int k = 0; k < nt; k++) {
                see[k] = tex0[m.triMat[k]].alpha;
                anySee |= see[k];
            }
            this.see = anySee ? see : null;
            final int[] triMat = m.triMat;
            alphaFn = (k, b1, b2) -> {
                final int a = tri[k * 3], b = tri[k * 3 + 1], c = tri[k * 3 + 2];
                final float b0 = 1f - b1 - b2;
                return tex0[triMat[k]].alphaAt(b0 * uv[a * 2] + b1 * uv[b * 2] + b2 * uv[c * 2],
                        b0 * uv[a * 2 + 1] + b1 * uv[b * 2 + 1] + b2 * uv[c * 2 + 1]);
            };
            bvh = new Bvh(p, tri, nt, null);
            visSky = new float[nv];
            visKey = new float[nv];
            visRim = new float[nv];
            vertexVisibility(used);
        }

        /** Per vertex (camera side of its normal): sky, key and rim seen. */
        private void vertexVisibility(boolean[] used) {
            final int[] stack = new int[128];
            final double[] nn = new double[3], vis = new double[3];
            for (int i = 0; i < nv; i++) {
                if (!used[i]) continue;
                nn[0] = n[i * 3];
                nn[1] = n[i * 3 + 1];
                nn[2] = n[i * 3 + 2];
                if (-(nn[0] * d[0] + nn[1] * d[1] + nn[2] * d[2]) < 0) {
                    nn[0] = -nn[0];
                    nn[1] = -nn[1];
                    nn[2] = -nn[2];
                }
                if (norm(nn) == 0) {
                    nn[0] = -d[0];
                    nn[1] = -d[1];
                    nn[2] = -d[2];
                }
                visibility(p[i * 3], p[i * 3 + 1], p[i * 3 + 2], nn, nn, look.vertexSkyRays, look.vertexLightRays, i, stack, vis);
                visSky[i] = (float) vis[0];
                visKey[i] = (float) vis[1];
                visRim[i] = (float) vis[2];
            }
        }

        /**
         * From a surface point (ng: its face's normal on the camera's side, nn: its shading normal): the share of the sky
         * (cosine-weighted) it sees, and of the key and rim lights (points on each square).
         */
        void visibility(double px, double py, double pz, double[] ng, double[] nn, int skyRays, int lightRays, int seed,
                int[] stack, double[] out) {
            final float eps = (float) (1.5e-3 * size), far = (float) (4 * size);
            final float ox = (float) (px + ng[0] * eps), oy = (float) (py + ng[1] * eps), oz = (float) (pz + ng[2] * eps);
            final double[] tu = new double[3], tv = new double[3];
            basis(nn, tu, tv);
            final double rot = 2 * Math.PI * hash01(seed), cr = Math.cos(rot), sr = Math.sin(rot);
            final double shift = hash01(seed * 31 + 7);
            double open = 0;
            for (int k = 0; k < skyRays; k++) {
                final double u = (k + 0.5) / skyRays, v = (radical(k) + shift) % 1.0;
                final double r = Math.sqrt(u), ph = 2 * Math.PI * v;
                final double a0 = r * Math.cos(ph), b0 = r * Math.sin(ph), c = Math.sqrt(Math.max(0, 1 - u));
                final double a = a0 * cr - b0 * sr, b = a0 * sr + b0 * cr;
                final float dx = (float) (tu[0] * a + tv[0] * b + nn[0] * c);
                final float dy = (float) (tu[1] * a + tv[1] * b + nn[1] * c);
                final float dz = (float) (tu[2] * a + tv[2] * b + nn[2] * c);
                open += bvh.transmit(ox, oy, oz, dx, dy, dz, 0, far, stack, see, alphaFn);
            }
            out[0] = skyRays > 0 ? open / skyRays : 1;
            final int nl = Math.max(1, (int) Math.round(Math.sqrt(lightRays)));
            out[1] = light(ox, oy, oz, nn, keyPos, keyDir, nl, seed, stack);
            out[2] = light(ox, oy, oz, nn, rimPos, rimDir, nl, seed + 1, stack);
        }

        /** The share of an area light (side = height, nl x nl jittered points) a point sees. */
        private double light(float ox, float oy, float oz, double[] nn, double[] pos, double[] dir, int nl, int seed, int[] stack) {
            final double[] u = new double[3], v = new double[3];
            basis(dir, u, v);
            final double ja = hash01(seed * 7 + 3), jb = hash01(seed * 13 + 5);
            double open = 0;
            int tried = 0;
            for (int a = 0; a < nl; a++) {
                for (int b = 0; b < nl; b++) {
                    final double fa = ((a + ja) / nl - 0.5) * height, fb = ((b + jb) / nl - 0.5) * height;
                    final double lx = pos[0] + u[0] * fa + v[0] * fb - ox, ly = pos[1] + u[1] * fa + v[1] * fb - oy,
                            lz = pos[2] + u[2] * fa + v[2] * fb - oz;
                    final double dist = Math.sqrt(lx * lx + ly * ly + lz * lz);
                    tried++;
                    if (lx * nn[0] + ly * nn[1] + lz * nn[2] <= 0) continue;
                    open += bvh.transmit(ox, oy, oz, (float) (lx / dist), (float) (ly / dist), (float) (lz / dist), 0,
                            (float) dist, stack, see, alphaFn);
                }
            }
            return open / tried;
        }

        /** The unit normal of triangle k's face, on the camera's side. */
        void faceNormal(int k, double[] out) {
            final int a = tri[k * 3], b = tri[k * 3 + 1], c = tri[k * 3 + 2];
            final double e1x = p[b * 3] - p[a * 3], e1y = p[b * 3 + 1] - p[a * 3 + 1], e1z = p[b * 3 + 2] - p[a * 3 + 2];
            final double e2x = p[c * 3] - p[a * 3], e2y = p[c * 3 + 1] - p[a * 3 + 1], e2z = p[c * 3 + 2] - p[a * 3 + 2];
            out[0] = e1y * e2z - e1z * e2y;
            out[1] = e1z * e2x - e1x * e2z;
            out[2] = e1x * e2y - e1y * e2x;
            if (norm(out) == 0 || -(out[0] * d[0] + out[1] * d[1] + out[2] * d[2]) < 0) {
                if (out[0] == 0 && out[1] == 0 && out[2] == 0) {
                    out[0] = -d[0];
                    out[1] = -d[1];
                    out[2] = -d[2];
                } else {
                    out[0] = -out[0];
                    out[1] = -out[1];
                    out[2] = -out[2];
                }
            }
        }

        /** The shading normal at (k, w1, w2): as stored, turned round on a triangle seen from its back. */
        void shadingNormal(int k, float w1, float w2, double[] out) {
            final int a = tri[k * 3], b = tri[k * 3 + 1], c = tri[k * 3 + 2];
            final float w0 = 1f - w1 - w2, f = facing[k];
            out[0] = (w0 * n[a * 3] + w1 * n[b * 3] + w2 * n[c * 3]) * f;
            out[1] = (w0 * n[a * 3 + 1] + w1 * n[b * 3 + 1] + w2 * n[c * 3 + 1]) * f;
            out[2] = (w0 * n[a * 3 + 2] + w1 * n[b * 3 + 2] + w2 * n[c * 3 + 2]) * f;
            if (norm(out) < 1e-12) faceNormal(k, out);
        }

        /** The light per picture pixel: the opaque surface at each pixel's centre, its rays, smoothed edge-aware. */
        void pixelLight(Tex[] tex, int[] triMat) {
            final int np = w * h;
            gTri = new int[np];
            gDepth = new float[np];
            final float[] gb1 = new float[np], gb2 = new float[np];
            Arrays.fill(gTri, -1);
            Arrays.fill(gDepth, Float.POSITIVE_INFINITY);
            final float inv = 1f / ss;
            for (int k = 0; k < nt; k++) {
                final Tex tx = tex[triMat[k]];
                final int a = tri[k * 3], b = tri[k * 3 + 1], c = tri[k * 3 + 2];
                final float ax = sx[a] * inv, ay = sy[a] * inv, bx = sx[b] * inv, by = sy[b] * inv, cx = sx[c] * inv, cy = sy[c] * inv;
                final float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
                if (!(Math.abs(area) > 1e-12f)) continue;
                final float ia = 1f / area;
                final int xa = Math.max(0, (int) Math.floor(Math.min(ax, Math.min(bx, cx)) - 0.5f));
                final int xb = Math.min(w - 1, (int) Math.ceil(Math.max(ax, Math.max(bx, cx)) - 0.5f));
                final int ya = Math.max(0, (int) Math.floor(Math.min(ay, Math.min(by, cy)) - 0.5f));
                final int yb = Math.min(h - 1, (int) Math.ceil(Math.max(ay, Math.max(by, cy)) - 0.5f));
                for (int yy = ya; yy <= yb; yy++) {
                    final float py = yy + 0.5f;
                    for (int xx = xa; xx <= xb; xx++) {
                        final float px = xx + 0.5f;
                        final float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) * ia;
                        final float w2 = ((ax - px) * (by - py) - (bx - px) * (ay - py)) * ia;
                        final float w0 = 1f - w1 - w2;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        final float z = w0 * sz[a] + w1 * sz[b] + w2 * sz[c];
                        final int i = yy * w + xx;
                        if (!(z < gDepth[i])) continue;
                        if (tx.alpha) {
                            final float u = w0 * uv[a * 2] + w1 * uv[b * 2] + w2 * uv[c * 2];
                            final float v = w0 * uv[a * 2 + 1] + w1 * uv[b * 2 + 1] + w2 * uv[c * 2 + 1];
                            if (tx.alphaAt(u, v) < 0.99f) continue;
                        }
                        gDepth[i] = z;
                        gTri[i] = k;
                        gb1[i] = w1;
                        gb2[i] = w2;
                    }
                }
            }
            final float[] raw = new float[np * 3];
            gNrm = new float[np * 3];
            final int[] stack = new int[128];
            final double[] ng = new double[3], nn = new double[3], vis = new double[3];
            for (int i = 0; i < np; i++) {
                final int k = gTri[i];
                if (k < 0) continue;
                final int a = tri[k * 3], b = tri[k * 3 + 1], c = tri[k * 3 + 2];
                final float w1 = gb1[i], w2 = gb2[i], w0 = 1f - w1 - w2;
                final double px = w0 * p[a * 3] + w1 * p[b * 3] + w2 * p[c * 3];
                final double py = w0 * p[a * 3 + 1] + w1 * p[b * 3 + 1] + w2 * p[c * 3 + 1];
                final double pz = w0 * p[a * 3 + 2] + w1 * p[b * 3 + 2] + w2 * p[c * 3 + 2];
                faceNormal(k, ng);
                shadingNormal(k, w1, w2, nn);
                visibility(px, py, pz, ng, nn, look.skyRays, look.lightRays, i, stack, vis);
                for (int ch = 0; ch < 3; ch++) {
                    raw[i * 3 + ch] = (float) vis[ch];
                    gNrm[i * 3 + ch] = (float) nn[ch];
                }
            }
            // smoothed: nearby pixels of the same surface (depth within a few pixels, normals alike)
            gVis = new float[np * 3];
            final float tz = (float) (3 * viewW / w);
            final int r = Math.max(0, look.denoise);
            for (int yy = 0; yy < h; yy++) {
                for (int xx = 0; xx < w; xx++) {
                    final int i = yy * w + xx;
                    if (gTri[i] < 0) continue;
                    float s0 = 0, s1 = 0, s2 = 0, ws = 0;
                    for (int dy = -r; dy <= r; dy++) {
                        final int y2 = yy + dy;
                        if (y2 < 0 || y2 >= h) continue;
                        for (int dx = -r; dx <= r; dx++) {
                            final int x2 = xx + dx;
                            if (x2 < 0 || x2 >= w) continue;
                            final int j = y2 * w + x2;
                            if (gTri[j] < 0) continue;
                            final float dz = (gDepth[j] - gDepth[i]) / tz;
                            final float nd = gNrm[i * 3] * gNrm[j * 3] + gNrm[i * 3 + 1] * gNrm[j * 3 + 1] + gNrm[i * 3 + 2] * gNrm[j * 3 + 2];
                            if (nd <= 0) continue;
                            final float n2 = nd * nd, n4 = n2 * n2;
                            final float wgt = (float) Math.exp(-(dx * dx + dy * dy) / (2.0 * 1.2 * 1.2) - dz * dz) * n4 * n4;
                            s0 += raw[j * 3] * wgt;
                            s1 += raw[j * 3 + 1] * wgt;
                            s2 += raw[j * 3 + 2] * wgt;
                            ws += wgt;
                        }
                    }
                    gVis[i * 3] = s0 / ws;
                    gVis[i * 3 + 1] = s1 / ws;
                    gVis[i * 3 + 2] = s2 / ws;
                }
            }
        }

        /** Tests: the per-pixel sky / key / rim seen (w x h x 3; NaN-free, 0 where nothing opaque). */
        public float[] pixelVisibility() {
            return gVis;
        }

        /** The light at a sample of the big render (x, y: sample grid; z: its depth; nrm: its normal): from the pixels near it. */
        boolean pixelVis(float fx, float fy, float z, double[] nrm, float[] out) {
            final float cx = fx / ss - 0.5f, cy = fy / ss - 0.5f;
            final int x0 = (int) Math.floor(cx), y0 = (int) Math.floor(cy);
            final float ax = cx - x0, ay = cy - y0;
            final float tz = (float) (3 * viewW / w);
            float s0 = 0, s1 = 0, s2 = 0, ws = 0;
            for (int dy = 0; dy <= 1; dy++) {
                final int yy = y0 + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = 0; dx <= 1; dx++) {
                    final int xx = x0 + dx;
                    if (xx < 0 || xx >= w) continue;
                    final int j = yy * w + xx;
                    if (gTri[j] < 0) continue;
                    final float dz = (gDepth[j] - z) / tz;
                    final double nd = nrm[0] * gNrm[j * 3] + nrm[1] * gNrm[j * 3 + 1] + nrm[2] * gNrm[j * 3 + 2];
                    if (nd <= 0.3) continue;
                    final float wgt = (dx == 1 ? ax : 1 - ax) * (dy == 1 ? ay : 1 - ay) * (float) Math.exp(-dz * dz) + 1e-6f;
                    s0 += gVis[j * 3] * wgt;
                    s1 += gVis[j * 3 + 1] * wgt;
                    s2 += gVis[j * 3 + 2] * wgt;
                    ws += wgt;
                }
            }
            if (ws < 0.02f) return false;
            out[0] = s0 / ws;
            out[1] = s1 / ws;
            out[2] = s2 / ws;
            return true;
        }
    }

    public static Scene scene(Mesh m, int w, int h, boolean full, double turnDeg, Look look) {
        return new Scene(m, w, h, full, turnDeg, look);
    }

    // ---------------------------------------------------------------- textures

    private static final float[] LIN = new float[256];

    static {
        for (int i = 0; i < 256; i++) {
            final double c = i / 255.0;
            LIN[i] = (float) (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
        }
    }

    /** A picture as linear, premultiplied RGBA floats (Cycles' image: sRGB to linear, then associated alpha). */
    static final class Tex {
        final int w, h;
        final float[] px;
        final boolean alpha, soft;

        Tex(Mesh.Material m) {
            alpha = m.alpha;
            soft = m.soft;
            if (m.rgba == null) {
                w = h = 1;
                px = new float[] {1, 1, 1, 1};
                return;
            }
            w = m.w;
            h = m.h;
            px = new float[w * h * 4];
            for (int i = 0; i < w * h; i++) {
                final float a = (m.rgba[i * 4 + 3] & 0xFF) / 255f;
                px[i * 4] = LIN[m.rgba[i * 4] & 0xFF] * a;
                px[i * 4 + 1] = LIN[m.rgba[i * 4 + 1] & 0xFF] * a;
                px[i * 4 + 2] = LIN[m.rgba[i * 4 + 2] & 0xFF] * a;
                px[i * 4 + 3] = a;
            }
        }

        /** Bilinear, repeating; out = premultiplied RGBA. */
        void sample(float u, float v, float[] out) {
            final float fx = u * w - 0.5f, fy = v * h - 0.5f;
            final float flx = (float) Math.floor(fx), fly = (float) Math.floor(fy);
            final float ax = fx - flx, ay = fy - fly;
            final int x0 = Math.floorMod((int) flx, w), y0 = Math.floorMod((int) fly, h);
            final int x1 = x0 + 1 == w ? 0 : x0 + 1, y1 = y0 + 1 == h ? 0 : y0 + 1;
            final int a = (y0 * w + x0) * 4, b = (y0 * w + x1) * 4, c = (y1 * w + x0) * 4, d = (y1 * w + x1) * 4;
            final float wa = (1 - ax) * (1 - ay), wb = ax * (1 - ay), wc = (1 - ax) * ay, wd = ax * ay;
            for (int k = 0; k < 4; k++) out[k] = px[a + k] * wa + px[b + k] * wb + px[c + k] * wc + px[d + k] * wd;
        }

        float alphaAt(float u, float v) {
            if (!alpha) return 1f;
            final float fx = u * w - 0.5f, fy = v * h - 0.5f;
            final float flx = (float) Math.floor(fx), fly = (float) Math.floor(fy);
            final float ax = fx - flx, ay = fy - fly;
            final int x0 = Math.floorMod((int) flx, w), y0 = Math.floorMod((int) fly, h);
            final int x1 = x0 + 1 == w ? 0 : x0 + 1, y1 = y0 + 1 == h ? 0 : y0 + 1;
            return px[(y0 * w + x0) * 4 + 3] * (1 - ax) * (1 - ay) + px[(y0 * w + x1) * 4 + 3] * ax * (1 - ay)
                    + px[(y1 * w + x0) * 4 + 3] * (1 - ax) * ay + px[(y1 * w + x1) * 4 + 3] * ax * ay;
        }
    }

    // ---------------------------------------------------------------- drawing

    /** The picture (RGBA, w x h, straight alpha) of the scene with this mesh's pictures (its own or its shiny's). */
    public static byte[] render(Scene s, Mesh m, Look look) {
        return render(s, m, look, null);
    }

    /**
     * comps (tests: fitting the look; w x h x 16): per output pixel the linear averages of the light's parts, 5 x RGB
     * (albedo x sky seen, albedo x sky hidden, albedo x key, albedo x rim, highlight; each without its Look factor),
     * then the coverage; null to skip.
     */
    public static byte[] render(Scene s, Mesh m, Look look, float[] comps) {
        final Tex[] tex = new Tex[m.mats.size()];
        for (int i = 0; i < tex.length; i++) tex[i] = new Tex(m.mats.get(i));
        if (s.gVis == null) s.pixelLight(tex, m.triMat);
        final int nt = s.nt;
        final boolean[] trans = new boolean[nt];
        int ntr = 0;
        for (int k = 0; k < nt; k++) {
            trans[k] = tex[m.triMat[k]].alpha;
            if (trans[k]) ntr++;
        }
        final int[] order = new int[ntr];
        ntr = 0;
        for (int k = 0; k < nt; k++)
            if (trans[k]) order[ntr++] = k;
        // each triangle's rows on the sample grid
        final int[] ty0 = new int[nt], ty1 = new int[nt];
        for (int k = 0; k < nt; k++) {
            final int a = s.tri[k * 3], b = s.tri[k * 3 + 1], c = s.tri[k * 3 + 2];
            final float mn = Math.min(s.sy[a], Math.min(s.sy[b], s.sy[c])), mx = Math.max(s.sy[a], Math.max(s.sy[b], s.sy[c]));
            ty0[k] = (int) Math.floor(mn - 0.5f);
            ty1[k] = (int) Math.ceil(mx - 0.5f);
        }
        final Shader sh = new Shader(s, m, tex, look);
        final int ss = s.ss, sw = s.sw;
        final int bandOut = Math.max(1, 64 / ss);  // output rows per band
        final int bandRows = bandOut * ss;
        final int rows = bandRows + 2;  // a sample row of margin each side, for the pixel filter
        final float[] depth = new float[rows * sw], b1 = new float[rows * sw], b2 = new float[rows * sw];
        final int[] id = new int[rows * sw];
        final float[] col = new float[rows * sw * 4];
        final float[] fil = new float[4];
        final byte[] hiRes = new byte[bandRows * sw * 4];
        final byte[] out = new byte[s.w * s.h * 4];
        final float[] c4 = new float[4], vis = new float[3];
        final double[] nrm = new double[3];
        final float[] cp = comps != null ? new float[rows * sw * 16] : null;
        final Frags frags = new Frags();
        for (int oy0 = 0; oy0 < s.h; oy0 += bandOut) {
            final int y0 = oy0 * ss - 1, nr = Math.min(rows, (s.h - oy0) * ss + 2);
            Arrays.fill(depth, Float.POSITIVE_INFINITY);
            Arrays.fill(id, -1);
            Arrays.fill(col, 0f);
            if (cp != null) Arrays.fill(cp, 0f);
            // opaque: nearest wins (the see-through pictures' solid texels too)
            for (int k = 0; k < nt; k++) {
                if (ty1[k] < y0 || ty0[k] >= y0 + nr) continue;
                raster(s, k, y0, nr, trans[k] ? tex[m.triMat[k]] : null, depth, id, b1, b2, sh, null, false, false);
            }
            for (int i = 0; i < nr * sw; i++) {
                if (id[i] < 0) continue;
                final int k = id[i];
                s.shadingNormal(k, b1[i], b2[i], nrm);
                final boolean got = s.pixelVis(i % sw + 0.5f, y0 + i / sw + 0.5f, depth[i], nrm, vis);
                sh.shade(k, b1[i], b2[i], c4, cp, i * 16, got ? vis : null);
                col[i * 4] = c4[0];
                col[i * 4 + 1] = c4[1];
                col[i * 4 + 2] = c4[2];
                col[i * 4 + 3] = 1f;
            }
            // see-through: every layer in front of the opaque kept per sample, then laid over back to front
            frags.clear(nr * sw);
            for (int q = 0; q < ntr; q++) {
                final int k = order[q];
                if (ty1[k] < y0 || ty0[k] >= y0 + nr) continue;
                raster(s, k, y0, nr, tex[m.triMat[k]], depth, id, b1, b2, sh, frags, cp != null, true);
            }
            frags.resolve(col, cp);
            // Cycles' pixel filter over the big render (premultiplied), then its 8-bit straight pixels
            final int fr = Math.min(bandRows, nr - 2);
            for (int r = 0; r < fr; r++) {
                for (int xx = 0; xx < sw; xx++) {
                    final int o = (r * sw + xx) * 4;
                    Arrays.fill(fil, 0f);
                    float wsum = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        final int rr = r + 1 + dy;
                        if (rr < 0 || rr >= nr) continue;
                        for (int dx = -1; dx <= 1; dx++) {
                            final int xc = xx + dx;
                            if (xc < 0 || xc >= sw) continue;
                            final float wgt = (dy == 0 ? 2 : 1) * (dx == 0 ? 2 : 1);
                            final int q = (rr * sw + xc) * 4;
                            fil[0] += col[q] * wgt;
                            fil[1] += col[q + 1] * wgt;
                            fil[2] += col[q + 2] * wgt;
                            fil[3] += col[q + 3] * wgt;
                            wsum += wgt;
                        }
                    }
                    final float a = fil[3] / wsum;
                    if (a <= 0f) {
                        hiRes[o] = hiRes[o + 1] = hiRes[o + 2] = hiRes[o + 3] = 0;
                        continue;
                    }
                    final float inv = 1f / fil[3];
                    for (int c = 0; c < 3; c++) hiRes[o + c] = (byte) toSrgb8(fil[c] * inv * (float) look.exposure);
                    hiRes[o + 3] = (byte) Math.min(255, Math.max(0, Math.round(a * 255f)));
                }
            }
            // Blender's img.scale: a box over the 8-bit straight pixels
            final int orows = fr / ss;
            for (int oy = 0; oy < orows; oy++) {
                for (int ox = 0; ox < s.w; ox++) {
                    int r = 0, g = 0, b = 0, a = 0;
                    for (int dy = 0; dy < ss; dy++) {
                        for (int dx = 0; dx < ss; dx++) {
                            final int o = ((oy * ss + dy) * sw + ox * ss + dx) * 4;
                            r += hiRes[o] & 0xFF;
                            g += hiRes[o + 1] & 0xFF;
                            b += hiRes[o + 2] & 0xFF;
                            a += hiRes[o + 3] & 0xFF;
                        }
                    }
                    final int n2 = ss * ss, o = ((oy0 + oy) * s.w + ox) * 4;
                    out[o] = (byte) ((r + n2 / 2) / n2);
                    out[o + 1] = (byte) ((g + n2 / 2) / n2);
                    out[o + 2] = (byte) ((b + n2 / 2) / n2);
                    out[o + 3] = (byte) ((a + n2 / 2) / n2);
                    if (comps != null) {
                        final int co = ((oy0 + oy) * s.w + ox) * 16;
                        for (int dy = 0; dy < ss; dy++)
                            for (int dx = 0; dx < ss; dx++) {
                                final int i = (oy * ss + dy + 1) * sw + ox * ss + dx;
                                for (int c = 0; c < 15; c++) comps[co + c] += cp[i * 16 + c] / n2;
                                comps[co + 15] += col[i * 4 + 3] / n2;
                            }
                    }
                }
            }
        }
        return out;
    }

    /** The see-through layers per sample (Cycles' order is per ray: nearest first, whatever the triangles' order). */
    static final class Frags {
        private int[] head = new int[0], next = new int[1024];
        private float[] z = new float[1024], a = new float[1024], rgb = new float[1024 * 3], parts;
        private int n, samples;
        private int[] idx = new int[64];

        // the see-through layers' light: rays at the centres of 2 x 2 picture-pixel blocks on each triangle's plane,
        // blended between the four round a sample; kept with the scene for the shiny picture
        private final int[] stack = new int[128];
        private final double[] ng = new double[3], nn = new double[3], vis = new double[3];
        private final float[] out3 = new float[3];

        void clear(int samples) {
            if (head.length < samples) head = new int[samples];
            Arrays.fill(head, 0, samples, -1);
            this.samples = samples;
            n = 0;
        }

        float[] light(Scene s, int k, float w1, float w2, int sx, int sy) {
            final float blk = 2f * s.ss;
            final float bx = (sx + 0.5f) / blk - 0.5f, by = (sy + 0.5f) / blk - 0.5f;
            final int x0 = (int) Math.floor(bx), y0 = (int) Math.floor(by);
            final float fx = bx - x0, fy = by - y0;
            out3[0] = out3[1] = out3[2] = 0;
            for (int j = 0; j <= 1; j++)
                for (int i = 0; i <= 1; i++) {
                    final float[] l = blockLight(s, k, x0 + i, y0 + j, blk);
                    final float wgt = (i == 1 ? fx : 1 - fx) * (j == 1 ? fy : 1 - fy);
                    out3[0] += l[0] * wgt;
                    out3[1] += l[1] * wgt;
                    out3[2] += l[2] * wgt;
                }
            return out3;
        }

        private float[] blockLight(Scene s, int k, int bx, int by, float blk) {
            final long key = ((long) k << 32) | (((long) (by + 1) * (s.w + 2) + bx + 1) & 0xFFFFFFFFL);
            final java.util.HashMap<Long, float[]> lights = s.layerLight;
            float[] l = lights.get(key);
            if (l != null) return l;
            // the block's centre on the triangle's plane (its corners' weights there, maybe outside it)
            final int a = s.tri[k * 3], b = s.tri[k * 3 + 1], c = s.tri[k * 3 + 2];
            final float px0 = (bx + 0.5f) * blk, py0 = (by + 0.5f) * blk;
            final float ax = s.sx[a], ay = s.sy[a], bxx = s.sx[b], byy = s.sy[b], cx = s.sx[c], cy = s.sy[c];
            final float area = (bxx - ax) * (cy - ay) - (cx - ax) * (byy - ay);
            final float inv = Math.abs(area) > 1e-9f ? 1f / area : 0f;
            final float w1 = ((cx - px0) * (ay - py0) - (ax - px0) * (cy - py0)) * inv;
            final float w2 = ((ax - px0) * (byy - py0) - (bxx - px0) * (ay - py0)) * inv;
            final float w0 = 1f - w1 - w2;
            final double px = w0 * s.p[a * 3] + w1 * s.p[b * 3] + w2 * s.p[c * 3];
            final double py = w0 * s.p[a * 3 + 1] + w1 * s.p[b * 3 + 1] + w2 * s.p[c * 3 + 1];
            final double pz = w0 * s.p[a * 3 + 2] + w1 * s.p[b * 3 + 2] + w2 * s.p[c * 3 + 2];
            s.faceNormal(k, ng);
            s.shadingNormal(k, w1, w2, nn);
            s.visibility(px, py, pz, ng, nn, s.look.skyRays + s.look.skyRays / 2, s.look.lightRays, (int) (key * 31 + 17), stack, vis);
            l = new float[] {(float) vis[0], (float) vis[1], (float) vis[2]};
            lights.put(key, l);
            return l;
        }

        int add(int i, float depth, float alpha) {
            if (n == z.length) {
                final int c = n * 2;
                next = Arrays.copyOf(next, c);
                z = Arrays.copyOf(z, c);
                a = Arrays.copyOf(a, c);
                rgb = Arrays.copyOf(rgb, c * 3);
                if (parts != null) parts = Arrays.copyOf(parts, c * 16);
            }
            z[n] = depth;
            a[n] = alpha;
            next[n] = head[i];
            head[i] = n;
            return n++;
        }

        void color(int f, float[] c) {
            rgb[f * 3] = c[0];
            rgb[f * 3 + 1] = c[1];
            rgb[f * 3 + 2] = c[2];
        }

        float[] parts() {
            if (parts == null || parts.length < z.length * 16)
                parts = parts == null ? new float[z.length * 16] : Arrays.copyOf(parts, z.length * 16);
            return parts;
        }

        /** Each sample's layers, far to near, over what is there (premultiplied). */
        void resolve(float[] col, float[] cp) {
            for (int i = 0; i < samples; i++) {
                if (head[i] < 0) continue;
                int k = 0;
                for (int f = head[i]; f >= 0; f = next[f]) {
                    if (k == idx.length) idx = Arrays.copyOf(idx, k * 2);
                    idx[k++] = f;
                }
                for (int p = 1; p < k; p++) {  // far first (insertion sort: few layers)
                    final int f = idx[p];
                    int q = p - 1;
                    while (q >= 0 && z[idx[q]] < z[f]) {
                        idx[q + 1] = idx[q];
                        q--;
                    }
                    idx[q + 1] = f;
                }
                for (int p = 0; p < k; p++) {
                    final int f = idx[p];
                    final float al = a[f], keep = 1f - al;
                    col[i * 4] = rgb[f * 3] * al + col[i * 4] * keep;
                    col[i * 4 + 1] = rgb[f * 3 + 1] * al + col[i * 4 + 1] * keep;
                    col[i * 4 + 2] = rgb[f * 3 + 2] * al + col[i * 4 + 2] * keep;
                    col[i * 4 + 3] = al + col[i * 4 + 3] * keep;
                    if (cp != null && parts != null)
                        for (int q = 0; q < 15; q++) cp[i * 16 + q] = parts[f * 16 + q] * al + cp[i * 16 + q] * keep;
                }
            }
        }
    }

    static int toSrgb8(float v) {
        if (!(v > 0)) return 0;
        if (v >= 1) return 255;
        final double s = v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
        return (int) Math.round(s * 255);
    }

    /**
     * One triangle over a band of the sample grid. Opaque pass (blend false): the nearest wins (a see-through
     * picture's texels below 0.99 left for the blended pass). Blended pass: each layer in front of the opaque kept.
     */
    private static void raster(Scene s, int k, int y0, int nr, Tex tex, float[] depth, int[] id, float[] b1, float[] b2,
            Shader sh, Frags frags, boolean parts, boolean blend) {
        final int a = s.tri[k * 3], b = s.tri[k * 3 + 1], c = s.tri[k * 3 + 2];
        final float ax = s.sx[a], ay = s.sy[a], bx = s.sx[b], by = s.sy[b], cx = s.sx[c], cy = s.sy[c];
        final float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (!(Math.abs(area) > 1e-9f)) return;
        final float inv = 1f / area;
        final int sw = s.sw;
        int xa = (int) Math.floor(Math.min(ax, Math.min(bx, cx)) - 0.5f), xb = (int) Math.ceil(Math.max(ax, Math.max(bx, cx)) - 0.5f);
        int ya = (int) Math.floor(Math.min(ay, Math.min(by, cy)) - 0.5f), yb = (int) Math.ceil(Math.max(ay, Math.max(by, cy)) - 0.5f);
        xa = Math.max(xa, 0);
        xb = Math.min(xb, sw - 1);
        ya = Math.max(ya, y0);
        yb = Math.min(yb, y0 + nr - 1);
        final float za = s.sz[a], zb = s.sz[b], zc = s.sz[c];
        final float[] c4 = blend ? new float[4] : null;
        final float ua = s.uv[a * 2], va = s.uv[a * 2 + 1], ub = s.uv[b * 2], vb = s.uv[b * 2 + 1], uc = s.uv[c * 2],
                vc = s.uv[c * 2 + 1];
        for (int yy = ya; yy <= yb; yy++) {
            final float py = yy + 0.5f;
            final int row = (yy - y0) * sw;
            for (int xx = xa; xx <= xb; xx++) {
                final float px = xx + 0.5f;
                final float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) * inv;  // weight of b
                final float w2 = ((ax - px) * (by - py) - (bx - px) * (ay - py)) * inv;  // weight of c
                final float w0 = 1f - w1 - w2;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                final float z = w0 * za + w1 * zb + w2 * zc;
                final int i = row + xx;
                if (!(z < depth[i])) continue;
                if (!blend) {
                    if (tex != null) {
                        final float u = w0 * ua + w1 * ub + w2 * uc, v = w0 * va + w1 * vb + w2 * vc;
                        if (tex.alphaAt(u, v) < 0.99f) continue;
                    }
                    depth[i] = z;
                    id[i] = k;
                    b1[i] = w1;
                    b2[i] = w2;
                } else {
                    final float u = w0 * ua + w1 * ub + w2 * uc, v = w0 * va + w1 * vb + w2 * vc;
                    final float al = tex.alphaAt(u, v);
                    if (al >= 0.99f || al < 0.002f) continue;
                    final int f = frags.add(i, z, al);
                    sh.shade(k, w1, w2, c4, parts ? frags.parts() : null, f * 16, frags.light(s, k, w1, w2, xx, yy));
                    frags.color(f, c4);
                }
            }
        }
    }

    /** The light at a point of a triangle (linear RGB, unassociated: the surface's own colour lit). */
    static final class Shader {
        final Scene s;
        final Mesh m;
        final Tex[] tex;
        final double kAmb, kBounce, kKey, kRim, kSpec, alpha2;
        final float[] t4 = new float[4];
        final float[] sky;
        final double[] nn = new double[3], ek = new double[2], er = new double[2];

        Shader(Scene s, Mesh m, Tex[] tex, Look look) {
            this.s = s;
            this.m = m;
            this.tex = tex;
            kAmb = look.sky;
            kBounce = look.bounce;
            kKey = look.key * s.keyP / (s.height * s.height);
            kRim = look.rim * s.rimP / (s.height * s.height);
            kSpec = look.spec;
            final double al = look.roughness * look.roughness;
            alpha2 = al * al;
            sky = new float[] {(float) look.skyRgb[0], (float) look.skyRgb[1], (float) look.skyRgb[2]};
        }

        /** vis: sky / key / rim seen there (the pixel's rays), or null: from the triangle's corners. */
        void shade(int k, float w1, float w2, float[] out, float[] parts, int po, float[] vis) {
            final int a = s.tri[k * 3], b = s.tri[k * 3 + 1], c = s.tri[k * 3 + 2];
            final float w0 = 1f - w1 - w2;
            final float u = w0 * s.uv[a * 2] + w1 * s.uv[b * 2] + w2 * s.uv[c * 2];
            final float v = w0 * s.uv[a * 2 + 1] + w1 * s.uv[b * 2 + 1] + w2 * s.uv[c * 2 + 1];
            tex[m.triMat[k]].sample(u, v, t4);
            float ar = t4[0], ag = t4[1], ab = t4[2];
            if (t4[3] > 1e-4f) {
                final float ia = 1f / t4[3];
                ar *= ia;
                ag *= ia;
                ab *= ia;
            }
            s.shadingNormal(k, w1, w2, nn);
            final double nx = nn[0], ny = nn[1], nz = nn[2];
            final double px = w0 * s.p[a * 3] + w1 * s.p[b * 3] + w2 * s.p[c * 3];
            final double py = w0 * s.p[a * 3 + 1] + w1 * s.p[b * 3 + 1] + w2 * s.p[c * 3 + 1];
            final double pz = w0 * s.p[a * 3 + 2] + w1 * s.p[b * 3 + 2] + w2 * s.p[c * 3 + 2];
            final double vs, vk, vr;
            if (vis != null) {
                vs = vis[0];
                vk = vis[1];
                vr = vis[2];
            } else {
                vs = w0 * s.visSky[a] + w1 * s.visSky[b] + w2 * s.visSky[c];
                vk = w0 * s.visKey[a] + w1 * s.visKey[b] + w2 * s.visKey[c];
                vr = w0 * s.visRim[a] + w1 * s.visRim[b] + w2 * s.visRim[c];
            }
            final double vx = -s.d[0], vy = -s.d[1], vz = -s.d[2];
            lightAt(px, py, pz, nx, ny, nz, vx, vy, vz, s.keyPos, s.keyDir, ek);
            lightAt(px, py, pz, nx, ny, nz, vx, vy, vz, s.rimPos, s.rimDir, er);
            final double dk = ek[0] * vk, dr = er[0] * vr, sp = (ek[1] * vk * kKey + er[1] * vr * kRim) * kSpec;
            final double amb = kAmb * (vs + kBounce * (1 - vs));
            final double lit = kKey * dk + kRim * dr;
            out[0] = (float) (ar * (sky[0] * amb + lit) + sp);
            out[1] = (float) (ag * (sky[1] * amb + lit) + sp);
            out[2] = (float) (ab * (sky[2] * amb + lit) + sp);
            if (parts != null) {
                final float[] alb = {ar, ag, ab};
                final double h2 = s.height * s.height;
                for (int ch = 0; ch < 3; ch++) {
                    parts[po + ch] = (float) (alb[ch] * sky[ch] * vs);
                    parts[po + 3 + ch] = (float) (alb[ch] * sky[ch] * (1 - vs));
                    parts[po + 6 + ch] = (float) (alb[ch] * dk * s.keyP / h2);
                    parts[po + 9 + ch] = (float) (alb[ch] * dr * s.rimP / h2);
                    parts[po + 12 + ch] = (float) ((ek[1] * vk * s.keyP + er[1] * vr * s.rimP) / h2);
                }
            }
        }

        /** out[0]: cos at the surface x cos at the light / d^2 (in heights); out[1]: the GGX highlight of that. */
        private void lightAt(double px, double py, double pz, double nx, double ny, double nz, double vx, double vy, double vz,
                double[] pos, double[] dir, double[] out) {
            double lx = pos[0] - px, ly = pos[1] - py, lz = pos[2] - pz;
            final double d2 = lx * lx + ly * ly + lz * lz, dl = Math.sqrt(d2);
            lx /= dl;
            ly /= dl;
            lz /= dl;
            final double nl = nx * lx + ny * ly + nz * lz;
            final double cl = Math.max(0, -(lx * dir[0] + ly * dir[1] + lz * dir[2]));
            if (nl <= 0 || cl <= 0) {
                out[0] = out[1] = 0;
                return;
            }
            final double e = nl * cl / d2 * s.height * s.height;
            out[0] = e;
            if (kSpec == 0) {
                out[1] = 0;
                return;
            }
            double hx = lx + vx, hy = ly + vy, hz = lz + vz;
            final double hl = Math.sqrt(hx * hx + hy * hy + hz * hz);
            hx /= hl;
            hy /= hl;
            hz /= hl;
            final double nh = Math.max(0, nx * hx + ny * hy + nz * hz), nv = Math.max(1e-4, nx * vx + ny * vy + nz * vz);
            final double dd = nh * nh * (alpha2 - 1) + 1;
            final double dist = alpha2 / (Math.PI * dd * dd);
            final double k2 = alpha2 / 2;
            final double g = (nl / (nl * (1 - k2) + k2)) * (nv / (nv * (1 - k2) + k2));
            final double vh = Math.max(0, vx * hx + vy * hy + vz * hz);
            final double fr = 0.04 + 0.96 * Math.pow(1 - vh, 5);
            out[1] = Math.PI * dist * g * fr / (4 * nl * nv) * e;
        }
    }

    // ---------------------------------------------------------------- small maths

    static double norm(double[] v) {
        final double l = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0) {
            v[0] /= l;
            v[1] /= l;
            v[2] /= l;
        }
        return l;
    }

    static void cross(double[] a, double[] b, double[] o) {
        final double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
        o[0] = x;
        o[1] = y;
        o[2] = z;
    }

    /** Two unit vectors at right angles to n (unit) and to each other. */
    static void basis(double[] n, double[] u, double[] v) {
        final double[] a = Math.abs(n[0]) < 0.9 ? new double[] {1, 0, 0} : new double[] {0, 1, 0};
        cross(n, a, u);
        norm(u);
        cross(n, u, v);
        norm(v);
    }

    static double radical(int i) {
        final long b = Integer.reverse(i) & 0xFFFFFFFFL;
        return b / 4294967296.0;
    }

    static double hash01(int i) {
        int h = i * 0x9E3779B1;
        h ^= h >>> 16;
        h *= 0x85EBCA6B;
        h ^= h >>> 13;
        return (h & 0xFFFFFF) / 16777216.0;
    }
}
