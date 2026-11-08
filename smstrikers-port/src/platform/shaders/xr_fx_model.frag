// strikers-quest r96: a character standing in your room (xr_fx_model.vert): its picture, lit from
// above and in front with a soft fill, a rim of light round its outline, turned white as it evolves
// (the games' glowing silhouette) and glowing. Premultiplied out, like everything in the effects layer.
#version 450

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 camPos;
    vec4 look;     // x white, y alpha, z glow, w soft part (blended, its picture's alpha)
    vec4 glowRgb;  // w: light from below
    vec4 up;       // r142: the room's up in the model's own frame (a special card's, held any way; else 0, 1, 0)
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vPos;
layout(location = 2) in vec3 vNrm;

layout(location = 0) out vec4 outColor;

// r147 (Lance: "i want that black stone glass to look like a damn kaleidoscope dream inside showcasing and spotlighting
// the character within the stone glass"): the BLACK 1/1's slab, inside - a living kaleidoscope. Its back wall a mirrored
// pattern, its four walls MIRRORS (each shows the back wall reflected: your eye's ray carried on to the back wall's plane and
// folded back into it, as a kaleidoscope's mirror box does - the pattern repeats, mirrored, away into the stone), and two
// panes of it floating in the stone in front (each seen where your eye's ray crosses it: they slide against each other as
// it tilts). 8 mirrors (16 wedges); big prismatic shards in the character's own colour, its jewel companions (its hue a
// third round each way) and obsidian violet, faint creases of light where they meet; each pane turning its own way, the
// shards drifting and morphing slowly. Dimmer round the middle, a warm glow and a shaft of light from above behind the
// character's picture (its own part, in front) so it's spotlit. tools/outside/cards147.py ports this line for line.
vec3 kal_jewel(vec3 c, float x) {  // the palette round a band (0..1): c, its two companions, violet; smooth between
    vec3 s = mix(vec3(dot(c, vec3(0.333))), c, 2.0);
    s = clamp(s, 0.0, 1.0);
    vec3 j0 = s, j1 = s.gbr, j2 = vec3(0.42, 0.14, 0.86), j3 = s.brg;
    float k = fract(x) * 4.0;
    float f = smoothstep(0.0, 1.0, fract(k));
    vec3 a = k < 1.0 ? j0 : (k < 2.0 ? j1 : (k < 3.0 ? j2 : j3));
    vec3 b = k < 1.0 ? j1 : (k < 2.0 ? j2 : (k < 3.0 ? j3 : j0));
    return mix(a, b, f);
}
// one pane: q its point (about -1..1 over the window), L 0 the back wall, 1 / 2 the panes nearer you; xyz its colour, w
// how much of it shows (the back wall all; a pane its shards' edges and a little of their glass)
vec4 kal_layer(vec2 q, float t, float L, vec3 c) {
    float r = length(q);
    float turn = (L < 0.5 ? 0.07 : (L < 1.5 ? -0.1 : 0.13));
    float a = atan(q.y, q.x) + t * turn;
    const float seg = 6.2831853 / 8.0;
    a = abs(mod(a, seg) - 0.5 * seg);  // (8 mirrors: each wedge and its reflection)
    vec2 m = r * vec2(cos(a), sin(a)) * (1.0 + 0.35 * L);
    float s1 = abs(fract(m.x * 1.6 + m.y * 0.9 + 0.15 * sin(t * 0.23 + L)) - 0.5);
    float s2 = abs(fract(-m.x * 0.8 + m.y * 1.9 - t * 0.03 + L * 0.37) - 0.5);
    float s3 = abs(fract(r * 2.2 * (1.0 + 0.2 * L) - t * 0.05 * (1.0 + L)) - 0.5);
    float facet = s1 + s2 + 0.8 * s3;
    vec3 col = kal_jewel(c, facet * 1.1 + 0.05 * t + 0.25 * L) * (0.08 + 0.95 * smoothstep(0.25, 1.1, facet));  // (jewels on black glass)
    float crease = 1.0 - smoothstep(0.0, 0.03, min(min(s1, s2), s3));
    col += crease * mix(vec3(1.0, 0.96, 0.9), c, 0.4) * (L < 0.5 ? 0.18 : 0.26);
    float show = L < 0.5 ? 1.0 : clamp(0.1 + 0.6 * crease + 0.25 * smoothstep(0.6, 1.2, facet), 0.0, 1.0);
    return vec4(col, show);
}
// a point's place folded back into the box between -h and h (a mirror at each side), from the middle
float kal_fold(float x, float h) { return 2.0 * h - abs(mod(x + h, 4.0 * h) - 2.0 * h) - h; }
// p: where your eye's ray (through this fragment) meets the back wall's plane (outside the window: seen in a mirror);
// mid, hw / hh the window's middle and half sizes
vec3 kaleido(vec3 p, vec3 cam, vec2 mid, float hw, float hh, float t, float spot, vec3 c) {
    vec2 d0 = p.xy - mid;
    vec2 f0 = vec2(kal_fold(d0.x, hw), kal_fold(d0.y, hh));
    vec2 q = f0 / hw;
    vec3 col = kal_layer(q, t, 0.0, c).rgb;
    for (int i = 1; i <= 2; i++) {  // (the panes: where your eye's ray crosses them, 4.5 and 3 cm into the stone)
        float zl = i == 1 ? -0.045 : -0.03;
        vec2 d = (cam + (p - cam) * ((zl - cam.z) / (p.z - cam.z))).xy - mid;
        vec4 li = kal_layer(vec2(kal_fold(d.x, hw), kal_fold(d.y, hh)) / hw, t, float(i), c);
        col = mix(col, li.rgb, li.w * 0.4);
    }
    float rc = length(q);
    col *= 0.25 + 0.75 * smoothstep(0.35, 1.0, rc);                                // (dimmer round the middle)
    col *= 1.0 - 0.35 * smoothstep(0.82, 1.0, max(abs(q.x), abs(f0.y / hh)));      // (the mirrors' seams)
    if (abs(d0.x) > hw || abs(d0.y) > hh) col *= 0.72;                              // (in a mirror: a little darker)
    vec3 warm = mix(vec3(1.0, 0.95, 0.86), c, 0.25);
    col += warm * exp(-rc * rc / 0.3) * (0.4 + 0.45 * spot);                         // (a glow behind the character)
    col += warm * exp(-q.x * q.x / 0.03) * smoothstep(-0.3, 1.0, q.y) * (0.1 + 0.18 * spot);  // (a shaft from above)
    return col;
}
// r160 (Lance: binder skins - "literal different skins from aligator, to leather, to furry, and everything in between"):
// the sRGB sampler hands back linear values; the skin atlas's lower half holds plain bytes (normal XY, roughness) - back to
// what was stored
vec3 to_stored(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 rainbow6(float h) {
    h = fract(h) * 6.0;
    return clamp(vec3(abs(h - 3.0) - 1.0, 2.0 - abs(h - 2.0), 2.0 - abs(h - 4.0)), 0.0, 1.0);
}
// the room round it as a soft environment (for metal / gloss): a bright ceiling, warm walls, a darker floor
vec3 env_of(vec3 r, vec3 U, float rough) {
    float y = dot(r, U);
    vec3 sky = vec3(0.92, 0.94, 1.0), wall = vec3(0.46, 0.40, 0.34), flr = vec3(0.10, 0.09, 0.08);
    vec3 sharp = y > 0.0 ? mix(wall, sky, smoothstep(0.05, 0.6, y)) : mix(wall, flr, smoothstep(0.0, 0.45, -y));
    sharp += vec3(1.6, 1.5, 1.35) * smoothstep(0.86, 0.97, y) * (1.0 - rough);  // (a ceiling light's window)
    vec3 blur = vec3(0.42, 0.40, 0.38);
    return mix(sharp, blur, clamp(rough * rough * 1.2, 0.0, 1.0));
}
// ====================================================================================================================
// r163 (Lance: "heavyyyyy on every special skin is moving and alive"; "the lava binder ... should be glowing almost where
// the cracks in the book are and where the lava is"; the slime "constantly dripping from it ... just a state of
// animation"): the binder's living skins. Per skin (pc.up.w = its catalogue index + 1: xr_cards.cpp kSkins / skins.py
// SKINS) its own motion over its pictures - lava flowing along its cracks, slime's wet sheen sliding down it, frost
// crawling in from its edges, the galaxy swirling and stars twinkling deep in it, neon pulsing, light sweeping over
// metal, 8-bit sprites playing on its cover, a light circling slowly round every one. Its halos, its light on the floor,
// its drips / embers / mist / smoke / stars: xr_binderfx.cpp (bfx_main below). tools/outside/binders163/shade163.py ports
// this line for line (the PC proof videos, living163.py).
float bh(vec2 p) {  // a hash (no sine: the same on every GPU)
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float bn(vec2 p) {  // value noise
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(bh(i), bh(i + vec2(1.0, 0.0)), f.x), mix(bh(i + vec2(0.0, 1.0)), bh(i + vec2(1.0, 1.0)), f.x), f.y);
}
// a band of light sweeping across it on the slant every `period` s (sp: its surface in metres)
float bsweep(vec2 sp, float t, float period, float width) {
    float x = dot(sp, vec2(0.83, 0.56)) - (fract(t / period) * 1.3 - 0.3);
    return exp(-x * x / (width * width));
}
vec3 lava_rgb(float h) {  // molten rock's light by its heat: dark red, orange, yellow, white-hot
    return vec3(1.0, 0.22, 0.02) * smoothstep(0.0, 0.45, h) + vec3(0.0, 0.40, 0.05) * smoothstep(0.3, 0.95, h) +
           vec3(0.0, 0.28, 0.42) * smoothstep(0.85, 1.5, h);
}
vec3 s2l(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
// the 8-bit cover's sprites (tools/outside/binders163/pixel_sprites.py: 4 bits a cell, cell 0 lowest; its palette sRGB)
const uint kPxCoin[16] = uint[](0x330u, 0x3223u, 0x3223u, 0x330u, 0x330u, 0x320u, 0x320u, 0x330u, 0x30u, 0x20u, 0x20u, 0x30u, 0x330u, 0x230u, 0x230u, 0x330u);
const uint kPxStar[15] = uint[](0x200u, 0x2220u, 0x22222u, 0x2220u, 0x20002u, 0x0u, 0x200u, 0x2220u, 0x200u, 0x0u, 0x0u, 0x0u, 0x400u, 0x0u, 0x0u);
const uint kPxQblock[7] = uint[](0x111111u, 0x1b44b1u, 0x14bbb1u, 0x1b4bb1u, 0x1bbbb1u, 0x1b4bb1u, 0x111111u);
const uint kPxRunner[16] = uint[](0x11100u, 0x66610u, 0x61600u, 0x66600u, 0x55550u, 0x55550u, 0x40040u, 0x110011u, 0x11100u, 0x66610u, 0x61600u, 0x66600u, 0x55550u, 0x55500u, 0x4400u, 0x1110u);
const vec3 kPxPal[12] = vec3[](vec3(0.0), vec3(0.0500, 0.0500, 0.0800), vec3(1.0000, 0.8200, 0.1000), vec3(0.8500, 0.4500, 0.0500), vec3(0.9700, 0.9700, 0.9700), vec3(0.9200, 0.1200, 0.2000), vec3(0.9800, 0.7600, 0.5600), vec3(0.1800, 0.2800, 0.8500), vec3(0.3600, 0.1600, 0.0500), vec3(0.3600, 0.5800, 0.9800), vec3(0.3000, 0.5000, 0.9200), vec3(0.9800, 0.6600, 0.1200));
int px_cell(uint row, int x) { return int((row >> uint(4 * x)) & 15u); }
// which palette colour a sprite puts in this 16 px cell of the strip (64 x 32 cells), -1 none (the picture as it is): the
// four coins spinning (8 frames a second), its two stars twinkling, a ? block bumped up every 1.6 s, a little footballer
// running across the front (a cell a step, 10 steps a second, his legs every step)
int pixel_sprite(vec2 uv, float t) {
    int cx = int(floor(uv.x * 64.0)), cy = int(floor(uv.y * 32.0));
    int c = -1;
    const ivec2 kCoins[4] = ivec2[](ivec2(4, 21), ivec2(22, 21), ivec2(38, 21), ivec2(58, 21));
    for (int i = 0; i < 4; i++) {
        int dx = cx - kCoins[i].x;
        if (cy >= 21 && cy < 25 && dx >= 0 && dx < 4) {
            int f = int(mod(floor(t * 8.0) + float(i), 4.0));
            int v = px_cell(kPxCoin[f * 4 + cy - 21], dx);
            c = v == 0 ? 10 : v;
        }
    }
    for (int i = 0; i < 2; i++) {
        ivec2 o = i == 0 ? ivec2(37, 3) : ivec2(57, 4);
        int dx = cx - o.x, dy = cy - o.y;
        if (dx >= 0 && dx < 5 && dy >= 0 && dy < 5) {
            int ph = int(mod(floor(t * 6.0) + float(i) * 3.0, 6.0));
            int f = ph < 3 ? 0 : (ph == 4 ? 2 : 1);
            int v = px_cell(kPxStar[f * 5 + dy], dx);
            c = v == 0 ? 9 : v;
        }
    }
    {
        int up = fract(t / 1.6) < 0.12 ? 1 : 0;
        int dx = cx - 57, dy = cy - (11 - up);
        if (dx >= 0 && dx < 6 && cy >= 10 && cy < 18) {
            int v = (dy >= 0 && dy < 7) ? px_cell(kPxQblock[dy], dx) : 0;
            c = v == 0 ? 9 : v;
        }
    }
    {
        float st = floor(t * 10.0);
        int x0 = 29 + int(mod(st, 54.0));
        int dx = cx - x0, dy = cy - 20;
        if (cx >= 35 && dx >= 0 && dx < 6 && dy >= 0 && dy < 8) {
            int v = px_cell(kPxRunner[int(mod(st, 2.0)) * 8 + dy], dx);
            if (v != 0) c = v;
        }
    }
    return c;
}
// r160: the 3D binder (glowRgb.w -5.5, xr_cards.cpp room_binder; a local model, `up` = the room's up in its frame). Its
// parts by their normals' length: 1 the skin (the atlas: rows 8..503 its colour, A metal; rows 520..1015 normal XY,
// roughness, A emissive / fur strand height), 2 its pages (a picture, glossy pocket plastic), 3 chrome (rings), 4..5 a fur
// shell (4 + its height 0..1). look.x: 1 holo, 2 sparkle, 4 pulse (bits); look.z: the emissive's boost; glowRgb.rgb its
// emissive colour. r163: up.w its skin's catalogue index + 1 (its living touch, above).
void binder_main(float nl) {
    vec3 v = normalize(pc.camPos.xyz - vPos);
    vec3 N = vNrm / max(nl, 1e-5);
    vec3 U = normalize(pc.up.xyz);
    vec3 F = vec3(0.0, 0.0, 1.0) - U * U.z;
    F = dot(F, F) > 1e-4 ? normalize(F) : vec3(0.0, 0.0, 1.0);
    vec3 key = normalize(0.35 * cross(U, F) + 0.85 * U + 0.4 * F);
    vec3 fill = normalize(-0.5 * cross(U, F) + 0.2 * U + 0.8 * F);
    float tsec = pc.camPos.w;
    int si = int(pc.up.w + 0.5) - 1;  // r163: which skin
    // r163: a light circling slowly round it (its highlights glide over it even held still)
    float oa = tsec * 0.45;
    vec3 key2 = normalize(cross(U, F) * cos(oa) * 0.9 + U * 0.55 + F * (0.55 + 0.35 * sin(oa)));
    if (nl > 1.5 && nl < 2.5) {  // its pages: the picture, a glossy pocket over it
        vec4 t = texture(uTex, vUv);
        if (dot(N, v) < 0.0) N = -N;
        vec3 r = reflect(-v, N);
        float diff = max(dot(N, key), 0.0);
        vec3 col = t.rgb * (0.66 + 0.30 * diff + 0.08 * max(dot(N, fill), 0.0));
        float fres = 0.04 + 0.5 * pow(1.0 - max(dot(N, v), 0.0), 5.0);
        col += env_of(r, U, 0.25) * fres * 0.5 + vec3(pow(max(dot(r, key), 0.0), 70.0) * 0.45);
        outColor = vec4(col * pc.look.y, pc.look.y);
        return;
    }
    if (nl > 2.5 && nl < 3.5) {  // chrome (r163: the circling light glinting along its rings)
        if (dot(N, v) < 0.0) N = -N;
        vec3 r = reflect(-v, N);
        vec3 col = env_of(r, U, 0.12) * vec3(0.86, 0.88, 0.92) + vec3(pow(max(dot(r, key), 0.0), 120.0) * 1.4) +
                   vec3(pow(max(dot(r, key2), 0.0), 120.0) * 0.8);
        outColor = vec4(col * pc.look.y, pc.look.y);
        return;
    }
    // the skin
    float shell = nl > 3.5 ? clamp(nl - 4.0, 0.0, 1.0) : -1.0;
    vec2 uvL = clamp(vUv, 0.0, 1.0);
    if (si == 26) {
        // r163 galaxy: its spiral sloshing round (the inner arms more: they wind and unwind), the nebula drifting
        vec2 d = (uvL - vec2(0.77002, 0.5)) * vec2(1024.0, 512.0);
        vec2 e = vec2(d.x, d.y / 0.62);
        float rr = length(e);
        float a = 0.55 * sin(tsec * 0.13) * exp(-rr / 85.0) * smoothstep(230.0, 120.0, rr);
        float ca = cos(a), sa = sin(a);
        e = vec2(e.x * ca - e.y * sa, e.x * sa + e.y * ca);
        uvL = vec2(0.77002, 0.5) + vec2(e.x, e.y * 0.62) / vec2(1024.0, 512.0);
        vec2 spw = uvL * vec2(0.598, 0.31);
        uvL += (vec2(bn(spw * 9.0 + tsec * 0.05), bn(spw * 9.0 + 5.3 - tsec * 0.04)) - 0.5) * vec2(0.006, 0.012);
        uvL = clamp(uvL, 0.0, 1.0);
    }
    vec2 ua = vec2(uvL.x, (8.0 + uvL.y * 496.0) / 1024.0);
    vec2 ub = vec2(uvL.x, (520.0 + uvL.y * 496.0) / 1024.0);
    vec4 A = texture(uTex, ua);
    vec4 B = texture(uTex, ub);
    vec3 nb = to_stored(B.rgb);
    if (shell >= 0.0 && B.a < shell + 0.02) discard;  // (a fur shell: only the strands that reach this high)
    vec2 nxy = nb.xy * 2.0 - 1.0;
    float rough = clamp(nb.z, 0.03, 1.0);
    float metal = A.a;
    vec3 alb = A.rgb;
    vec2 sp = vUv * vec2(0.598, 0.31);  // r163: its surface in metres (u across the strip, v down it)
    if (si == 29) {  // r163 8-bit: its sprites playing (their cells over its picture, lit as its own)
        int c8 = pixel_sprite(clamp(vUv, 0.0, 0.9999), tsec);
        if (c8 > 0) {
            vec2 lp = fract(vUv * vec2(64.0, 32.0)) * 16.0;
            float lit = (lp.x < 3.0 || lp.y < 3.0) ? 1.12 : ((lp.x > 12.0 || lp.y > 12.0) ? 0.78 : 1.0);
            alb = s2l(min(kPxPal[c8] * lit, vec3(1.0)));
            metal = 0.0;
        }
    }
    if (si == 32) {  // r163 slime: its surface wobbling slowly (two waves' slopes tilt its light: the highlights swim)
        float w1 = sp.x * 31.0 + sp.y * 17.0 + tsec * 1.3, w2 = -sp.x * 13.0 + sp.y * 37.0 - tsec * 0.9;
        nxy += 0.11 * vec2(cos(w1) * 0.9 - cos(w2) * 0.4, cos(w1) * 0.5 + cos(w2) * 0.9);
    }
    // its tangent frame from the screen's derivatives (u right, v down the picture: +Y of the normal = up the picture)
    vec3 dpx = dFdx(vPos), dpy = dFdy(vPos);
    vec2 dux = dFdx(vUv), duy = dFdy(vUv);
    vec3 r1 = cross(dpy, N), r2 = cross(N, dpx);
    vec3 T = r1 * dux.x + r2 * duy.x;
    vec3 Bt = -(r1 * dux.y + r2 * duy.y);
    float tl = max(dot(T, T), dot(Bt, Bt));
    vec3 n = N;
    vec3 Tn = vec3(1.0, 0.0, 0.0), Bn = vec3(0.0, 1.0, 0.0);  // r163: the frame's unit axes (in-surface motion)
    if (tl > 1e-20) {
        float inv = inversesqrt(tl);
        float zz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
        float k = shell >= 0.0 ? 0.6 : 1.0;  // (fur: the strands' tilt, softened)
        n = normalize(T * inv * nxy.x * k + Bt * inv * nxy.y * k + N * zz);
        Tn = normalize(T);
        Bn = normalize(Bt);
    }
    if (dot(N, v) < 0.0) { n = -n; N = -N; }
    // r163: "down" along its surface (sp's axes), for what runs and flows (lying flat: down its picture)
    vec2 gd = vec2(dot(-U, Tn), -dot(-U, Bn));
    float gl = length(gd);
    vec2 fd = normalize(gd + vec2(0.0, 0.25) * (1.0 - smoothstep(0.15, 0.45, gl)) + vec2(1e-4, 0.0));
    vec3 r = reflect(-v, n);
    float ndv = max(dot(n, v), 0.0);
    float diff = max(dot(n, key), 0.0), diff2 = max(dot(n, fill), 0.0);
    float ao = shell >= 0.0 ? mix(0.38, 1.0, shell) : 1.0;  // (fur: dark at the roots)
    vec3 f0 = mix(vec3(0.04), alb, metal);
    vec3 fres = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0) * (1.0 - rough * 0.85);
    float sp1 = pow(max(dot(r, key), 0.0), mix(400.0, 6.0, rough)) * mix(3.0, 0.25, rough);
    vec3 col = alb * (1.0 - metal) * (0.30 + 0.78 * diff + 0.18 * diff2) * ao;
    col += env_of(r, U, rough) * fres * mix(0.55, 1.0, metal) * ao;
    col += sp1 * mix(vec3(1.0), alb, metal) * ao;
    if (shell >= 0.0) col += alb * pow(1.0 - ndv, 2.0) * 0.25 * shell;  // (the fur's sheen at its edges)
    // r163: the circling light's highlight (how much: by skin, below)
    float sp2 = pow(max(dot(r, key2), 0.0), mix(500.0, 10.0, rough)) * mix(1.6, 0.10, rough);
    float k2 = 0.35;
    int flags = int(pc.look.x + 0.5);
    bool customEm = false;
    float em = B.a;
    if (si == 30) {
        // molten lava: lava FLOWING along its cracks (a noise carried downhill, two phases crossfaded so it never
        // stretches), white-hot where it runs thickest, its glow spilling onto the rock round them (its cracks' mask
        // blurred: a coarse mip), all of it breathing
        float ph = fract(tsec * 0.2), wA = 1.0 - abs(2.0 * ph - 1.0), ph2 = fract(ph + 0.5);
        vec2 q = sp * 36.0;
        float f1 = bn(q - fd * ph * 2.6) * 0.65 + bn(q * 2.3 - fd * ph * 5.2 + 3.1) * 0.35;
        float f2 = bn(q - fd * ph2 * 2.6 + 7.7) * 0.65 + bn(q * 2.3 - fd * ph2 * 5.2 + 9.4) * 0.35;
        float flow = mix(f2, f1, wA);
        float breathe = 0.86 + 0.14 * sin(tsec * 1.25 + sp.x * 9.0 - sp.y * 6.0);
        float heat = em * (0.30 + 1.9 * flow * flow) * breathe;
        float spill = textureLod(uTex, ub, 4.0).a;
        col = col * (1.0 - 0.6 * em) + lava_rgb(heat) * heat * 1.5 + vec3(1.0, 0.26, 0.04) * spill * spill * (0.9 + 0.5 * flow) * breathe;
        customEm = true;
        k2 = 0.15;
    } else if (si == 32) {
        // slime: wet light sliding down it in streaks, lit through (brighter at its edges as you look across it), tiny
        // bubbles rising in it (deeper than its surface: they shift as it tilts), its soft inner glow
        vec2 fs = vec2(-fd.y, fd.x);
        float s = bn(vec2(dot(sp, fs) * 70.0, dot(sp, fd) * 7.0 - tsec * 0.8));
        float sheen = smoothstep(0.58, 0.88, s);
        col += env_of(r, U, 0.04) * sheen * 0.28 + vec3(0.85, 1.0, 0.65) * sheen * pow(max(dot(r, key), 0.0), 10.0) * 0.5;
        float lt = 0.85 + 0.15 * sin(tsec * 1.4 + dot(sp, fd) * 25.0);
        col += vec3(0.16, 0.55, 0.05) * (0.10 + 0.35 * pow(1.0 - ndv, 2.0)) * lt;
        vec2 vo = vec2(dot(v, Tn), -dot(v, Bn)) / max(ndv, 0.3) * 0.004;
        vec2 bq = (sp - vo) * 55.0 + fd * tsec * 0.6;
        vec2 bc = floor(bq), bf = fract(bq) - 0.5;
        float bhh = bh(bc + 17.0);
        vec2 off = (vec2(fract(bhh * 3.1), fract(bhh * 5.3)) - 0.5) * 0.4;
        float ring = smoothstep(0.07, 0.0, abs(length(bf - off) - (0.08 + 0.12 * fract(bhh * 13.7)))) * step(0.9, bhh);
        col += vec3(0.75, 1.0, 0.5) * ring * 0.1;
        col += pc.glowRgb.rgb * em * pc.look.z * 0.2 * lt;
        customEm = true;
        k2 = 0.6;
    } else if (si == 31) {
        // frozen ice: frost crawling in from its edges and back (feathery fingers, a bright crystallising rim), crystals
        // catching the light (facets of their own glinting as it turns); its glints twinkle (its mask: the emissive below)
        float pu = vUv.x < 0.46 ? vUv.x / 0.46 : (vUv.x > 0.54 ? (vUv.x - 0.54) / 0.46 : 0.5);
        float de = min(min(pu, 1.0 - pu) * 0.275, min(vUv.y, 1.0 - vUv.y) * 0.31);
        float fing = bn(sp * 60.0) * 0.6 + bn(sp * 160.0 + 3.0) * 0.4;
        float front = 0.012 + 0.03 * (0.5 + 0.5 * sin(tsec * 0.9 + bn(sp * 8.0) * 3.0)) + (fing - 0.5) * 0.028;
        float frost = smoothstep(front + 0.004, front - 0.004, de);
        float cr = 1.0 - abs(2.0 * bn(sp * 220.0 + 1.7) - 1.0);
        vec3 fcol = vec3(0.86, 0.94, 1.0) * (0.55 + 0.5 * cr) * (0.45 + 0.65 * diff) + env_of(r, U, 0.3) * 0.12;
        col = mix(col, fcol, frost * 0.8);
        col += vec3(0.7, 0.9, 1.0) * smoothstep(0.004, 0.0, abs(de - front)) * 0.45;
        float gh = bh(floor(sp * 650.0));
        vec3 nf = normalize(n + (Tn * (gh - 0.5) + Bn * (fract(gh * 7.13) - 0.5)) * 0.6);
        float g2 = pow(max(dot(reflect(-v, nf), key2), 0.0), 260.0) * step(0.86, gh) * (0.6 + 0.4 * sin(tsec * 3.0 + gh * 50.0));
        col += vec3(0.9, 0.97, 1.0) * g2 * 3.0;
        k2 = 0.6;
    } else if (si == 26) {
        // galaxy: stars deep in it (two layers further in: they slide past each other as it tilts), drifting and
        // twinkling; its arms' light turning slowly round its core; a shooting star across it every few seconds
        vec2 vt = vec2(dot(v, Tn), -dot(v, Bn)) / max(ndv, 0.25);
        for (int L = 0; L < 2; L++) {
            float dep = L == 0 ? 0.012 : 0.035, sc = L == 0 ? 260.0 : 150.0;
            vec2 q = (sp - vt * dep + vec2(0.0016, 0.0007) * tsec * (1.0 + float(L))) * sc;
            float hs = bh(floor(q) + 31.0 * float(L));
            vec2 o = fract(q) - 0.5 - (vec2(fract(hs * 7.3), fract(hs * 3.9)) - 0.5) * 0.6;
            float st = exp(-dot(o, o) * (L == 0 ? 160.0 : 110.0)) * step(0.86, hs) *
                       (0.45 + 0.55 * sin(tsec * (1.3 + 3.0 * fract(hs * 11.0)) + hs * 40.0));
            col += mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.85, 0.7), fract(hs * 5.0)) * max(st, 0.0) * (L == 0 ? 1.3 : 0.9);
        }
        vec2 gc = (vUv - vec2(0.77002, 0.5)) * vec2(1024.0, 512.0);
        vec2 ge = vec2(gc.x, gc.y / 0.62);
        float gr = length(ge) + 1.0, ga = atan(ge.y, ge.x);
        float arm = pow(0.5 + 0.5 * cos(2.0 * (ga - tsec * 0.07) - 4.4 * log(gr / 12.0)), 3.0) * smoothstep(190.0, 50.0, gr) *
                    smoothstep(4.0, 30.0, gr);
        col += vec3(0.45, 0.35, 0.9) * arm * 0.22 * (vUv.x > 0.54 ? 1.0 : 0.0);
        float per = 4.2, kk = floor(tsec / per), ft = fract(tsec / per) * per;
        float hk = bh(vec2(kk, 7.1));
        if (ft < 0.8) {
            vec2 p0 = vec2(0.04 + 0.5 * fract(hk * 5.1), 0.03 + 0.1 * fract(hk * 9.7));
            vec2 dr = normalize(vec2(fract(hk * 3.3) > 0.5 ? 1.0 : -1.0, 0.5 + 0.4 * fract(hk * 2.1)));
            vec2 dd = sp - (p0 + dr * ft * 0.32);
            float al = -dot(dd, dr), pe = length(dd + dr * al);
            float s = step(0.0, al) * (1.0 - smoothstep(0.0, 0.08, al)) * exp(-pe * pe / 4e-7) + exp(-dot(dd, dd) / 5e-6);
            col += vec3(0.9, 0.95, 1.0) * s * 2.2 * sin(ft / 0.8 * 3.14159);
        }
        k2 = 0.4;
    } else if (si == 28) {
        // neon grid: a pulse travelling through its lines, CRT scanlines rolling down, a flicker now and then (its halos
        // flicker with it: xr_binderfx.cpp, the same hash), the front's floor lines rolling toward you, its sun's bands
        float fl = bh(vec2(floor(tsec * 14.0), 3.7));
        float flick = fl > 0.96 ? 0.3 : (fl > 0.92 ? 0.7 : 1.0);
        float pz = fract(vUv.y * 1.2 + vUv.x * 0.35 - tsec * 0.45) - 0.5;
        float pulse = 0.55 + 1.1 * exp(-pz * pz / 0.004);
        float scan = 0.86 + 0.14 * sin(vUv.y * 1400.0 - tsec * 24.0);
        bool flr = vUv.x > 0.54 && vUv.y > 0.5195;
        vec3 neon = pc.glowRgb.rgb * em * pc.look.z * pulse * scan * (flr ? 0.55 : 1.0);
        if (flr) {
            float zz2 = vUv.y * 512.0 - 266.0;
            float f = fract(90.0 / zz2 * 2.0 - tsec * 1.1);
            float dpx = min(f, 1.0 - f) * 0.5 * zz2 * zz2 / 90.0;
            neon += vec3(0.35, 1.0, 1.0) * exp(-dpx * dpx / 1.2) * smoothstep(0.0, 10.0, zz2) * 1.6;
        }
        vec2 sd = (vUv - vec2(0.77002, 0.5)) * vec2(1024.0, 512.0) - vec2(0.0, 5.0);
        if (vUv.x > 0.54 && sd.y < 5.0 && length(sd) < 120.0) col += alb * (0.25 + 0.2 * sin(tsec * 2.0 - sd.y * 0.25));
        col += neon * flick;
        customEm = true;
        k2 = 0.2;
    } else if (si == 27) {
        // holographic: its rainbow shifting with time AND as it tilts, a bright band sweeping over it, its facets glinting
        float h = dot(r, vec3(0.62, 0.48, 0.62)) * 2.4 + vUv.x * 2.2 + vUv.y * 1.3 + tsec * 0.11 +
                  0.18 * sin(tsec * 0.6 + vUv.y * 7.0 + vUv.x * 4.0);
        col = mix(col, col * 0.35 + rainbow6(h) * (0.55 + 0.6 * metal), 0.62);
        float sw = bsweep(sp, tsec, 3.4, 0.035);
        col += (rainbow6(h + 0.5) * 0.4 + vec3(0.15)) * sw;
        float fh = bh(floor(sp * 180.0));
        col += vec3(1.0) * pow(max(0.0, sin(dot(r, vec3(9.0, 7.0, 5.0)) + fh * 30.0 + tsec * 2.0)), 24.0) * step(0.9, fh) * 1.2;
        k2 = 0.5;
    } else if (si == 33 || si == 21 || si == 22) {
        // gold plated / brushed chrome / diamond plate: a band of light travelling over it (xr_binderfx.cpp's glints ride
        // it); chrome's circling highlight drawn out across its brushing; the plate's treads twinkling
        float sw = bsweep(sp, tsec + float(si) * 0.7, si == 33 ? 3.2 : 4.0, si == 22 ? 0.05 : 0.035);
        col += sw * (env_of(r, U, rough * 0.5) * alb * 1.6 + alb * 0.35) * ao;
        if (si == 21) {
            vec3 h2 = normalize(key2 + v);
            float th = dot(Tn, h2);
            col += alb * pow(sqrt(max(0.0, 1.0 - th * th)), 80.0) * 0.7;
        }
        if (si == 22) {
            float gh = bh(floor(sp * 300.0));
            col += vec3(1.0) * step(0.93, gh) * (1.0 - smoothstep(0.25, 0.35, rough)) * pow(max(0.0, sin(tsec * 2.5 + gh * 60.0)), 16.0) * 1.5;
        }
        k2 = si == 21 ? 0.3 : 0.8;
    } else if (si == 24) {
        // dragon scales: an iridescent shimmer travelling over them, embers glowing between them (its mask), breathing
        float th = 1.0 - ndv;
        float hue = th * 1.3 + sp.x * 2.0 + sp.y * 1.5 + tsec * 0.05 + 0.15 * sin(tsec * 0.8 + sp.x * 25.0);
        float wave = 0.5 + 0.5 * sin(sp.x * 30.0 - sp.y * 12.0 - tsec * 1.6);
        col += rainbow6(hue) * (0.03 + 0.2 * th * th) * (0.4 + 0.6 * wave) * metal * 1.2;  // (on its rims: the scales stay crimson)
        float fl2 = bn(sp * 40.0 + vec2(0.0, -tsec * 0.5));
        col += pc.glowRgb.rgb * em * pc.look.z * 0.55 * (0.35 + 0.9 * fl2) * (0.75 + 0.25 * sin(tsec * 1.3));
        customEm = true;
        k2 = 0.5;
    } else if (si == 25) {
        // shark skin: water's light rippling over it (caustics), a wet sheen sliding down it
        vec2 q = sp * 45.0;
        float c1 = bn(q + vec2(tsec * 0.25, -tsec * 0.4)), c2 = bn(q * 1.3 - vec2(tsec * 0.2, tsec * 0.3) + 4.0);
        float caust = pow(1.0 - abs(c1 - c2), 14.0);
        vec2 fs = vec2(-fd.y, fd.x);
        float wet = smoothstep(0.55, 0.85, bn(vec2(dot(sp, fs) * 40.0, dot(sp, fd) * 6.0 - tsec * 0.55)));
        col += vec3(0.55, 0.8, 1.0) * caust * 0.06 + env_of(r, U, 0.06) * (wet * 0.22 + caust * 0.05);
        k2 = 0.6;
    } else if (si == 23) {  // marble: a soft polished reflection gliding over it
        col += env_of(r, U, 0.1) * bsweep(sp, tsec, 7.0, 0.06) * 0.22;
        k2 = 0.7;
    } else if (si == 34) {  // stone tablet: a slow wave of ember light along its carved runes (its mask)
        float wv = 0.5 + 0.5 * sin(tsec * 0.8 - sp.x * 9.0 + sp.y * 3.0);
        col += pc.glowRgb.rgb * em * pc.look.z * 0.45 * (0.15 + 0.85 * wv * wv) * (0.85 + 0.3 * bn(sp * 80.0 + tsec * 0.7));
        customEm = true;
        k2 = 0.15;
    } else if (si == 20) {  // carbon fiber: the circling light's sheen flipping between its weave's tows
        vec2 cp = vUv * vec2(1024.0, 512.0) / 14.0;
        bool warp = mod(floor(cp.x) - floor(cp.y), 4.0) < 2.0;
        vec3 h2 = normalize(key2 + v);
        float th = dot(warp ? Bn : Tn, h2);
        col += vec3(0.9, 0.92, 1.0) * pow(sqrt(max(0.0, 1.0 - th * th)), 60.0) * 0.35;
        k2 = 0.4;
    } else if (si == 29 || si == 10 || si == 11 || si == 16 || si == 17 || si == 18 || si == 19) {
        k2 = 0.2;  // (8-bit: its sprites, above; the furs move: xr_binderfx.cpp)
    } else if (si == 9) {  // snakeskin: a faint iridescence travelling over its scales
        float th = 1.0 - ndv;
        col += rainbow6(th * 1.5 + sp.x * 3.0 + tsec * 0.07) * (0.05 + 0.12 * th * th) * (0.5 + 0.5 * sin(sp.x * 40.0 - tsec * 1.2));
        k2 = 0.4;
    } else if (si == 14) {  // velvet: its sheen rolling across it
        col += alb * pow(1.0 - ndv, 2.0) * 0.4 * (0.5 + 0.5 * sin(sp.x * 20.0 - tsec * 1.2));
        k2 = 0.2;
    } else {  // the rest: a slow faint sweep where it's glossy
        col += env_of(r, U, rough) * bsweep(sp, tsec + float(si), 8.0, 0.05) * 0.12 * (1.0 - rough);
        k2 = 0.35;
    }
    col += sp2 * k2 * mix(vec3(1.0), alb, metal) * ao;
    if ((flags & 1) != 0 && si != 27) {  // holographic: a rainbow that slides as it tilts
        float h = dot(r, vec3(0.62, 0.48, 0.62)) * 1.6 + vUv.x * 2.2 + vUv.y * 1.3 + tsec * 0.03;
        col = mix(col, col * 0.4 + rainbow6(h) * (0.55 + 0.6 * metal), 0.6);
    }
    if (!customEm && shell < 0.0 && em > 0.004 && pc.look.z > 0.0) {
        float k = pc.look.z;
        if ((flags & 4) != 0) k *= 0.75 + 0.25 * sin(tsec * 1.7 + vUv.x * 9.0 + vUv.y * 5.0);
        if ((flags & 2) != 0) {
            float h = fract(sin(dot(floor(vUv * vec2(512.0, 256.0)), vec2(12.9898, 78.233))) * 43758.5453);
            k *= 0.55 + 0.45 * pow(max(0.0, sin(tsec * 2.3 + h * 40.0)), 4.0);
        }
        col += pc.glowRgb.rgb * em * k;
    }
    outColor = vec4(col * pc.look.y, pc.look.y);
}
// r163: the binder's halos, its light on the floor and its particles (glowRgb.w -7.5: xr_binderfx.cpp builds the quads in
// LOCAL space each frame). vUv.x = the sprite's kind x 2 + its own u, vUv.y its own v (0 its top); vNrm its colour (light:
// added) or, for a drop / a splat / a puff / heat shimmer, its parameters. Light is added (alpha 0); a drop, a splat and
// smoke cover what's behind them (premultiplied).
float bsmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}
void bfx_main() {
    float kind = floor(vUv.x * 0.5);
    vec2 q = vec2(vUv.x - kind * 2.0, vUv.y) * 2.0 - 1.0;
    int k = int(kind + 0.5);
    float r2 = dot(q, q), vy = vUv.y, t = pc.camPos.w;
    vec3 nr = vNrm;
    vec4 o = vec4(0.0);
    if (k == 0 || k == 8) {  // a soft dot of light (halo glows) / its pool on the floor (flatter, wider)
        float a = k == 0 ? exp(-r2 * 3.5) * max(0.0, 1.0 - r2) : max(0.0, 1.0 - r2) * max(0.0, 1.0 - r2) * exp(-r2 * 1.2);
        o = vec4(nr * a, 0.0);
    } else if (k == 1) {  // a star: a hot core and four rays
        float ax = abs(q.x), ay = abs(q.y);
        float a = exp(-r2 * 30.0) * 1.5 + (exp(-ax * 22.0) * exp(-ay * 3.0) + exp(-ay * 22.0) * exp(-ax * 3.0)) * 0.9 + exp(-r2 * 5.0) * 0.25;
        o = vec4(nr * a * max(0.0, 1.0 - r2), 0.0);
    } else if (k == 2) {  // a halo strip along an edge: soft across it, faded at its ends
        float a = exp(-q.y * q.y * 5.0) * (1.0 - q.y * q.y) * smoothstep(1.0, 0.6, abs(q.x));
        o = vec4(nr * a, 0.0);
    } else if (k == 7) {  // a streak (an ember, a shooting star): its head (v 1) bright, its tail fading behind
        float a = exp(-q.x * q.x * 7.0) * (1.0 - q.x * q.x) * pow(vy, 2.2) + exp(-(q.x * q.x * 6.0 + (vy - 0.9) * (vy - 0.9) * 120.0)) * 1.2;
        o = vec4(nr * a, 0.0);
    } else if (k == 6) {  // heat shimmer: faint wavy streaks of warm light rising off it, in patches
        float sd = floor(nr.x * 0.25) / 64.0;
        float w = 0.5 + 0.5 * sin(q.x * 8.0 + sin(vy * 9.0 + t * 3.3 + sd * 40.0) * 1.8 + sd * 17.0);
        float a = w * w * w * smoothstep(1.0, 0.5, abs(q.x)) * smoothstep(0.0, 0.6, vy) * smoothstep(1.0, 0.9, vy);
        a *= smoothstep(0.3, 0.8, bn(vec2(q.x * 3.0, vy * 4.0 + t * 1.6) + sd * 50.0));
        o = vec4(vec3(1.0, 0.45, 0.12) * a * nr.y, 0.0);
    } else if (k == 5) {  // a puff: mist (light) or smoke (a shade), lumpy, churning
        float mode = mod(nr.x, 4.0), sd = floor(nr.x * 0.25) / 64.0;
        float lump = bn(q * 2.2 + sd * 37.0 + vec2(0.0, t * 0.35)) * 0.6 + bn(q * 4.7 - sd * 11.0 - vec2(t * 0.2, 0.0)) * 0.4;
        float a = max(0.0, 1.0 - r2);
        if (mode > 0.5 && mode < 1.5) {  // (smoke: lumpier)
            lump = lump * 0.6 + bn(q * 7.0 + sd * 23.0 + vec2(0.0, t * 0.6)) * 0.4;
            a = a * a * smoothstep(0.38, 0.72, lump + 0.3 * (1.0 - r2)) * nr.y;
        } else {
            a = a * a * smoothstep(0.25, 0.75, lump + 0.35 * (1.0 - r2)) * nr.y;
        }
        if (mode > 0.5 && mode < 1.5) {  // (r163 fixes: lit smoke - grey, its underside lit by the embers below)
            float under = smoothstep(-0.2, 0.9, q.y);
            o = vec4((vec3(0.17, 0.16, 0.15) * (0.5 + 0.5 * (1.0 - under)) + vec3(1.0, 0.36, 0.08) * under * 0.9) * a, a * 0.55);
        }
        else o = vec4((mode < 0.5 ? vec3(0.62, 0.80, 1.0) : vec3(1.0, 0.5, 0.15)) * a, 0.0);
    } else if (k == 3) {
        // a drop: hanging from its edge (v 0) by a neck down to its bead, or falling (a teardrop). nr: x its stuff
        // (0 slime, 1 water, 2 lava) + its neck's width (of the bead's radius), y its length in bead radii, z its fade
        float mat = floor(nr.x), neck = nr.x - mat, A2 = max(nr.y, 1.0);
        const float M = 1.4;  // (the quad: 1.4 x the bead's width, for its edge and glow)
        float X = q.x * M, Y = vy * A2, Yc = A2 - M;
        float hw = mix(neck, neck * 0.6 + 0.14, smoothstep(0.0, max(Yc, 1e-3), Y));
        float db = length(vec2(X, Y - Yc)) - 1.0;
        float dn = Y < Yc ? max(abs(X) - hw, -Y) : 1e3;
        float d = bsmin(db, dn, 0.5);
        float cov = smoothstep(0.08, -0.08, d);
        float wb = smoothstep(Yc - 1.2, Yc - 0.4, Y);
        vec2 n2 = mix(vec2(clamp(X / max(hw, 0.05), -1.0, 1.0), 0.0), vec2(X, Y - Yc), wb);
        vec3 n3 = normalize(vec3(n2, sqrt(max(0.0, 1.0 - dot(n2, n2)))));
        vec3 L = normalize(vec3(-0.45, -0.55, 0.7));
        float df = max(dot(n3, L), 0.0), spec = pow(max(dot(n3, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0), 60.0);
        float fade = nr.z;
        if (mat < 0.5) {
            vec3 c = vec3(0.24, 0.78, 0.10) * (0.30 + 0.75 * df) + vec3(0.25, 0.6, 0.05) * pow(1.0 - n3.z, 2.0) * 0.6;
            float al = cov * (0.82 + 0.13 * n3.z) * fade;
            o = vec4(c * al + vec3(spec * cov * 0.9 * fade), al);
        } else if (mat < 1.5) {
            vec3 c = vec3(0.75, 0.88, 1.0) * (0.2 + 0.5 * pow(1.0 - n3.z, 1.5));
            float al = cov * (0.18 + 0.55 * pow(1.0 - n3.z, 2.0)) * fade;
            o = vec4(c * al + vec3(spec * cov * 1.2 * fade), al);
        } else {
            float hot = 0.6 + 0.9 * n3.z;
            o = vec4((lava_rgb(hot) * hot * 1.5 * cov + vec3(1.0, 0.3, 0.04) * exp(-max(d, 0.0) * 2.5) * 0.35 * (1.0 - cov)) * fade, 0.0);
        }
    } else if (k == 4) {
        // a splat on the floor (lying flat): an uneven puddle and a few droplets thrown round it, glossy. nr: x its stuff
        // + 4 x its seed (0..63), y how far it has spread (0..1), z its fade
        float mat = mod(nr.x, 4.0), sd = floor(nr.x * 0.25) / 64.0, spread = nr.y, fade = nr.z;
        float rr = sqrt(r2), ang = atan(q.y, q.x);
        float edge = spread * (0.74 + 0.07 * sin(ang * 3.0 + sd * 40.0) + 0.06 * sin(ang * 5.0 + sd * 70.0) + 0.04 * sin(ang * 9.0 + sd * 90.0));
        float cov = smoothstep(edge + 0.03, edge - 0.03, rr);
        for (int i = 0; i < 4; i++) {
            float an = sd * 50.0 + float(i) * 1.7;
            float dist = spread * (1.02 + 0.2 * fract(sd * 13.0 + float(i) * 0.37));
            float rd = spread * (0.05 + 0.04 * fract(sd * 7.0 + float(i) * 0.61));
            cov = max(cov, smoothstep(rd + 0.02, rd - 0.02, length(q - vec2(cos(an), sin(an)) * dist)));
        }
        float dome = sqrt(max(0.0, 1.0 - rr / max(edge, 1e-3)));
        float hl = smoothstep(0.35, 0.0, length(q / max(spread, 0.05) - vec2(-0.3, -0.25)));
        if (mat < 0.5) {
            float al = cov * 0.85 * fade;
            o = vec4(vec3(0.20, 0.70, 0.08) * (0.55 + 0.35 * dome) * al + vec3(0.9, 1.0, 0.8) * hl * cov * 0.5 * fade, al);
        } else if (mat < 1.5) {
            float al = cov * (0.12 + 0.15 * (1.0 - dome)) * fade;
            o = vec4(vec3(0.7, 0.85, 1.0) * al + vec3(hl * cov * 0.4 * fade), al);
        } else {
            o = vec4(vec3(1.0, 0.3, 0.04) * cov * (0.5 + 0.8 * dome) * fade, 0.0);
        }
    }
    outColor = o * pc.look.y;
}
void main() {
    if (pc.glowRgb.w < -5.0) {  // r160: the binder (its own mode, before anything else); r163: sleeves gone, its effects -7.5
        if (pc.glowRgb.w < -7.0) bfx_main();
        else binder_main(length(vNrm));
        return;
    }
    // r135 (Lance: "make the strikers illustrated more clear. its kinda blurry text/image wise"): STRIKERS ILLUSTRATED's
    // paper (glowRgb.w < 0) sampled half a mip level sharper: its page is minified 1.7-2.4x at a hand's length and plain
    // trilinear blurs its words to 0.64-0.80 of a perfect render's edges. tools/outside/mag_clarity_sim.py: -0.5 gives
    // the least error against the ideal at both hold distances (0.93-0.97 of its edges) and no more shimmer than 0;
    // -0.75 overshoots (aliasing) when it is held close. Everything else as before (bias 0).
    vec4 t = texture(uTex, vUv, pc.glowRgb.w < -0.5 ? -0.5 : 0.0);
    bool soft = mod(pc.look.w, 2.0) > 0.5;
    float shine = floor(pc.look.w * 0.5) / 100.0;  // r127: a shiny one's glint (0..1)
    if (!soft && t.a < 0.3) discard;
    vec3 v = normalize(pc.camPos.xyz - vPos);
    vec3 n = vNrm;
    float nl = length(n);
    n = nl > 1e-5 ? n / nl : v;
    if (dot(n, v) < 0.0) n = -n;  // both sides of a sheet (wings, ears) lit as the side you see
    vec3 key = normalize(vec3(0.35, 0.85, 0.4));
    vec3 U = vec3(0.0, 1.0, 0.0);
    if (pc.glowRgb.w < -2.75 && pc.glowRgb.w > -3.75) {
        // r142 (Lance: "make the cards more manipulable"): a special card held any way - tilted, flat, upside down, edge on
        // - its figure / room / slab / shell lit from above in the ROOM (as everything else) and from the card's front
        // side (seen from straight above or below: your side), the spotlight from above in the room; held upright, the
        // same light as before (its own up the room's)
        U = normalize(pc.up.xyz);
        vec3 F = vec3(0.0, 0.0, 1.0) - U * U.z;
        vec3 E = pc.camPos.xyz - U * dot(pc.camPos.xyz, U);
        vec3 Ed = dot(E, E) > 1e-8 ? normalize(E) : vec3(0.0, 0.0, 1.0);
        float fl = length(F);
        vec3 Fm = mix(Ed, F / max(fl, 1e-4), smoothstep(0.15, 0.5, fl));
        F = dot(Fm, Fm) > 1e-6 ? normalize(Fm) : Ed;
        key = normalize(0.35 * cross(U, F) + 0.85 * U + 0.4 * F);
    }
    float diff = max(dot(n, key), 0.0);
    // r138: a trading card's light (glowRgb.w < -3.75, xr_cards.cpp: the auras behind face-down cards, the spotlight's
    // beam, your surroundings dimmed under it): its picture as it is, premultiplied (alpha 0: light added; alpha: a
    // shade), times look.y
    if (pc.glowRgb.w < -3.75) {
        // r141: the 1/1's world (glowRgb.w < -4.25, xr_cards.cpp fx_frame): each sprite by its vertex's "normal" - its
        // colour x its fade, added (light: its picture's alpha left out); negative, a shade (smoke, shadow): its
        // picture's coverage darkening what's behind it by -normal.r
        if (pc.glowRgb.w < -4.25) {
            if (vNrm.r < 0.0) outColor = vec4(0.0, 0.0, 0.0, t.a * min(-vNrm.r, 1.0)) * pc.look.y;
            else outColor = vec4(t.rgb * vNrm, 0.0) * pc.look.y;
            return;
        }
        outColor = t * pc.look.y;
        return;
    }
    // r138: a special trading card (glowRgb.w -3 a window card, -2.9 a pop-out - no window: its figure stands on the card's
    // top edge, half of it behind the card's plane -, -3.5 obsidian glass; xr_cards.cpp), drawn in
    // its card's own frame (ModelDraw::local: vPos / camPos in metres at size 1, its front the z = 0 plane facing +z). Its
    // shell (normals 2 long: its front, back, edge, the obsidian's glass) as any card (below), a face turned away from
    // you not drawn; its inside (behind its front) only where you'd see it through its window - a portal: glowRgb.r its
    // half width, .g / .b its bottom / top as parts of the card's height from its middle (+ 0.5), look.x the card's width
    // - lit as a character (a shiny one's glint, at the card's scale), obsidian's dark violet glass deeper in; look.z the
    // spotlight on it.
    float spot = 0.0;
    if (pc.glowRgb.w < -2.75) {
        spot = pc.look.z;
        // r149: an autograph writing itself on (xr_cards.cpp special_moves: normals 3 long, their x how far it's written):
        // its picture R its glow, G its ink, B when it's written there (0..1); gold ink glowing, white-hot at the pen
        if (nl > 2.9) {
            float prog = vNrm.x;
            // r179 (Lance: "the autograph be signed in the air in like a neon or just LED fashion" - "no pen involved just a
            // floating signature"): its copy in the air (x = 10 + how far it's written): a neon tube in its ink - a hot
            // near-white core where its ink is, its colour glowing round it, a faint flicker / hum; the line simply grows,
            // no bright writing tip
            if (prog > 9.5) {
                float ap = prog - 10.0;
                if (t.b > ap || t.a < 0.01) discard;
                float rg2 = floor(vNrm.y * 256.0 + 0.5), bl2 = floor((vNrm.z - 3.0) * 256.0 + 0.5);
                vec3 inkN = rg2 + bl2 < 0.5 ? vec3(1.0, 0.74, 0.30) : vec3(floor(rg2 / 256.0), mod(rg2, 256.0), bl2) / 255.0;
                float tm = pc.camPos.w;
                float hum = 0.93 + 0.05 * sin(tm * 37.0) + 0.02 * sin(tm * 113.0) + (fract(sin(floor(tm * 9.0) * 12.9898) * 43758.5) > 0.97 ? -0.25 : 0.0);
                // (its HUE held: the ink at its brightest (its largest channel 1) - core and glow one colour, only how bright
                // differs, never past 1 - so burnt orange stays burnt orange, never washing to yellow / white)
                vec3 hueC = inkN / max(max(inkN.r, inkN.g), max(inkN.b, 0.001));
                float inten = clamp(t.g * 1.0 + t.r * 0.85, 0.0, 1.0);
                float a2 = clamp(max(t.r * 0.75, t.g), 0.0, 1.0) * pc.look.y * hum;
                outColor = vec4(hueC * inten * pc.look.y * hum, a2);
                return;
            }
            if (t.b > prog || t.a < 0.01) discard;
            float pen = smoothstep(prog - 0.05, prog, t.b) * step(prog, 1.0);
            // its ink's colour (xr_cards.cpp sign_ink): y = red + green / 256, z = 3 + blue / 256; none = gold
            float rg = floor(vNrm.y * 256.0 + 0.5), bl = floor((vNrm.z - 3.0) * 256.0 + 0.5);
            vec3 inkC = rg + bl < 0.5 ? vec3(1.0, 0.74, 0.30) : vec3(floor(rg / 256.0), mod(rg, 256.0), bl) / 255.0;
            vec3 ink = mix(inkC, mix(inkC, vec3(1.0), 0.6), t.g) + pen * mix(inkC, vec3(1.0), 0.5);
            float a = max(t.r * 0.6, t.g) * pc.look.y;
            outColor = vec4(ink * (t.r * 0.9 + t.g * 1.1) * pc.look.y, a);
            return;
        }
        if (nl > 1.5) {
            if (dot(vNrm, pc.camPos.xyz - vPos) < 0.0) discard;
        } else {
            // r141: what stands above the card's top edge (the BLACK 1/1's throne and its figure) is outside its window
            bool above = vPos.y > 0.7 * pc.look.x;
            if (vPos.z < 0.0 && pc.glowRgb.w < -2.95 && !above) {
                if (pc.camPos.z <= 0.0) discard;
                float k = pc.camPos.z / (pc.camPos.z - vPos.z);
                vec2 hit = pc.camPos.xy + (vPos.xy - pc.camPos.xy) * k;
                float cw = pc.look.x, ch = 1.4 * pc.look.x;
                if (abs(hit.x) > pc.glowRgb.r * cw || hit.y < (pc.glowRgb.g - 0.5) * ch || hit.y > (pc.glowRgb.b - 0.5) * ch)
                    discard;
            }
            // r147: the BLACK 1/1's stone: its back wall (normals a quarter long) the kaleidoscope; the character's
            // picture in it (normals half long) crisp, spotlit, a bright rim round it (its picture's edge: where its
            // neighbours a few texels off are see-through). The card's colour in up.w (0xRRGGBB as a number).
            // r149: a TIZM BOOM's layers popping out of its card (normals 0.75 long): printed pop art - flat, bright, unlit
            if (nl > 0.65 && nl < 0.85) {
                outColor = vec4(t.rgb * (1.0 + 0.15 * spot) * pc.look.y, pc.look.y);
                return;
            }
            if (pc.glowRgb.w < -3.25 && !above && nl > 0.15 && nl < 0.6) {
                float cw = pc.look.x, ch = 1.4 * pc.look.x;
                float c24 = pc.up.w;
                vec3 kc = c24 > 0.5 ? vec3(floor(c24 / 65536.0), mod(floor(c24 / 256.0), 256.0), mod(c24, 256.0)) / 255.0
                                    : vec3(0.42, 0.14, 0.86);
                kc = kc / max(max(kc.r, max(kc.g, kc.b)), 0.35);  // (its hue at a jewel's brightness)
                vec3 col;
                if (nl < 0.35) {  // (its walls: the back wall's plane where your eye's ray reaches it, 6 cm in)
                    float wb = (pc.glowRgb.g - 0.5) * ch, wt = (pc.glowRgb.b - 0.5) * ch;
                    vec2 mid = vec2(0.0, 0.5 * (wb + wt));
                    vec3 cam = pc.camPos.xyz;
                    vec3 p0 = cam + (vPos - cam) * ((-0.06 - cam.z) / min(vPos.z - cam.z, -1e-4));
                    col = kaleido(p0, cam, mid, pc.glowRgb.r * cw, 0.5 * (wt - wb), pc.camPos.w, spot, kc);
                } else {
                    float d = 3.0 / 512.0;
                    float e = min(min(texture(uTex, vUv + vec2(d, 0.0)).a, texture(uTex, vUv - vec2(d, 0.0)).a),
                                  min(texture(uTex, vUv + vec2(0.0, d)).a, texture(uTex, vUv - vec2(0.0, d)).a));
                    float rim = clamp(1.0 - e, 0.0, 1.0);
                    col = t.rgb * (1.0 + 0.2 * spot) + rim * mix(vec3(1.0, 0.95, 0.85), kc, 0.2) * (0.55 + 0.6 * spot);
                }
                outColor = vec4(col * pc.look.y, pc.look.y);
                return;
            }
            float wrapIn = max(dot(n, -key) * 0.5 + 0.5, 0.0);
            float rimIn = pow(1.0 - max(dot(n, v), 0.0), 3.0);
            vec3 colIn = t.rgb * (0.55 + 0.62 * diff + 0.12 * wrapIn) + rimIn * vec3(0.12);
            if (shine > 0.0) {  // r127's glint, at the card's scale (OUR shiny: Lance)
                float h = (vPos.y * 0.9 + 0.32 * (vPos.x + vPos.z)) * 14.0;
                float f = fract(h / 1.6 - pc.camPos.w / 1.4) - 0.5;
                float band = exp(-f * f / (2.0 * 0.018 * 0.018)) + 0.4 * exp(-f * f / (2.0 * 0.07 * 0.07));
                float hue = fract(h / 1.6 - pc.camPos.w / 1.4 + 0.5) * 6.0;
                vec3 rb = clamp(vec3(abs(hue - 3.0) - 1.0, 2.0 - abs(hue - 2.0), 2.0 - abs(hue - 4.0)), 0.0, 1.0);
                // r141 (coordinator: "our shiny's own colours must show, with the r127 glint SWEEPING over it (a band)"): on
                // a throne, a pop-out or out in a cameo (above its card / no window) the rainbow rides only the band - the
                // whole-body sheen painted a shiny Gengar into a rainbow blob; the rainbow sparkles go round it instead
                bool bandOnly = above || pc.glowRgb.w > -2.95;
                vec3 sheen = bandOnly ? band * 0.45 * rb : (0.12 + 0.25 * rimIn) * rb;
                colIn += shine * (band * vec3(1.0, 0.98, 0.92) * 1.3 + sheen);
            }
            if (pc.glowRgb.w < -3.25 && !above) {
                // obsidian (r138, coordinator: "the figure inside CRISP and clearly lit in its real colours ... slightly
                // tinted by the glass but never washed out"): the glass only cools it a touch, a little darker deeper in;
                // the spotlight lights the figure from above - its top and a warm rim - (not the slab's walls: their own
                // picture near-black), without it a faint violet rim
                float deep = clamp(-vPos.z / 0.03, 0.0, 1.0);
                // r141: the slab holds no figure now (its 1/1 stands on its throne above): its walls without the grey
                // rim a character gets (seen edge on through the window they read grey, not deep black glass)
                colIn -= rimIn * vec3(0.12);
                colIn = mix(colIn, colIn * vec3(0.86, 0.84, 1.0), 0.6) * (1.0 - 0.15 * deep);
                float lit = smoothstep(0.03, 0.12, max(t.r, max(t.g, t.b)));
                float up = max(dot(n, U), 0.0);
                colIn += spot * (t.rgb * 0.45 * up + vec3(1.0, 0.94, 0.82) * rimIn * (0.35 + 0.65 * up) * 0.7 * lit);
                colIn += vec3(0.10, 0.04, 0.20) * rimIn * lit * (1.0 - spot);
            } else {
                colIn *= 1.0 + 0.45 * spot;
                if (above) colIn += spot * vec3(1.0, 0.94, 0.82) * rimIn * 0.55;  // r141: the throne's figure, under the spotlight
            }
            float aIn = (soft ? t.a : 1.0) * pc.look.y;
            outColor = vec4(colIn * aIn, aIn);
            return;
        }
    }
    // r137: a LANCINI TIZM trading card or its pack (glowRgb.w < -1.5, xr_cards.cpp): chrome stock (paper as the
    // magazine's, with a gloss coat) and its refractor where its picture's alpha is under 1 (xr_cards.cpp: (1 - alpha) x 2
    // = how much): a prismatic rainbow that slides across it as it tilts in your hand (the light's reflection), in the
    // stock's own tint (silver a full rainbow, gold a golden one, black chrome dark with colour in it), glitter specks
    // twinkling, a white glint band sweeping over it - the parallels' prism, the pack's chrome wrapper
    if (pc.glowRgb.w < -1.5) {
        float shade = clamp(0.74 + 0.26 * max(dot(n, v), 0.0) + 0.06 * (diff - 0.5), 0.55, 1.0);
        vec3 r = reflect(-v, n);
        float gloss = pow(max(dot(r, key), 0.0), 48.0);
        // r138 (Lance: "i want a CLEAR distinction between whats base and whats a parralell"): where there's no foil (a
        // BASE card, a card's edge, the printed parts of a parallel) a satin print: a faint broad sheen, no glint at all
        vec3 col = t.rgb * shade + vec3(0.035 * pow(max(dot(r, key), 0.0), 4.0));
        float foil = t.a < 0.985 ? clamp((0.985 - t.a) * 2.0, 0.0, 1.0) : 0.0;
        if (soft && pc.glowRgb.w < -2.75) {
            // r138: obsidian's glass face (coordinator: "deep near-black glass (sharp clean reflections / a specular
            // streak, glossy, like polished obsidian)"): its picture (a light black tint, thin streaks) + a crisp highlight
            // where the light reflects, the spotlight's beam reflected as it tips back to you, its polished edges; as
            // opaque as it shines, see-through elsewhere (the figure crisp behind it); no prism
            float ndv = max(dot(n, v), 0.0);
            float s = pow(max(dot(r, key), 0.0), 160.0) * 2.5 + gloss * 0.5 + spot * pow(max(dot(r, U), 0.0), 24.0) * 1.2 +
                      pow(1.0 - ndv, 4.0) * 0.45;
            outColor = vec4((t.rgb * t.a * shade + vec3(s)) * pc.look.y, clamp(t.a + s, 0.0, 1.0) * pc.look.y);
            return;
        }
        if (foil > 0.0) {
            float tilt = dot(r, vec3(0.62, 0.48, 0.62));
            float hue = fract(tilt * 1.7 + vUv.x * 0.9 + vUv.y * 1.3 + pc.camPos.w * 0.035) * 6.0;
            vec3 rainbow = clamp(vec3(abs(hue - 3.0) - 1.0, 2.0 - abs(hue - 2.0), 2.0 - abs(hue - 4.0)), 0.0, 1.0);
            vec2 cell = floor(vUv * vec2(160.0, 224.0));
            float h = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
            float glit = step(0.94, h) * pow(max(0.0, sin(pc.camPos.w * 2.6 + h * 40.0 + tilt * 30.0)), 10.0);
            float b = fract(tilt * 1.1 + vUv.x * 0.35 - pc.camPos.w * 0.12) - 0.5;
            float band = exp(-b * b / 0.003);
            // the prism comes in bands that travel as it tilts (and slowly by themselves); a tinted stock (gold) keeps
            // most of its tint, a grey one (silver) goes the full rainbow, a dark one (black) gets colour in its shine
            float chroma = max(t.r, max(t.g, t.b)) - min(t.r, min(t.g, t.b));
            float sat = mix(1.0, 0.3, clamp(chroma * 1.6, 0.0, 1.0)) *
                        (0.25 + 0.75 * pow(0.5 + 0.5 * sin(tilt * 9.0 + (vUv.x - vUv.y) * 3.0 - pc.camPos.w * 0.4), 2.0));
            vec3 holo = (t.rgb * mix(vec3(1.0), 0.35 + 1.05 * rainbow, sat) + rainbow * 0.14 * sat) * shade +
                        vec3(glit * 0.9 + band * 0.32 + gloss * 0.3);
            if (pc.glowRgb.w < -3.25 && foil < 0.9) {
                // r138: the BLACK 1/1's obsidian shell (coordinator: "black chrome edges (no rainbow), the holographic
                // 1/1 stamp the one colourful thing" - its stamp's foil full, the rest under 0.9, xr_cards.cpp kStock):
                // a dark mirror - the room above reflected brighter as it tips back, the light's highlight, the glint band
                float sky = smoothstep(-0.1, 0.7, dot(r, U));
                holo = t.rgb * shade * 0.55 + vec3(0.05 + 0.22 * sky + gloss * 1.1 + band * 0.28 + glit * 0.25);
            }
            col = mix(col, holo, foil);
        }
        col *= 1.0 + 0.55 * spot;  // r138: a special card's spotlight (0 any other)
        float a = (soft ? t.a : 1.0) * pc.look.y;  // r138: its soft parts (obsidian's glass) by their alpha
        outColor = vec4(col * a, a);
        return;
    }
    // r131: STRIKERS ILLUSTRATED's paper (glowRgb.w < 0): the page's own colours, flat to you = as drawn, shaded only
    // as it bends away from you (and a touch by the light)
    if (pc.glowRgb.w < -0.5) {
        float shade = clamp(0.72 + 0.28 * max(dot(n, v), 0.0) + 0.06 * (diff - 0.5), 0.55, 1.0);
        outColor = vec4(t.rgb * shade * pc.look.y, pc.look.y);
        return;
    }
    float wrap = max(dot(n, -key) * 0.5 + 0.5, 0.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 lit = t.rgb * (0.48 + 0.62 * diff + 0.12 * wrap);
    lit += t.rgb * pc.glowRgb.rgb * pc.glowRgb.w * max(-n.y, 0.0);  // the theme's light from below
    lit += rim * mix(vec3(0.25), pc.glowRgb.rgb, 0.6) * (0.35 + pc.look.z);
    // the evolution's white: flat bright light, a blue-white rim
    vec3 white = vec3(1.0) * (0.9 + 0.1 * diff) + rim * vec3(0.55, 0.75, 1.0);
    vec3 col = mix(lit, white, clamp(pc.look.x, 0.0, 1.0));
    col += pc.glowRgb.rgb * pc.look.z * (0.25 + 0.75 * rim);
    // r127 (Lance: "make them jawns glisten and shimmer"): a bright glint sweeping up across it on the slant every
    // 1.4 s (a band 1.6 m apart in the room, so one at a time on it), a soft rainbow sheen drifting with it
    if (shine > 0.0) {
        float h = vPos.y * 0.9 + 0.32 * (vPos.x + vPos.z);
        float f = fract(h / 1.6 - pc.camPos.w / 1.4) - 0.5;
        float band = exp(-f * f / (2.0 * 0.018 * 0.018)) + 0.4 * exp(-f * f / (2.0 * 0.07 * 0.07));
        float hue = fract(h / 1.6 - pc.camPos.w / 1.4 + 0.5) * 6.0;
        vec3 rainbow = clamp(vec3(abs(hue - 3.0) - 1.0, 2.0 - abs(hue - 2.0), 2.0 - abs(hue - 4.0)), 0.0, 1.0);
        col += shine * (band * vec3(1.0, 0.98, 0.92) * 1.3 + (0.12 + 0.25 * rim) * rainbow);
    }
    float a = (soft ? t.a : 1.0) * pc.look.y;
    outColor = vec4(col * a, a);
}
