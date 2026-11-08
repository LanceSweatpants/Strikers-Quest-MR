// strikers-quest: goal celebration effects, the pieces' shapes as signed distances in the quad's
// -1..1 square (negative inside). Shape ids match xr_fx.cpp's Shape enum. Output is premultiplied;
// a glowing piece gives up alpha so it adds light (over passthrough too).
#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor1;
layout(location = 2) in vec4 vColor2;
layout(location = 3) in vec4 vParams;
layout(location = 4) in float vShade;
layout(location = 5) in vec4 vQuat;   // r168: the instance's quaternion (the ball skins' preview ball: its turn)
layout(location = 6) in vec3 vRight;  // r168: your eye's right and up (LOCAL)
layout(location = 7) in vec3 vUp;
// r169: the ball skins' real-fire flipbook (xr_fx.cpp make_flame: heat R, smoke G; 4 x 4 cells of 64 x 128)
layout(set = 0, binding = 1) uniform sampler2D flameTex;
// r170: the shop's preview ball's baked frames (xr_fx.cpp set_ball_atlas: 8 x 4 cells)
layout(set = 0, binding = 2) uniform sampler2D ballTex;

// r169: its colour ramp (tools/outside/balls169/flame169.py ramp(): keep in step), display colours
vec3 flameRamp(float h) {
    if (h < 0.12) return mix(vec3(0.0), vec3(0.45, 0.03, 0.0), h / 0.12);
    if (h < 0.32) return mix(vec3(0.45, 0.03, 0.0), vec3(0.95, 0.20, 0.02), (h - 0.12) / 0.20);
    if (h < 0.55) return mix(vec3(0.95, 0.20, 0.02), vec3(1.0, 0.50, 0.08), (h - 0.32) / 0.23);
    if (h < 0.78) return mix(vec3(1.0, 0.50, 0.08), vec3(1.0, 0.80, 0.32), (h - 0.55) / 0.23);
    return mix(vec3(1.0, 0.80, 0.32), vec3(1.0, 0.97, 0.86), (h - 0.78) / 0.22);
}

layout(location = 0) out vec4 outColor;

// r168: src/platform/ball_shade.h's twin (the BALL SKINS' living surfaces): keep them line for line
float bsHash(ivec3 c) {
    uvec3 u = uvec3(c);
    uint h = (u.x * 0x8da6b343u) ^ (u.y * 0xd8163841u) ^ (u.z * 0xcb1ab31fu);
    h ^= h >> 15u;
    h *= 0x2c1b3c6du;
    h ^= h >> 12u;
    h *= 0x297a2d39u;
    h ^= h >> 15u;
    return float(h >> 8u) * (1.0 / 16777216.0);
}
float bsNoise(vec3 p) {
    vec3 fl = floor(p);
    ivec3 i = ivec3(fl);
    vec3 tt = p - fl;
    vec3 u = tt * tt * (3.0 - 2.0 * tt);
    float a = mix(bsHash(i), bsHash(i + ivec3(1, 0, 0)), u.x);
    float b = mix(bsHash(i + ivec3(0, 1, 0)), bsHash(i + ivec3(1, 1, 0)), u.x);
    float c = mix(bsHash(i + ivec3(0, 0, 1)), bsHash(i + ivec3(1, 0, 1)), u.x);
    float d = mix(bsHash(i + ivec3(0, 1, 1)), bsHash(i + ivec3(1, 1, 1)), u.x);
    return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}
float bsFbm(vec3 p, int oct) {
    float sum = 0.0, amp = 0.5, nrm = 0.0;
    for (int k = 0; k < oct; k++) {
        sum += amp * bsNoise(p);
        nrm += amp;
        p = p * 2.03 + vec3(17.1, 9.2, 4.7);
        amp *= 0.5;
    }
    return sum / nrm;
}
// (r170: the preview ball samples its baked frames now - bsFire / bsEarth gone)


float dot2(vec2 v) { return dot(v, v); }

float sdCircle(vec2 p, float r) { return length(p) - r; }

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdEllipse(vec2 p, vec2 r) {
    // Good enough near the edge, which is all the anti-aliasing needs.
    float k = length(p / r);
    return (k - 1.0) * min(r.x, r.y);
}

float sdHeart(vec2 p) {
    p.x = abs(p.x);
    if (p.y + p.x > 1.0) return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}

float sdStar5(vec2 p, float r, float rf) {
    const vec2 k1 = vec2(0.809016994375, -0.587785252292);
    const vec2 k2 = vec2(-k1.x, k1.y);
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= r;
    vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

float sdHexagon(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

// r153: a triangle's signed distance (the goal explosions' mirror shards)
float sdTri(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

vec3 lin(vec3 srgb) { return pow(srgb, vec3(2.2)); }

// r82: the themed celebrations' soft looks (xr_fx.cpp kPuff..kSlash): premultiplied colour + alpha.
vec4 soft(vec3 col, float a, float glow) { return vec4(col * a, a * (1.0 - glow)); }

void main() {
    vec2 p = vUv * 2.0 - 1.0;
    int shape = int(vParams.x + 0.5);
    vec3 c1 = lin(vColor1.rgb);
    vec3 c2 = lin(vColor2.rgb);
    vec3 col = c1;
    float d = 1.0;
    float glow = vParams.w;

    float shade = 0.55 + 0.45 * vShade;
    if (shape == 0) {            // paper confetti and streamers: a different colour on each side
        d = sdBox(p, vec2(0.92, 0.92)) - 0.06;
        col = gl_FrontFacing ? c1 : c2;
    } else if (shape == 15) {    // metallic confetti: glints as it turns to the light
        d = sdBox(p, vec2(0.92, 0.92)) - 0.06;
        col = gl_FrontFacing ? c1 : c2;
        shade = 0.45 + 0.35 * vShade + 1.6 * pow(vShade, 14.0);
        glow = 0.25 * pow(vShade, 14.0);
    } else if (shape == 17) {    // marker ring at your player's feet: team colour, white rim
        float r = length(p);
        d = abs(r - 0.78) - 0.16;
        col = abs(r - 0.78) > 0.1 ? vec3(1.0) : c1;
        shade = 1.0;
    } else if (shape == 18) {    // marker arrow over your player's head, pointing down, white edge
        vec2 q = vec2(abs(p.x), -p.y);
        // A downward triangle: tip at the bottom.
        d = max(q.x * 0.866 + q.y * 0.5 - 0.45, -q.y - 0.75);
        float inner = max(q.x * 0.866 + q.y * 0.5 - 0.27, -q.y - 0.55);
        col = inner < 0.0 ? c1 : vec3(1.0);
        shade = 1.0;
    } else if (shape == 19) {    // the game's screen flash, over your whole view
        float a = vParams.z;
        outColor = vec4(c1 * a, a);
        return;
    } else if (shape == 24) {    // r60: a glowing outline along the quad's edge (the screen's, a badge's)
        vec2 ext = vec2(vParams.y, 1.0);      // the quad's half size, in half-heights
        vec2 q = p * ext;
        float t = max(vColor2.a, 0.004);      // the band's width, in half-heights
        float band = abs(sdBox(q, ext - vec2(0.5 * t))) - 0.5 * t;
        float aa = max(fwidth(band), 1e-4);
        float a = clamp(0.5 - band / aa, 0.0, 1.0);
        float inside = sdBox(q, ext - vec2(t));
        float halo = inside < 0.0 ? 0.3 * exp(inside / (1.5 * t)) : 0.0;  // a soft glow just inside it
        float alpha = max(a, halo) * vParams.z;
        outColor = vec4(c1 * alpha, alpha * (1.0 - glow));
        return;
    } else if (shape == 16) {    // the goal flash: an even wash of colour
        float a = vParams.z * (1.0 - smoothstep(0.6, 1.0, length(p)) * 0.3);
        outColor = vec4(c1 * a, a * (1.0 - glow));
        return;
    } else if (shape == 1) {     // coin
        float r = length(p);
        d = r - 0.95;
        col = r > 0.72 ? c2 : c1;
        if (abs(p.x) < 0.09 && abs(p.y) < 0.42) col = c2;
    } else if (shape == 2) {     // heart
        vec2 q = vec2(p.x, -p.y) * 1.05 + vec2(0.0, 0.55);
        d = sdHeart(q) * 0.9;
        col = mix(c1, c2, smoothstep(0.2, -0.6, p.x + p.y) * 0.35);
    } else if (shape == 3) {     // star
        d = sdStar5(p, 0.95, 0.45);
        col = mix(c1, c2, smoothstep(-0.4, -0.05, d) * 0.0 + smoothstep(0.0, 0.8, length(p)) * 0.3);
    } else if (shape == 4) {     // petal
        d = sdEllipse(p, vec2(0.42, 0.95));
        col = mix(c2, c1, smoothstep(-0.9, -0.3, p.y));
    } else if (shape == 5) {     // mushroom: cap with spots over a stem
        float cap = max(sdEllipse(p - vec2(0.0, 0.05), vec2(0.95, 0.8)), -(p.y + 0.05));
        float stem = sdBox(p - vec2(0.0, -0.45), vec2(0.38, 0.42)) - 0.08;
        d = min(cap, stem);
        if (cap < stem) {
            col = c1;
            if (sdCircle(p - vec2(0.0, 0.5), 0.22) < 0.0 || sdCircle(p - vec2(-0.55, 0.22), 0.16) < 0.0 ||
                sdCircle(p - vec2(0.55, 0.22), 0.16) < 0.0) col = c2;
        } else {
            col = vec3(0.93, 0.86, 0.72);
        }
    } else if (shape == 6) {     // egg with spots
        vec2 q = p;
        q.x *= 1.0 + 0.18 * q.y;
        d = sdEllipse(q, vec2(0.72, 0.95));
        col = c1;
        if (sdCircle(p - vec2(-0.25, 0.25), 0.2) < 0.0 || sdCircle(p - vec2(0.3, -0.2), 0.24) < 0.0 ||
            sdCircle(p - vec2(-0.15, -0.55), 0.14) < 0.0) col = c2;
    } else if (shape == 7) {     // banana: a crescent with dark tips
        float outer = sdCircle(p - vec2(0.0, 0.45), 1.05);
        float inner = sdCircle(p - vec2(0.0, 0.85), 1.05);
        d = max(max(outer, -inner), -p.y - 0.75);
        col = abs(p.x) > 0.72 ? c2 : c1;
    } else if (shape == 8) {     // shell: dome with a light rim
        d = sdEllipse(p, vec2(0.95, 0.75));
        float inner = sdEllipse(p, vec2(0.72, 0.52));
        col = inner < 0.0 ? c1 : c2;
        if (inner < 0.0 && (abs(p.x) < 0.05 || abs(p.y) < 0.05)) col = c1 * 0.6;
    } else if (shape == 9) {     // hammer: head over a handle
        float head = sdBox(p - vec2(0.0, 0.55), vec2(0.82, 0.3)) - 0.05;
        float handle = sdBox(p - vec2(0.0, -0.3), vec2(0.13, 0.62));
        d = min(head, handle);
        col = head < handle ? c2 : c1;
    } else if (shape == 10) {    // rose: bloom with a swirl on a stem
        float bloom = sdCircle(p - vec2(0.0, 0.32), 0.6);
        float stem = sdBox(p - vec2(0.0, -0.5), vec2(0.07, 0.45));
        float leaf = sdEllipse(p - vec2(0.22, -0.45), vec2(0.22, 0.1));
        d = min(bloom, min(stem, leaf));
        if (bloom <= min(stem, leaf)) {
            float a = atan(p.y - 0.32, p.x);
            float r = length(p - vec2(0.0, 0.32));
            col = c1 * (0.75 + 0.25 * sin(a * 2.0 + r * 22.0));
        } else {
            col = c2;
        }
    } else if (shape == 11) {    // garlic: a bulb with a point and cloves
        float bulb = sdEllipse(p - vec2(0.0, -0.15), vec2(0.85, 0.75));
        float tip = sdBox(p - vec2(0.0, 0.6), vec2(0.1, 0.3));
        d = min(bulb, tip);
        col = c1;
        if (bulb < 0.0 && abs(fract((p.x + 1.0) * 2.5) - 0.5) < 0.06) col = c2;
    } else if (shape == 12) {    // bow: two loops and a knot
        vec2 q = vec2(abs(p.x), p.y);
        float loopL = sdEllipse(q - vec2(0.5, 0.0), vec2(0.45, 0.6));
        float knot = sdCircle(p, 0.22);
        d = min(loopL, knot);
        col = knot < 0.0 ? c2 : c1;
    } else if (shape == 29) {    // r82: a fog puff: soft and lumpy (its own seed in colour 2's alpha), deeper in the middle
        float r2 = dot(p, p);
        float ang = atan(p.y, p.x);
        float seed = vColor2.a * 6.2831853;
        float lump = 1.0 + 0.18 * sin(ang * 3.0 + seed) + 0.12 * sin(ang * 5.0 - seed * 1.7);
        float a = exp(-r2 * 2.6 / (lump * lump)) * (1.0 - smoothstep(0.75, 1.0, r2));
        outColor = soft(mix(c2, c1, exp(-r2 * 3.0)), a * vParams.z, glow);
        return;
    } else if (shape == 30) {    // a streak along the quad (rain, a laser): a soft line, tapered ends
        float core = exp(-p.x * p.x * 9.0);
        float ends = 1.0 - smoothstep(0.55, 1.0, abs(p.y));
        vec3 sc = mix(c2, c1, exp(-p.x * p.x * 40.0));
        outColor = soft(sc * 1.3, core * ends * vParams.z, glow);
        return;
    } else if (shape == 31) {    // a lightning segment: a white-hot core in a wide glow of colour 2
        float ax = abs(p.x);
        float core = exp(-ax * ax * 60.0);
        float halo = exp(-ax * ax * 5.0) * 0.6;
        float ends = 1.0 - smoothstep(0.88, 1.0, abs(p.y));
        float a = clamp(core + halo, 0.0, 1.0) * ends * vParams.z;
        vec3 sc = (c1 * core * 2.0 + c2 * halo) / max(core + halo, 1e-3);
        outColor = soft(sc, a, glow);
        return;
    } else if (shape == 32) {    // a glowing ring (shock waves, psychic waves)
        float r = length(p);
        float band = exp(-pow((r - 0.82) / 0.06, 2.0));
        float inner = 0.25 * exp(-pow((r - 0.82) / 0.22, 2.0));
        float a = clamp(band + inner, 0.0, 1.0) * (1.0 - smoothstep(0.95, 1.0, r)) * vParams.z;
        outColor = soft(mix(c2, c1, band) * 1.3, a, glow);
        return;
    } else if (shape == 33) {    // a hypnotic spiral, turning (colour 2's alpha)
        float r = length(p);
        float s = 0.5 + 0.5 * sin(atan(p.y, p.x) * 2.0 - r * 16.0 + vColor2.a * 40.0);
        float a = smoothstep(0.35, 0.65, s) * (1.0 - smoothstep(0.75, 1.0, r)) * vParams.z;
        outColor = soft(mix(c2, c1, s), a, glow);
        return;
    } else if (shape == 34) {    // a bubble: clear, a bright rim and a glint
        float r = length(p);
        float rim = smoothstep(0.6, 0.92, r) * (1.0 - smoothstep(0.92, 1.0, r));
        float glint = exp(-dot2(p - vec2(-0.35, 0.38)) * 40.0);
        float a = clamp(rim * 0.8 + glint + 0.08 * (1.0 - smoothstep(0.9, 1.0, r)), 0.0, 1.0) * vParams.z;
        outColor = soft(mix(c1, vec3(1.0), glint), a, glow);
        return;
    } else if (shape == 35) {    // a music note
        vec2 hp = p - vec2(-0.28, -0.55);
        hp = vec2(hp.x * 0.94 + hp.y * 0.34, -hp.x * 0.34 + hp.y * 0.94);
        float head = sdEllipse(hp, vec2(0.32, 0.22));
        float stem = sdBox(p - vec2(0.02, 0.1), vec2(0.06, 0.68));
        vec2 fp = p - vec2(0.28, 0.6);
        fp = vec2(fp.x * 0.8 + fp.y * 0.6, -fp.x * 0.6 + fp.y * 0.8);
        float flag = sdBox(fp, vec2(0.28, 0.08));
        d = min(head, min(stem, flag));
        col = c1;
        shade = 1.0;
    } else if (shape == 36) {    // a "Z"
        float top = sdBox(p - vec2(0.0, 0.7), vec2(0.7, 0.13));
        float bot = sdBox(p - vec2(0.0, -0.7), vec2(0.7, 0.13));
        vec2 q = vec2(p.x * 0.6 + p.y * 0.8, -p.x * 0.8 + p.y * 0.6);
        float diag = sdBox(q, vec2(0.13, 0.85));
        d = min(min(top, bot), diag);
        col = c1;
        shade = 1.0;
    } else if (shape == 37) {    // a glowing hexagon (a reflector, a crystal)
        float h = sdHexagon(p, 0.8);
        float band = exp(-pow(h / 0.06, 2.0));
        float a = clamp(band + (h < 0.0 ? 0.15 : 0.0), 0.0, 1.0) * vParams.z;
        outColor = soft(mix(c2, c1, band) * 1.3, a, glow);
        return;
    } else if (shape == 38) {    // a ghost's face in the fog: two glowing eyes and a grin
        float eyes = min(sdEllipse(p - vec2(-0.33, 0.25), vec2(0.17, 0.11)), sdEllipse(p - vec2(0.33, 0.25), vec2(0.17, 0.11)));
        float grin = max(max(sdCircle(p - vec2(0.0, 0.15), 0.62), -sdCircle(p - vec2(0.0, 0.42), 0.68)), -p.y - 0.75);
        float dd = min(eyes, grin);
        float aa2 = max(fwidth(dd), 1e-4);
        float inside = clamp(0.5 - dd / aa2, 0.0, 1.0);
        float halo = exp(-max(dd, 0.0) * 12.0) * 0.5;
        outColor = soft(mix(c2, c1, inside) * 1.4, clamp(inside + halo, 0.0, 1.0) * vParams.z, glow);
        return;
    } else if (shape == 39) {    // a snowflake: six arms with twigs
        float r = length(p);
        float sector = mod(atan(p.y, p.x) + 0.5236, 1.0472) - 0.5236;
        vec2 q = vec2(cos(sector), sin(sector)) * r;
        float arm = sdBox(q - vec2(0.45, 0.0), vec2(0.45, 0.05));
        vec2 t = vec2(q.x - 0.55, abs(q.y));
        t = vec2(t.x * 0.7071 + t.y * 0.7071, -t.x * 0.7071 + t.y * 0.7071);
        float twig = sdBox(t - vec2(0.12, 0.0), vec2(0.14, 0.04));
        d = min(min(arm, twig), sdCircle(p, 0.12));
        col = c1;
        shade = 1.0;
    } else if (shape == 70) {    // a drop (water, mud, lava): round below, pointed above, a highlight
        vec2 q = vec2(p.x, p.y + 0.2);
        float ball = sdCircle(q, 0.55);
        float tip = max(abs(q.x) * 0.94 + q.y * 0.34 - 0.52, -q.y);
        d = min(ball, tip);
        col = mix(c2, c1, smoothstep(-0.6, 0.4, p.y));
        if (sdCircle(p - vec2(-0.18, -0.05), 0.12) < 0.0) col = mix(col, vec3(1.0), 0.7);
        shade = 1.0;
    } else if (shape == 71) {    // Mr. Game & Watch's LCD shapes, flat black (variant in colour 2's alpha)
        int v = int(vColor2.a * 3.99);
        if (v == 0) {            // an "8"
            float hs = min(min(sdBox(p - vec2(0.0, 0.8), vec2(0.42, 0.1)), sdBox(p, vec2(0.42, 0.1))), sdBox(p + vec2(0.0, 0.8), vec2(0.42, 0.1)));
            vec2 a = vec2(abs(p.x) - 0.5, abs(p.y) - 0.4);
            d = min(hs, sdBox(a, vec2(0.1, 0.32)));
        } else if (v == 1) {     // a parachute
            float dome = max(sdCircle(p - vec2(0.0, 0.1), 0.75), -(p.y - 0.1));
            float man = sdBox(p - vec2(0.0, -0.7), vec2(0.14, 0.2));
            vec2 sl = vec2(abs(p.x) - 0.35 + (p.y - 0.1) * 0.45, p.y + 0.3);
            float lines = sdBox(sl, vec2(0.03, 0.42));
            d = min(min(dome, man), lines);
        } else if (v == 2) {     // a ball (Juggle)
            d = sdCircle(p, 0.55);
        } else {                 // the judge's hammer
            float head = sdBox(p - vec2(0.0, 0.5), vec2(0.6, 0.25));
            float handle = sdBox(p - vec2(0.0, -0.25), vec2(0.1, 0.6));
            d = min(head, handle);
        }
        col = c1;
        shade = 1.0;
    } else if (shape == 72) {    // a checkered flag
        d = sdBox(p, vec2(0.92, 0.66));
        vec2 g = floor((p + 1.0) * 3.0);
        col = mod(g.x + g.y, 2.0) < 0.5 ? vec3(0.95) : vec3(0.03);
    } else if (shape == 73) {    // a wisp: a flame of light, a hot core
        float w = 0.5 * (1.0 - smoothstep(-0.6, 0.95, p.y));
        float a = exp(-p.x * p.x / (w * w + 1e-3) * 1.8) * smoothstep(-1.0, -0.45, p.y) * (1.0 - smoothstep(0.7, 1.0, p.y));
        float core = exp(-dot2(p - vec2(0.0, -0.35)) * 10.0);
        outColor = soft(mix(c2, c1, core) * 1.5, clamp(a, 0.0, 1.0) * vParams.z, glow);
        return;
    } else if (shape == 74) {    // a slash: a thin bright crescent
        float outer = sdCircle(p - vec2(0.0, -0.35), 1.0);
        float inner = sdCircle(p - vec2(0.0, -0.55), 1.0);
        float cres = max(max(outer, -inner), abs(p.x) - 0.95);
        float a = exp(-max(cres, 0.0) * 25.0) * (cres < 0.0 ? 1.0 : 0.7) * (1.0 - smoothstep(0.7, 0.95, abs(p.x)));
        outColor = soft(mix(c2, c1, exp(-max(-cres, 0.0) * 20.0)) * 1.4, clamp(a, 0.0, 1.0) * vParams.z, glow);
        return;
    } else if (shape == 75) {    // r89 (Lance: Pichu's "kebobs ... need to be lightning"): a lightning bolt,
        // the zigzag: a yellow body with a white-hot middle in a glow of colour 2
        const vec2 v[7] = vec2[7](vec2(0.10, 1.00), vec2(0.58, 1.00), vec2(0.16, 0.22), vec2(0.54, 0.22),
                                  vec2(-0.24, -1.00), vec2(-0.02, -0.12), vec2(-0.42, -0.12));
        float dd = dot2(p - v[0]);
        float sgn = 1.0;
        for (int i = 0, j = 6; i < 7; j = i, i++) {
            vec2 e = v[j] - v[i];
            vec2 w = p - v[i];
            vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
            dd = min(dd, dot(b, b));
            bvec3 c = bvec3(p.y >= v[i].y, p.y < v[j].y, e.x * w.y > e.y * w.x);
            if (all(c) || all(not(c))) sgn = -sgn;
        }
        float sd = sgn * sqrt(dd);
        float aa = max(fwidth(sd), 1e-4);
        float body = clamp(0.5 - sd / aa, 0.0, 1.0);
        float halo = exp(-max(sd, 0.0) * 9.0) * 0.55 * (1.0 - body);
        vec3 bc = mix(c1, vec3(1.0), smoothstep(-0.05, -0.16, sd));
        float a = clamp(body + halo, 0.0, 1.0) * vParams.z;
        vec3 sc = (bc * body * 1.5 + c2 * halo) / max(body + halo, 1e-3);
        outColor = soft(sc, a, glow);
        return;
    } else if (shape == 76) {    // r89 (Lance: Charmander's "generic stars ... needs to be like flames"): a
        // flame: a round base with tongues licking up (their sway from colour 2's alpha), white-yellow
        // heart, colour 1 (yellow-orange) through to colour 2 (red) at the tips
        float seed = vColor2.a * 6.2831853;
        float body = sdCircle(p - vec2(0.0, -0.42), 0.46);
        float d2 = body;
        // the main tongue and two smaller ones, each narrowing to a point, swaying a little
        for (int k = 0; k < 3; k++) {
            float x0 = k == 0 ? 0.0 : (k == 1 ? -0.26 : 0.28);
            float top = k == 0 ? 0.96 : (k == 1 ? 0.42 : 0.30);
            float w0 = k == 0 ? 0.42 : 0.2;
            float y0 = k == 0 ? -0.42 : -0.15;
            float t = clamp((p.y - y0) / (top - y0), 0.0, 1.0);
            float cx = x0 + (0.14 + 0.05 * float(k)) * sin(t * 3.4 + seed + float(k) * 2.1) * t;
            float wdt = w0 * (1.0 - t) * (1.0 - 0.35 * t);
            float tongue = max(abs(p.x - cx) - wdt, max(y0 - p.y, p.y - top));
            float hcap = 0.08;
            float hh = clamp(0.5 + 0.5 * (tongue - d2) / hcap, 0.0, 1.0);  // smooth union
            d2 = mix(tongue, d2, hh) - hcap * hh * (1.0 - hh);
        }
        float aa = max(fwidth(d2), 1e-4);
        float a = clamp(0.5 - d2 / aa, 0.0, 1.0);
        float heat = clamp(1.0 - length((p - vec2(0.0, -0.45)) * vec2(1.6, 0.9)), 0.0, 1.0);
        vec3 fc = mix(c2, c1, smoothstep(0.0, 0.55, heat + 0.25 * (-p.y)));
        fc = mix(fc, vec3(1.0, 0.97, 0.82), smoothstep(0.55, 0.9, heat));
        float edge = exp(-max(d2, 0.0) * 14.0) * 0.35 * (1.0 - a);
        float alpha = clamp(a + edge, 0.0, 1.0) * vParams.z;
        outColor = soft((fc * a * 1.4 + c2 * edge) / max(a + edge, 1e-3), alpha, glow);
        return;
    } else if (shape == 77) {    // r96: a shiny's twinkle (the games' sparkle): a four-point star (an astroid,
        // long thin points), a white core going to colour 1 at its points, in a soft glow of colour 2
        vec2 q = abs(p) / 0.96;
        float s = sqrt(q.x) + sqrt(q.y);
        float star = clamp((1.0 - s) / 0.2, 0.0, 1.0);
        float core = clamp((0.6 - s) / 0.35, 0.0, 1.0);
        float halo = 0.5 * exp(-dot(p, p) * 7.0);
        float a = max(star, halo);
        vec3 sc = mix(c2, mix(c1, vec3(1.0), core), clamp(star * 1.5, 0.0, 1.0));
        outColor = soft(sc * 1.3, a * vParams.z, glow);
        return;
    } else if (shape >= 140 && shape <= 149) {
        // r153: the goal explosions' own (xr_goalfx.cpp)
        if (shape == 140) {          // a cartoon bomb: a round dark body (colour 1) flashing colour 2 (colour 2's alpha:
            // how lit), its shine, a fuse cap, a lit fuse
            float lit = vColor2.a;
            vec2 q = p - vec2(0.0, -0.1);
            float body = length(q) - 0.74;
            float cap = sdBox(p - vec2(0.0, 0.68), vec2(0.17, 0.1)) - 0.03;
            vec2 f0 = vec2(0.0, 0.76), f1 = vec2(0.3, 0.96);
            vec2 fa = p - f0, fb = f1 - f0;
            float fuse = length(fa - fb * clamp(dot(fa, fb) / dot(fb, fb), 0.0, 1.0)) - 0.045;
            float dd = min(min(body, cap), fuse);
            float aa = max(fwidth(dd), 1e-4);
            float a = clamp(0.5 - dd / aa, 0.0, 1.0);
            vec3 n = normalize(vec3(q, sqrt(max(0.0, 0.5476 - dot(q, q)))));
            float l = clamp(0.45 + 0.75 * dot(n, normalize(vec3(-0.5, 0.6, 0.6))), 0.12, 1.25);
            vec3 col2 = mix(c1, c2, lit * 0.85) * l + c2 * lit * 0.25;
            if (sdEllipse(q - vec2(-0.3, 0.32), vec2(0.17, 0.11)) < 0.0) col2 = mix(col2, vec3(1.0), 0.85);
            if (body > -0.05) col2 *= 0.55;  // its outline
            if (cap < body && cap < fuse) col2 = vec3(0.55, 0.56, 0.62) * (0.8 + 0.2 * p.y);
            if (fuse < cap && fuse < body) col2 = vec3(0.86, 0.74, 0.5);
            float fl = exp(-dot2(p - f1) * 30.0);
            vec3 flame = mix(vec3(1.0, 0.45, 0.1), vec3(1.0, 0.95, 0.7), fl);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha + flame * fl * 1.6 * vParams.z, alpha);
            return;
        } else if (shape == 141) {   // a smoke ring: soft, lumpy, puffy (its own seed in colour 2's alpha), lit on top
            float r = length(p);
            float ang = atan(p.y, p.x);
            float seed = vColor2.a * 6.2831853;
            float th = 0.17 + 0.05 * sin(ang * 5.0 + seed) + 0.035 * sin(ang * 11.0 - seed * 1.7);
            float dd = (r - 0.66) / th;
            float a = exp(-dd * dd * 1.6) * (1.0 - smoothstep(0.93, 1.0, r));
            vec3 sc = mix(c2, c1, clamp(0.5 + 0.5 * p.y - 0.4 * dd, 0.0, 1.0));
            outColor = soft(sc, a * vParams.z, glow);
            return;
        } else if (shape == 142) {   // a star portal: two glowing spiral arms turning (colour 2's alpha), a white heart, stars
            float r = length(p);
            float ang = atan(p.y, p.x) + vColor2.a * 6.2831853;
            float arms = 0.5 + 0.5 * cos(2.0 * ang - log(r + 0.03) * 5.5);
            float arm = arms * arms * arms * smoothstep(1.0, 0.18, r);
            float core = exp(-r * r * 22.0);
            vec2 cell = floor(p * 14.0);
            vec2 f = fract(p * 14.0) - 0.5;
            float h = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
            float star = h > 0.86 ? smoothstep(0.32, 0.0, length(f)) * (0.6 + 0.4 * sin(h * 50.0 + vColor2.a * 30.0)) : 0.0;
            float edge = 1.0 - smoothstep(0.82, 1.0, r);
            float a = clamp(arm * 0.9 + core + star * 0.9 + 0.12 * smoothstep(1.0, 0.3, r), 0.0, 1.0) * edge * vParams.z;
            vec3 sc = mix(c2, c1, arm) * (0.6 + arm) + vec3(1.0) * (core * 1.6 + star);
            outColor = soft(sc, a, glow);
            return;
        } else if (shape == 143) {   // a giant chomper plant's head from the front (colour 1, its spots and lips colour 2),
            // its mouth open by colour 2's alpha: teeth top and bottom, dark inside
            float open = vColor2.a;
            float head = sdEllipse(p, vec2(0.92, 0.9));
            float mh = 0.04 + 0.42 * open;
            vec2 mq = p - vec2(0.0, -0.08);
            float mouth = sdEllipse(mq, vec2(0.7, mh));
            float aa = max(fwidth(head), 1e-4);
            float a = clamp(0.5 - head / aa, 0.0, 1.0);
            vec3 col2 = c1 * (0.72 + 0.5 * clamp(0.7 - length(p - vec2(-0.3, 0.35)), 0.0, 1.0));
            bool spot = length(p - vec2(-0.45, 0.42)) < 0.16 || length(p - vec2(0.05, 0.64)) < 0.14 ||
                        length(p - vec2(0.47, 0.4)) < 0.15 || length(p - vec2(-0.12, 0.34)) < 0.08;
            if (spot && mq.y > mh + 0.1) col2 = c2;
            if (head > -0.05) col2 *= 0.6;  // its outline
            if (mouth < 0.0) {
                col2 = mix(vec3(0.22, 0.02, 0.05), vec3(0.7, 0.16, 0.26), clamp(-mq.y / max(mh, 0.01), 0.0, 1.0) * 0.7);
                float e = mh * sqrt(max(0.0, 1.0 - (mq.x / 0.7) * (mq.x / 0.7)));
                float tx = fract((mq.x + 1.0) * 4.0) - 0.5;
                float tooth = min(0.13, e * 0.9) * (1.0 - abs(tx) * 2.0);
                if (mq.y > e - tooth - 0.015 || mq.y < -e + tooth + 0.015) col2 = vec3(0.98, 0.97, 0.9);
            }
            if (abs(mouth) < 0.055) col2 = c2 * 0.95;  // its lips
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 144) {   // a marble column (colour 1 its marble, colour 2 its gold), upright: abacus, echinus,
            // a fluted round shaft, a base; a soft gold glow round it
            float w = vParams.y;              // its half width, in half heights (the quad is w wide, 1 tall)
            vec2 q = vec2(p.x * w, p.y);
            float abacus = sdBox(q - vec2(0.0, 1.0 - 0.14 * w), vec2(w * 0.98, 0.14 * w));
            float swell = clamp((q.y - (1.0 - 0.48 * w)) / (0.24 * w), 0.0, 1.0);
            float echinus = sdBox(q - vec2(0.0, 1.0 - 0.38 * w), vec2(w * (0.7 + 0.24 * swell), 0.11 * w));
            float plinth = sdBox(q - vec2(0.0, -1.0 + 0.13 * w), vec2(w * 0.98, 0.13 * w));
            float torus = sdBox(q - vec2(0.0, -1.0 + 0.34 * w), vec2(w * 0.8, 0.07 * w)) - 0.04 * w;
            float shaftW = w * (0.66 - 0.05 * (q.y + 1.0));
            float shaft = sdBox(q, vec2(shaftW, 1.0 - 0.45 * w));
            float parts = min(min(abacus, echinus), min(plinth, torus));
            float dd = min(parts, shaft);
            float aa = max(fwidth(dd), 1e-5);
            float a = clamp(0.5 - dd / aa, 0.0, 1.0);
            float u = clamp(q.x / max(shaftW, 1e-3), -1.0, 1.0);
            float roundL = 0.62 + 0.42 * sqrt(max(0.0, 1.0 - u * u)) - 0.18 * u;
            float flute = 0.86 + 0.14 * cos(u * 3.14159 * 4.5);
            vec3 col2 = c1 * roundL * flute;
            if (shaft > parts) col2 = c1 * (0.8 + 0.2 * clamp(1.0 - abs(q.x) / w, 0.0, 1.0)) * (q.y > 0.0 ? 1.0 : 0.85);
            if (abs(q.y - (1.0 - 0.52 * w)) < 0.05 * w || abs(q.y - (-1.0 + 0.47 * w)) < 0.05 * w) col2 = c2;
            float halo = exp(-max(dd, 0.0) * 5.0 / max(w, 0.05)) * 0.3 * (1.0 - a);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha + c2 * halo * vParams.z, alpha);
            return;
        } else if (shape == 145) {   // a LANCINI TIZM card: its face (a refractor's rainbow over colour 1, a star, its name
            // bar) and its back (navy, a gold lattice, an oval); its edge in colour 2 (chrome, silver, gold, black)
            float asp = vParams.y;
            vec2 q = vec2(p.x * asp, p.y);
            float card = sdBox(q, vec2(asp * 0.95, 0.95) - 0.07) - 0.07;
            float aa = max(fwidth(card), 1e-4);
            float a = clamp(0.5 - card / aa, 0.0, 1.0);
            float inner = sdBox(q, vec2(asp * 0.95 - 0.1, 0.85));
            vec3 col2;
            if (gl_FrontFacing) {
                float hue = fract((q.x + q.y) * 0.6 + vColor2.a * 3.0 + vShade * 0.5);
                vec3 rb = clamp(abs(fract(hue + vec3(0.0, 0.667, 0.333)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
                col2 = mix(c1, rb, 0.35) * (0.75 + 0.25 * vShade);
                if (sdStar5(q - vec2(0.0, 0.1), 0.42, 0.45) < 0.0) col2 = mix(vec3(1.0), c1, 0.25);
                if (q.y < -0.55 && abs(q.x) < asp * 0.7) col2 = vec3(0.07, 0.08, 0.12);
            } else {
                col2 = vec3(0.05, 0.08, 0.22);
                vec2 dq = q * 7.0;
                float lat = (abs(fract(dq.x + dq.y) - 0.5) < 0.06 || abs(fract(dq.x - dq.y) - 0.5) < 0.06) ? 1.0 : 0.0;
                col2 = mix(col2, vec3(0.85, 0.66, 0.24), lat * 0.55);
                if (sdEllipse(q, vec2(asp * 0.55, 0.38)) < 0.0)
                    col2 = sdEllipse(q, vec2(asp * 0.5, 0.33)) < 0.0 ? vec3(0.05, 0.08, 0.22) : vec3(0.95, 0.78, 0.3);
            }
            if (inner > 0.0) col2 = c2 * (0.7 + 0.5 * vShade);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 146) {   // a mirror shard (one of four outlines, colour 2's alpha): a pale mirror from colour 2 to
            // colour 1, a glint sweeping as it turns, a bright edge
            int v = int(vColor2.a * 3.99);
            vec2 a0 = vec2(-0.9, -0.8), a1 = vec2(0.95, -0.55), a2 = vec2(-0.1, 0.95);
            if (v == 1) { a0 = vec2(-0.95, 0.3); a1 = vec2(0.8, -0.9); a2 = vec2(0.6, 0.95); }
            else if (v == 2) { a0 = vec2(-0.6, -0.95); a1 = vec2(0.9, 0.1); a2 = vec2(-0.85, 0.85); }
            else if (v == 3) { a0 = vec2(-0.95, -0.2); a1 = vec2(0.3, -0.95); a2 = vec2(0.85, 0.9); }
            float dd = sdTri(p, a0, a1, a2);
            float aa = max(fwidth(dd), 1e-4);
            float a = clamp(0.5 - dd / aa, 0.0, 1.0);
            float gl = (p.x + p.y) * 0.7 - (vShade * 2.0 - 1.0) * 0.9;
            float glint = exp(-gl * gl * 18.0);
            vec3 col2 = mix(c2, c1, 0.5 + 0.5 * p.y) * (0.55 + 0.45 * vShade) + vec3(glint * 0.8);
            float edge = exp(min(dd, 0.0) * 14.0) * a;
            col2 = mix(col2, c2 * 1.4 + 0.3, edge * 0.6);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 147) {   // 8-bit: a block (colour 2's alpha < 0.3), a pixel burst (< 0.7) or a pixel ring
            float v = vColor2.a;
            if (v < 0.3) {
                float dd = sdBox(p, vec2(0.86));
                float aa = max(fwidth(dd), 1e-4);
                float a = clamp(0.5 - dd / aa, 0.0, 1.0);
                vec3 col2 = c1;
                if (p.x < -0.6 || p.y > 0.6) col2 = mix(c1, vec3(1.0), 0.45);
                if (p.x > 0.6 || p.y < -0.6) col2 = c1 * 0.45;
                if (abs(p.x) > 0.6 && abs(p.y) > 0.6) col2 = c1 * 0.75;
                float alpha = a * vParams.z;
                outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
                return;
            }
            vec2 g = (floor((p * 0.5 + 0.5) * 16.0) + 0.5) / 16.0 * 2.0 - 1.0;  // each pixel's middle
            float r = length(g);
            float ang = atan(g.y, g.x);
            float fill;
            vec3 col2;
            if (v < 0.7) {
                float spikes = 0.52 + 0.4 * pow(abs(cos(ang * 4.0)), 5.0);  // an eight-point burst
                fill = r < spikes ? 1.0 : 0.0;
                col2 = r < spikes * 0.5 ? vec3(1.0, 1.0, 0.85) : (r < spikes * 0.78 ? c2 : c1);
            } else {
                fill = (r > 0.66 && r < 0.95) ? 1.0 : 0.0;
                col2 = r > 0.81 ? c1 : c2;
            }
            float alpha = fill * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 148) {   // a shockwave: a bright thin front (colour 1), a soft trail inside it (colour 2)
            float r = length(p);
            float fr = (r - 0.93) / 0.025;
            float front = exp(-fr * fr);
            float trail = smoothstep(0.35, 0.93, r) * (1.0 - smoothstep(0.93, 0.96, r)) * 0.28;
            float a = clamp(front + trail, 0.0, 1.0) * vParams.z;
            vec3 sc = mix(c2, c1, clamp(front * 1.2, 0.0, 1.0)) * 1.3;
            outColor = soft(sc, a, glow);
            return;
        } else {                     // 149: a plant's stem piece: a round green tube (colour 1 lit, colour 2 its shadow)
            float u = p.x;
            float dd = abs(u) - 0.95;
            float aa = max(fwidth(dd), 1e-4);
            float a = clamp(0.5 - dd / aa, 0.0, 1.0);
            float l = 0.6 + 0.4 * sqrt(max(0.0, 1.0 - u * u)) - 0.25 * u;
            vec3 col2 = mix(c2, c1, clamp(l, 0.0, 1.0)) * (0.85 + 0.25 * l);
            if (abs(u + 0.35) < 0.08) col2 = mix(col2, vec3(0.75, 1.0, 0.6), 0.5);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        }
    } else if (shape >= 150 && shape <= 158) {
        // r161: the room takeovers' own (xr_goalfx.cpp, GOAL EXPLOSIONS pack 2)
        if (shape == 150) {          // an 8-bit "?" block: colour 1 its face, colour 2 its outline and rivets; colour 2's
            // alpha over a half: spent (brown, no "?")
            vec2 g = clamp(floor((p * 0.5 + 0.5) * 16.0), 0.0, 15.0);
            bool spent = vColor2.a > 0.5;
            vec3 face = spent ? vec3(0.42, 0.24, 0.12) : c1;
            vec3 col2 = face;
            bool edge = g.x < 1.0 || g.x > 14.0 || g.y < 1.0 || g.y > 14.0;
            if (!edge && (g.y > 13.0 || g.x < 2.0)) col2 = mix(face, vec3(1.0), 0.35);
            if (!edge && (g.y < 2.0 || g.x > 13.0)) col2 = face * 0.6;
            if ((g.x == 2.0 || g.x == 13.0) && (g.y == 2.0 || g.y == 13.0)) col2 = c2;
            if (edge) col2 = c2 * 0.6;
            if (!spent) {
                const int kQ[10] = int[10](60, 102, 102, 6, 12, 24, 24, 0, 24, 24);
                int row = int(12.0 - g.y), colx = int(g.x - 4.0);
                int srow = row - 1, scol = colx - 1;  // (its shadow: a pixel down and right)
                bool on = row >= 0 && row < 10 && colx >= 0 && colx < 8 && ((kQ[row] >> (7 - colx)) & 1) == 1;
                bool sh = srow >= 0 && srow < 10 && scol >= 0 && scol < 8 && ((kQ[srow] >> (7 - scol)) & 1) == 1;
                if (sh) col2 = c2 * 0.7;
                if (on) col2 = vec3(1.0, 0.98, 0.9);
            }
            float dd = sdBox(p, vec2(0.97));
            float a = clamp(0.5 - dd / max(fwidth(dd), 1e-4), 0.0, 1.0) * vParams.z;
            outColor = vec4(col2 * a, a * (1.0 - glow));
            return;
        } else if (shape == 151) {   // a pixel alien (colour 1, its eyes colour 2; our own sprites): which in colour 2's
            // alpha - 0-5 three aliens x two frames, 6 the saucer (its quad twice as wide), 7 the saucer flashing white
            const int kInvBody[56] = int[56](112, 508, 1022, 1022, 1022, 340, 594, 1105, 112, 508, 1022, 1022, 1022, 340, 340, 594,
                1025, 626, 508, 1022, 2047, 1022, 508, 650, 514, 626, 508, 1022, 2047, 1022, 508, 1285, 32, 248, 508, 508, 508, 168,
                396, 514, 32, 248, 508, 508, 508, 168, 216, 260, 2016, 8184, 16380, 32766, 65535, 14748, 4104, 0);
            const int kInvEye[56] = int[56](0, 0, 0, 136, 0, 0, 0, 0, 0, 0, 0, 136, 0, 0, 0, 0, 0, 0, 0, 112, 112, 0, 0, 0, 0, 0,
                0, 112, 112, 0, 0, 0, 0, 0, 0, 136, 0, 0, 0, 0, 0, 0, 0, 136, 0, 0, 0, 0, 0, 0, 0, 4680, 0, 0, 0, 0);
            int v = int(vColor2.a * 7.99);
            bool flash = v == 7;
            if (flash) v = 6;
            int w = v == 6 ? 16 : 11;
            int gx, gy;
            if (v == 6) {
                gx = int(floor((p.x * 0.5 + 0.5) * 16.0));
                gy = int(floor((0.5 - p.y * 0.5) * 8.0));
            } else {
                gx = int(floor((p.x * 0.5 + 0.5) * 13.0)) - 1;
                gy = int(floor((0.5 - p.y * 0.5) * 13.0)) - 2;
            }
            float a = 0.0;
            vec3 col2 = c1;
            if (gx >= 0 && gx < w && gy >= 0 && gy < 8) {
                int bit = (w - 1) - gx;
                if (((kInvBody[v * 8 + gy] >> bit) & 1) == 1) {
                    a = 1.0;
                    col2 = ((kInvEye[v * 8 + gy] >> bit) & 1) == 1 ? c2 : c1 * (0.85 + 0.3 * (1.0 - float(gy) / 8.0));
                }
            }
            if (flash && a > 0.0) col2 = vec3(1.0);
            float halo = exp(-dot(p, p) * 2.2) * 0.22 * (1.0 - a);  // (a soft glow round it: it reads over your room)
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha + c1 * halo * vParams.z, alpha * (1.0 - glow));
            return;
        } else if (shape == 152) {   // a shark's fin side on (colour 1, its lit edge colour 2), its nose toward +x (colour
            // 2's alpha over a half: mirrored), leaning back, foam where it cuts the water
            vec2 q = vec2(vColor2.a > 0.5 ? -p.x : p.x, p.y);
            float t = clamp((q.y + 0.9) / 1.85, 0.0, 1.0);
            float xl = mix(0.8, -0.25, t) + 0.2 * sin(3.14159 * t);
            float xt = mix(-0.85, -0.25, t) + 0.25 * sin(3.14159 * t);
            float dd = max(max(q.x - xl, xt - q.x), max(-0.9 - q.y, q.y - 0.95));
            float a = clamp(0.5 - dd / max(fwidth(dd), 1e-4), 0.0, 1.0);
            vec3 col2 = c1 * (0.75 + 0.35 * t);
            col2 = mix(col2, c2, exp(-max(xl - q.x, 0.0) * 12.0) * 0.7);
            col2 = mix(col2, vec3(1.0), smoothstep(-0.62, -0.88, q.y) * 0.8);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 153) {   // a dance-floor tile lit from under (colour 1, brighter at its heart, a frosted grid,
            // a rim of colour 2); how lit: its alpha
            float dd = sdBox(p, vec2(0.86)) - 0.08;
            float a = clamp(0.5 - dd / max(fwidth(dd), 1e-4), 0.0, 1.0);
            float heart = 1.0 - 0.45 * length(p);
            vec2 gq = abs(fract(p * 1.5 + 0.5) - 0.5);
            float grid = smoothstep(0.03, 0.0, min(gq.x, gq.y)) * 0.25;
            float rim = exp(dd * 10.0) * a;
            vec3 col2 = c1 * (0.55 + 0.6 * heart) * (1.0 - grid) + c2 * rim * 0.5;
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 154) {   // a mirror ball (colour 1 its glass, colour 2 its shade), turning (colour 2's alpha):
            // facets, dark seams, glints, a highlight from above
            float r = length(p);
            float dd = r - 0.95;
            float a = clamp(0.5 - dd / max(fwidth(dd), 1e-4), 0.0, 1.0);
            vec3 n = vec3(p, sqrt(max(0.0, 1.0 - min(r * r, 1.0))));
            float lon = atan(n.x, n.z) + vColor2.a * 6.2831853;
            float lat = asin(clamp(n.y, -1.0, 1.0));
            vec2 cell = floor(vec2(lon, lat) * 4.4);
            vec2 f = fract(vec2(lon, lat) * 4.4);
            float h = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
            float grout = step(0.9, max(f.x, f.y));
            float spec = pow(max(0.0, dot(normalize(n), normalize(vec3(-0.4, 0.7, 0.6)))), 18.0);
            vec3 col2 = mix(c2, c1, 0.35 + 0.65 * h) * (0.6 + 0.4 * n.z) * (1.0 - 0.7 * grout) + vec3(spec * 1.4);
            if (h > 0.93) col2 += vec3(0.9);
            float alpha = a * vParams.z;
            outColor = vec4(col2 * alpha, alpha * (1.0 - glow));
            return;
        } else if (shape == 155) {   // water seen from above (colour 1 its light, colour 2 its depth): waves and caustics
            // moving (colour 2's alpha: its clock), soft at its edge, a ring of foam
            float r = length(p);
            float t = vColor2.a * 6.2831853;
            float w1 = sin(dot(p, vec2(11.0, 6.0)) + t) * sin(dot(p, vec2(-5.0, 13.0)) - t * 1.3);
            float w2 = sin(dot(p, vec2(19.0, -8.0)) - t * 2.0 + sin(p.y * 7.0 + t));
            float wave = 0.5 + 0.25 * w1 + 0.25 * w2;
            float caust = pow(abs(sin(p.x * 17.0 + sin(p.y * 15.0 + t) * 1.6 + t * 1.5) * sin(p.y * 16.0 + sin(p.x * 14.0 - t) * 1.6)), 8.0);
            float edge = 1.0 - smoothstep(0.9, 1.0, r);
            float foam = exp(-pow((r - 0.95) / 0.02, 2.0));
            vec3 col2 = mix(c2, c1, wave) + vec3(caust * 0.6) + vec3(foam);
            float a = clamp((0.38 + 0.3 * caust + 0.1 * wave) * edge + foam * 0.8, 0.0, 1.0) * vParams.z;
            outColor = vec4(col2 * a, a * (1.0 - glow));
            return;
        } else if (shape == 156) {   // a meteor's crater on your floor: a scorched dish (colour 2), a raised rim, cracks
            // glowing colour 1 while it's hot (colour 2's alpha: its heat)
            float r = length(p);
            float heat = vColor2.a;
            float ang = atan(p.y, p.x);
            float dish = 1.0 - smoothstep(0.55, 1.0, r);
            float rim = exp(-pow((r - 0.72) / 0.09, 2.0));
            float crack = pow(abs(sin(ang * 5.0 + sin(r * 11.0) * 0.7)), 30.0) * smoothstep(0.85, 0.15, r);
            float core = exp(-r * r * 10.0);
            float aS = clamp(dish * 0.85 + rim * 0.3, 0.0, 1.0) * vParams.z;
            vec3 base = c2 * (0.5 + 0.8 * rim);
            vec3 glowC = c1 * heat * (crack * 2.0 + core * 1.5);
            outColor = vec4(base * aS + glowC * vParams.z, aS);
            return;
        } else if (shape == 157) {   // a black hole: a black heart that hides what's behind it, a photon ring, an accretion
            // glow swirling round it (colour 1 hot, colour 2 cool; colour 2's alpha: its turn)
            float r = length(p);
            float ang = atan(p.y, p.x) + vColor2.a * 6.2831853;
            float core = 1.0 - smoothstep(0.34, 0.38, r);
            float ring = exp(-pow((r - 0.4) / 0.03, 2.0));
            float arms = 0.5 + 0.5 * cos(3.0 * ang - log(r + 0.05) * 7.0);
            float disc = exp(-pow((r - 0.55) / 0.2, 2.0)) * (0.35 + 0.65 * arms) * (1.0 - smoothstep(0.85, 1.0, r));
            vec3 glowC = mix(c2, c1, arms * 0.7 + ring) * (disc * 1.3 + ring * 2.0) * (1.0 - core);
            outColor = vec4(glowC * vParams.z, core * vParams.z);
            return;
        } else {                     // 158: a light cone (a disco beam): narrow at its source (local -y), wide where it
            // lands (+y), colour 1, brightest near its source
            float t = (p.y + 1.0) * 0.5;
            float w = mix(0.08, 0.95, t);
            float a = exp(-(p.x / w) * (p.x / w) * 2.5) * smoothstep(0.0, 0.08, t) * (1.0 - smoothstep(0.85, 1.0, t)) * (0.5 + 0.5 * (1.0 - t));
            outColor = soft(c1 * 1.2, a * vParams.z, glow);
            return;
        }
    } else if (shape >= 164 && shape <= 172) {
        // r184: the ORIGINALS' themed celebrations (xr_fx.cpp special_for_pack "orig_*", ORIGINALS-CELEBRATIONS.md)
        if (shape == 164) {          // an arcade marquee's words in 5x7 LED pixels on a dark panel, its border lights
            // chasing: which words by the quad's aspect ((6 n + 3) / 11 for n letters: 3 "1UP", 9 "GAME OVER",
            // 14 "NEW HIGH SCORE"); the letters cycle from colour 1 to colour 2 (colour 2's alpha: the phase)
            const int kFont[119] = int[119](
                0, 0, 0, 0, 0, 0, 0,             // ' '
                4, 12, 4, 4, 4, 4, 14,           // 1
                17, 17, 17, 17, 17, 17, 14,      // U
                30, 17, 17, 30, 16, 16, 16,      // P
                17, 25, 21, 19, 17, 17, 17,      // N
                31, 16, 16, 30, 16, 16, 31,      // E
                17, 17, 17, 21, 21, 21, 10,      // W
                17, 17, 17, 31, 17, 17, 17,      // H
                14, 4, 4, 4, 4, 4, 14,           // I
                14, 17, 16, 23, 17, 17, 15,      // G
                15, 16, 16, 14, 1, 1, 30,        // S
                14, 17, 16, 16, 16, 17, 14,      // C
                14, 17, 17, 17, 17, 17, 14,      // O
                30, 17, 17, 30, 20, 18, 17,      // R
                14, 17, 17, 31, 17, 17, 17,      // A
                17, 27, 21, 21, 17, 17, 17,      // M
                17, 17, 17, 17, 17, 10, 4);      // V
            const int kUp[3] = int[3](1, 2, 3);
            const int kOver[9] = int[9](9, 14, 15, 5, 0, 12, 16, 5, 13);
            const int kHigh[14] = int[14](4, 5, 6, 0, 7, 8, 9, 7, 0, 10, 11, 12, 13, 5);
            int n = int(floor((vParams.y * 11.0 - 3.0) / 6.0 + 0.5));
            float W = 6.0 * float(n) + 3.0;
            vec2 g = vec2((p.x * 0.5 + 0.5) * W, (0.5 - p.y * 0.5) * 11.0);  // in pixels: x right, y down
            float ph = vColor2.a;
            // the panel: dark glass, a rounded edge, a neon border of chasing lights
            float edge = sdBox(g - vec2(0.5 * W, 5.5), vec2(0.5 * W - 0.2, 5.3)) - 0.2;
            float inPanel = clamp(0.5 - edge / max(fwidth(edge), 1e-4), 0.0, 1.0);
            float bd = sdBox(g - vec2(0.5 * W, 5.5), vec2(0.5 * W - 0.75, 4.75));
            float band = exp(-bd * bd * 5.0);
            float chase = 0.45 + 0.55 * step(0.5, fract((g.x - g.y) * 0.25 - ph * 6.0));
            vec3 rgb = vec3(0.012, 0.01, 0.03) * 0.7 + c2 * band * chase * 1.3;
            float a = max(0.7, band * 0.9);
            // the letters
            float gx = g.x - 1.5;
            int li = int(floor(gx / 6.0));
            int cx = int(floor(gx - 6.0 * float(li)));
            int row = int(floor(g.y - 2.0));
            if (gx >= 0.0 && li >= 0 && li < n && cx < 5 && row >= 0 && row < 7) {
                int ch = n <= 3 ? kUp[min(li, 2)] : (n <= 9 ? kOver[min(li, 8)] : kHigh[min(li, 13)]);
                bool on = ((kFont[ch * 7 + row] >> (4 - cx)) & 1) == 1;
                vec2 f = fract(g) - 0.5;
                float led = sdBox(f, vec2(0.3)) - 0.1;
                float ledA = clamp(0.5 - led / max(fwidth(led), 1e-4), 0.0, 1.0);
                if (on) {
                    float t = 0.5 + 0.5 * sin(6.2831853 * (ph * 3.0 - float(li) * 0.09));
                    vec3 lc = mix(c1, c2, t) * 1.25 + vec3(0.25) * exp(-dot(f, f) * 18.0);
                    rgb = mix(rgb, lc, ledA);
                    a = mix(a, 1.0, ledA);
                } else {
                    rgb += vec3(0.035, 0.035, 0.05) * ledA;  // an LED that is off
                }
            }
            float A = vParams.z * inPanel;
            outColor = vec4(rgb * A, a * A * (1.0 - glow));
            return;
        } else if (shape == 165) {   // a moon (colour 1 its face, colour 2 its halo): its craters, darker toward its
            // limb, its dark part faintly lit; its phase in colour 2's alpha (0 a crescent, 0.5 half, 1 full)
            float R = 0.6;
            float r = length(p);
            float ph = vColor2.a;
            float disc = r - R;
            float inD = clamp(0.5 - disc / max(fwidth(disc), 1e-4), 0.0, 1.0);
            float lit;
            if (ph > 0.75) {
                lit = 1.0;
            } else if (ph > 0.25) {
                lit = smoothstep(-0.03, 0.03, p.x);
            } else {
                lit = smoothstep(-0.03, 0.03, length(p - vec2(-0.3, 0.08)) - R * 0.93);
            }
            vec3 face = c1;
            const vec3 kCr[6] = vec3[6](vec3(0.2, 0.18, 0.13), vec3(-0.2, -0.1, 0.1), vec3(0.06, -0.3, 0.14),
                                        vec3(-0.12, 0.3, 0.07), vec3(0.32, -0.14, 0.07), vec3(-0.34, 0.12, 0.06));
            float shadeC = 1.0;
            for (int i = 0; i < 6; ++i) {
                float dc = length(p - kCr[i].xy) - kCr[i].z;
                shadeC *= dc < 0.0 ? 0.78 + 0.1 * smoothstep(-kCr[i].z, 0.0, dc) : 1.0 + 0.12 * exp(-dc * 40.0) * step(0.0, dc);
            }
            // seas: soft darker blotches
            float sea = smoothstep(0.25, 0.0, length(p - vec2(-0.12, 0.05))) * 0.12 + smoothstep(0.2, 0.0, length(p - vec2(0.15, -0.12))) * 0.1;
            float limb = 0.72 + 0.28 * sqrt(max(0.0, 1.0 - (r / R) * (r / R)));
            vec3 surf = face * shadeC * limb * (1.0 - sea);
            vec3 col2 = mix(surf * 0.07, surf * 1.15, lit);
            float haloAmp = 0.3 + 0.5 * ph;
            float halo = exp(-max(disc, 0.0) * 6.0) * (1.0 - smoothstep(0.85, 1.0, r)) * haloAmp * (1.0 - inD);
            float A = vParams.z;
            outColor = vec4((col2 * inD + c2 * halo) * A, inD * A * (1.0 - glow));
            return;
        } else if (shape == 166) {   // a heraldic banner on a gold rod: colour 1 its field, colour 2 its border and
            // crest (a shield with a cross), a swallowtail, waving (colour 2's alpha: the phase), folds shaded
            float ph = vColor2.a * 6.2831853;
            float hang = clamp((0.86 - p.y) / 1.86, 0.0, 1.0);  // 0 at the rod .. 1 at the tails
            float wave = sin(p.y * 4.0 + ph * 3.0) * 0.07 * hang + sin(p.y * 9.0 - ph * 5.0) * 0.02 * hang;
            float x = p.x + wave;
            float foldShade = 0.82 + 0.18 * cos(p.y * 4.0 + ph * 3.0);
            float bottom = -1.0 + 0.3 * (1.0 - abs(x) / 0.88);  // the swallowtail's notch
            float body = max(abs(x) - 0.88, max(p.y - 0.86, bottom - p.y));
            vec3 col2 = c1;
            float border = max(abs(x) - 0.74, max(p.y - 0.74, bottom + 0.14 - p.y));
            if (border > 0.0) col2 = c2;
            vec2 cp = vec2(x, p.y - 0.12);
            float shieldD = max(sdBox(cp - vec2(0.0, 0.12), vec2(0.38, 0.22)), -cp.y - 0.5);
            shieldD = min(shieldD, sdCircle(cp - vec2(0.0, -0.1), 0.38));
            shieldD = max(shieldD, cp.y - 0.34);
            if (shieldD < 0.0) col2 = c2;
            float crossD = min(sdBox(cp - vec2(0.0, 0.0), vec2(0.06, 0.3)), sdBox(cp - vec2(0.0, 0.08), vec2(0.26, 0.06)));
            if (shieldD < -0.05 && crossD < 0.0) col2 = c1;
            col2 *= foldShade;
            // the rod and its finials
            float rod = min(sdBox(p - vec2(0.0, 0.9), vec2(0.97, 0.035)), min(sdCircle(p - vec2(-0.95, 0.9), 0.07), sdCircle(p - vec2(0.95, 0.9), 0.07)));
            vec3 gold = vec3(1.0, 0.72, 0.22) * (0.75 + 0.5 * smoothstep(0.86, 0.94, p.y));
            d = min(body, rod);
            col = rod < 0.0 ? gold : col2;
            shade = 0.75 + 0.25 * vShade;
        } else if (shape == 167) {   // a marble chess square: colour 1 one face, colour 2 the other, veins, a gold bevel
            float dd = sdBox(p, vec2(0.9)) - 0.06;
            vec3 face = gl_FrontFacing ? c1 : c2;
            float vein = abs(sin(p.x * 3.1 + sin(p.y * 2.3 + p.x) * 1.7 + p.y * 1.3));
            face *= 0.9 + 0.1 * smoothstep(0.0, 0.12, vein);
            float rim = smoothstep(-0.14, -0.02, dd);
            col = mix(face, vec3(0.95, 0.68, 0.2), rim * 0.85);
            col += vec3(0.12) * exp(-dot2(p - vec2(-0.45, 0.45)) * 3.0);
            d = dd;
        } else if (shape == 168) {   // a foam finger: colour 1 the foam, colour 2 its "#1" and cuff
            // a fist (the three folded fingers' knuckles on top), the index finger up tall, the thumb, a cuff
            float palm = sdBox(p - vec2(0.05, -0.35), vec2(0.42, 0.38)) - 0.12;
            float finger = sdBox(p - vec2(-0.17, 0.45), vec2(0.15, 0.43)) - 0.1;
            float knuck = min(sdCircle(p - vec2(0.12, 0.06), 0.13), sdCircle(p - vec2(0.36, 0.03), 0.12));
            float thumb = sdEllipse(p - vec2(0.56, -0.3), vec2(0.13, 0.24));
            float cuff = sdBox(p - vec2(0.05, -0.88), vec2(0.44, 0.1)) - 0.02;
            d = min(min(palm, finger), min(min(knuck, thumb), cuff));
            col = c1;
            // "#1" big on the palm
            vec2 tp = p - vec2(0.06, -0.38);
            float one = min(sdBox(tp - vec2(0.15, 0.0), vec2(0.075, 0.25)), sdBox(tp - vec2(0.07, 0.19), vec2(0.1, 0.055)));
            one = min(one, sdBox(tp - vec2(0.15, -0.23), vec2(0.14, 0.04)));
            vec2 hp = tp - vec2(-0.18, 0.0);
            float hash = min(min(sdBox(hp - vec2(-0.06, 0.0), vec2(0.03, 0.2)), sdBox(hp - vec2(0.06, 0.0), vec2(0.03, 0.2))),
                             min(sdBox(hp - vec2(0.0, 0.08), vec2(0.15, 0.028)), sdBox(hp - vec2(0.0, -0.08), vec2(0.15, 0.028))));
            if (min(one, hash) < 0.0 || cuff < 0.0) col = c2;
            if (abs(knuck) < 0.02 && p.y < 0.12) col = c1 * 0.7;   // the creases between the knuckles
            col *= 0.82 + 0.28 * (1.0 - length(p - vec2(-0.25, 0.35)) * 0.55);
        } else if (shape == 169) {   // a t-shirt: colour 1 the shirt, colour 2 its collar, cuffs and a stripe
            float body = sdBox(p - vec2(0.0, -0.22), vec2(0.5, 0.68)) - 0.04;
            vec2 sl = vec2(abs(p.x) - 0.62, p.y - 0.25);
            sl = vec2(sl.x * 0.8 - sl.y * 0.6, sl.x * 0.6 + sl.y * 0.8);
            float sleeve = sdBox(sl, vec2(0.24, 0.17)) - 0.03;
            float neck = sdCircle(p - vec2(0.0, 0.55), 0.2);
            d = max(min(body, sleeve), -neck);
            col = c1;
            if (abs(neck) < 0.06 || (sleeve < 0.0 && sl.x > 0.14) || abs(p.y + 0.05) < 0.07) col = c2;
            col *= 0.86 + 0.14 * sin(p.x * 5.0 + p.y * 3.0);
        } else if (shape == 170) {   // a title belt: colour 1 its gold plates, colour 2 the strap; a ruby, a star round
            // it, stitching, a glint sweeping over the gold (colour 2's alpha)
            float asp = vParams.y;
            vec2 q = vec2(p.x * asp, p.y);
            float strap = sdBox(q, vec2(asp - 0.06, 0.4)) - 0.04;
            float side = min(sdBox(vec2(abs(q.x) - 1.25, q.y), vec2(0.3, 0.42)) - 0.06, sdBox(vec2(abs(q.x) - 1.95, q.y), vec2(0.2, 0.33)) - 0.05);
            float plate = sdEllipse(q, vec2(0.98, 0.92));
            d = min(strap, min(side, plate));
            vec3 gold = c1 * (0.7 + 0.4 * (0.5 + 0.5 * q.y));
            col = c2 * (0.8 + 0.2 * (0.5 + 0.5 * q.y));
            if (abs(abs(q.y) - 0.31) < 0.025 && fract(q.x * 6.0) < 0.5 && plate > 0.0 && side > 0.0) col = c2 * 1.8 + 0.08;
            if (side < 0.0) col = gold * (0.9 + 0.1 * step(0.0, side + 0.08));
            if (plate < 0.0) {
                col = gold;
                if (abs(plate + 0.12) < 0.03) col = gold * 1.35;          // an engraved rim
                if (sdStar5(q, 0.62, 0.45) < 0.0) col = gold * 1.18;      // the star
                float gem = sdCircle(q, 0.2);
                if (gem < 0.0) col = vec3(0.75, 0.04, 0.1) * (0.7 + 0.6 * exp(-dot2(q - vec2(-0.06, 0.07)) * 60.0));
            }
            float sweep = vColor2.a * 9.0 - 4.5;
            float glint = exp(-pow(q.x + q.y * 0.6 - sweep, 2.0) * 5.0);
            if (plate < 0.0 || side < 0.0) col += vec3(0.6) * glint;
            shade = 0.9 + 0.1 * vShade;
        } else if (shape == 171) {   // a chess piece standing (colour 1, lit, its rim colour 2): which in colour 2's alpha,
            // 0 pawn, 1 knight, 2 bishop, 3 rook, 4 queen, 5 king
            int v = int(vColor2.a * 5.99);
            float base = min(sdBox(p - vec2(0.0, -0.86), vec2(0.56, 0.1)) - 0.03, sdBox(p - vec2(0.0, -0.7), vec2(0.42, 0.07)));
            float t = clamp((p.y + 0.62) / 0.9, 0.0, 1.0);
            float body = max(abs(p.x) - mix(0.34, 0.15, t), abs(p.y + 0.17) - 0.45);
            float collar = sdEllipse(p - vec2(0.0, 0.28), vec2(0.3, 0.06));
            float top;
            if (v == 0) {
                top = sdCircle(p - vec2(0.0, 0.5), 0.24);
                body = max(body, p.y - 0.3);
            } else if (v == 1) {   // the horse's head: a neck rising, a long muzzle forward (left), ears
                vec2 h = p - vec2(0.02, 0.32);
                h = vec2(h.x * 0.9 + h.y * 0.43, -h.x * 0.43 + h.y * 0.9);
                top = sdEllipse(h, vec2(0.24, 0.48));
                vec2 m = p - vec2(-0.26, 0.52);
                m = vec2(m.x * 0.94 - m.y * 0.34, m.x * 0.34 + m.y * 0.94);
                top = min(top, sdBox(m, vec2(0.24, 0.12)) - 0.06);
                top = min(top, sdTri(p, vec2(0.02, 0.78), vec2(0.14, 0.98), vec2(0.2, 0.72)));
                top = max(top, -sdCircle(p - vec2(-0.02, 0.62), 0.045));  // its eye
                body = max(body, p.y - 0.1);
                collar = sdEllipse(p - vec2(0.0, 0.08), vec2(0.34, 0.07));
            } else if (v == 2) {   // the mitre, its slit, a little ball
                top = min(sdEllipse(p - vec2(0.0, 0.52), vec2(0.21, 0.3)), sdCircle(p - vec2(0.0, 0.9), 0.07));
                vec2 s2 = p - vec2(0.05, 0.58);
                s2 = vec2(s2.x * 0.7 - s2.y * 0.7, s2.x * 0.7 + s2.y * 0.7);
                top = max(top, -sdBox(s2, vec2(0.025, 0.14)));
            } else if (v == 3) {   // the rook's tower and its battlements
                body = max(abs(p.x) - 0.28, abs(p.y + 0.15) - 0.47);
                top = sdBox(p - vec2(0.0, 0.52), vec2(0.38, 0.2));
                float notch = min(sdBox(p - vec2(-0.13, 0.7), vec2(0.06, 0.1)), sdBox(p - vec2(0.13, 0.7), vec2(0.06, 0.1)));
                top = max(top, -notch);
                collar = sdBox(p - vec2(0.0, 0.3), vec2(0.36, 0.05));
            } else if (v == 4) {   // the queen's crown: a band and five pearls
                top = sdBox(p - vec2(0.0, 0.48), vec2(0.24, 0.16)) - 0.03;
                for (int i = 0; i < 5; ++i) top = min(top, sdCircle(p - vec2(-0.26 + 0.13 * float(i), 0.7 + 0.06 * (1.0 - abs(float(i) - 2.0) * 0.5)), 0.065));
            } else {               // the king's crown and cross
                top = sdBox(p - vec2(0.0, 0.5), vec2(0.25, 0.17)) - 0.03;
                top = min(top, min(sdBox(p - vec2(0.0, 0.82), vec2(0.05, 0.16)), sdBox(p - vec2(0.0, 0.84), vec2(0.15, 0.045))));
            }
            d = min(min(base, body), min(collar, top));
            float lightUp = 0.55 + 0.45 * smoothstep(-1.0, 1.0, p.y);
            col = c1 * lightUp + vec3(0.35) * exp(-pow((p.x + 0.12) / 0.06, 2.0)) * 0.5;  // a gloss stripe
            col = mix(col, c2, smoothstep(-0.06, -0.01, d) * 0.85);                     // its rim in colour 2
            if (abs(collar) < 0.02 || abs(base + 0.0) < 0.02) col = c2;
            shade = 1.0;
        } else {                     // 172: a luchador mask: colour 1 the mask, colour 2 its trim (round the eyes, the
            // mouth, a stripe up the brow, flame wings); eye and mouth holes are see-through
            float mask = sdEllipse(p, vec2(0.66, 0.9));
            vec2 e = vec2(abs(p.x) - 0.26, p.y - 0.1);
            float eye = sdEllipse(e, vec2(0.18, 0.11));
            float mouth = sdEllipse(p - vec2(0.0, -0.45), vec2(0.17, 0.08));
            d = max(mask, -min(eye, mouth));
            col = c1;
            float wing = sdEllipse(vec2(e.x - 0.08, e.y - 0.06), vec2(0.32, 0.2));
            if (wing < 0.0 || mouth < 0.06 || (abs(p.x) < 0.05 && p.y > 0.24)) col = c2;
            if (eye < 0.045 && eye > 0.0) col = vec3(1.0);
            col *= 0.8 + 0.25 * (1.0 - length(p * vec2(1.0, 0.8)));
            shade = 0.85 + 0.15 * vShade;
        }
    } else if (shape == 163) {
        // r169: a real-fire flipbook flame (xr_balls.cpp): its frame in colour 2's alpha (0-15, +16 mirrored), its
        // temperature (colour 1 red: 1 hot .. 0 cooled to smoke), how smoky (green), how bright (blue x 2); its smoke's
        // colour colour 2. The fire adds light (it glows over passthrough), the smoke is laid over
        int fr = int(vColor2.a * 255.0 + 0.5);
        int f = fr & 15;
        float u = (fr & 16) != 0 ? 1.0 - vUv.x : vUv.x;
        vec2 tuv = vec2((float(f % 4) + u) / 4.0, (float(f / 4) + (1.0 - vUv.y)) / 4.0);
        vec4 s = texture(flameTex, tuv);
        float temp = vColor1.r, smokeMul = vColor1.g, bright = vColor1.b * 2.0;
        float h = s.r * (0.25 + 0.75 * temp);
        vec3 emit = lin(flameRamp(h)) * h * bright * 1.4;
        float sa = clamp(s.g + s.r * (1.0 - temp) * 0.7, 0.0, 1.0) * smokeMul * (1.0 - h);
        float A = vParams.z;
        outColor = vec4((emit + c2 * sa) * A, sa * A);
        return;
    } else if (shape == 160) {
        // r168 / r170: the BALL SKINS' shop preview - a ball drawn per pixel from its own baked frames (binding 2:
        // xr_balls.cpp bakes them from src/platform/ball_shade.h, the game's own - 8 x 4 cells, each its picture
        // round the pole), its sphere 0.62 of the quad, its turn the instance's quaternion (vQuat), where it is in its
        // frames in the glow slot (blended between two), its own light or lit (colour 1 red), how many frames (colour 1
        // green), its glow's colour and strength (colour 2) round it
        float R = 0.62;
        float r = length(p);
        vec3 back = normalize(cross(vRight, vUp));  // (toward your eye)
        vec3 colIn = vec3(0.0);
        float aIn = 0.0;
        float frames = max(1.0, floor(vColor1.g * 255.0 + 0.5));
        float fpos = mod(vParams.w, frames);
        float f0 = floor(fpos), fb = fpos - f0;
        float f1 = mod(f0 + 1.0, frames);
        if (r < R) {
            vec2 s2 = p / R;
            vec3 nv = vec3(s2, sqrt(max(0.0, 1.0 - dot(s2, s2))));
            vec3 nw = normalize(vRight * nv.x + vUp * nv.y + back * nv.z);
            vec4 qc = vec4(-vQuat.xyz, vQuat.w);
            vec3 o = nw + 2.0 * cross(qc.xyz, cross(qc.xyz, nw) + qc.w * nw);
            vec3 ns = vec3(o.x, -o.z, o.y);  // (the shade's own frame: Z its pole)
            float u = atan(ns.y, ns.x) / 6.2831853;
            u = u - floor(u);
            float vv = acos(clamp(ns.z, -1.0, 1.0)) / 3.14159265;
            vec2 c0 = vec2(mod(f0, 8.0), floor(f0 / 8.0)), c1 = vec2(mod(f1, 8.0), floor(f1 / 8.0));
            vec2 inCell = vec2(u, clamp(vv, 0.01, 0.99));
            vec3 t0 = texture(ballTex, (c0 + inCell) / vec2(8.0, 4.0)).rgb;
            vec3 t1 = texture(ballTex, (c1 + inCell) / vec2(8.0, 4.0)).rgb;
            vec3 tc = mix(t0, t1, fb);  // (already linear: an sRGB texture)
            if (vColor1.r > 0.5) {
                colIn = tc * (0.85 + 0.3 * sqrt(nv.z));
            } else {
                vec3 L = normalize(vec3(-0.4, 0.75, 0.5));
                float lam = max(0.0, dot(nw, L));
                vec3 H = normalize(L + back);
                float spec = pow(max(0.0, dot(nw, H)), 48.0) * 0.35;
                colIn = tc * (0.22 + 0.9 * lam) + vec3(spec);
            }
            float aa = max(fwidth(r), 1e-4);
            aIn = clamp((R - r) / aa, 0.0, 1.0);
        }
        vec3 halo = vec3(0.0);
        if (r > R * 0.97) {
            float ang = atan(p.y, p.x);
            float lick = bsFbm(vec3(cos(ang) * 2.0, sin(ang) * 2.0 - vParams.w * 0.2, vParams.w * 0.1), 3);
            float g = exp(-(r - R) / R * 6.0) * (0.6 + 0.6 * lick) * vColor2.a;
            halo = c2 * clamp(g, 0.0, 1.0) * (1.0 - smoothstep(0.9, 1.0, r));
        }
        float A = vParams.z;
        outColor = vec4((colIn * aIn + halo * (1.0 - aIn)) * A, aIn * A);
        return;    } else {                     // spark: a soft glowing dot
        float r2 = dot(p, p);
        float a = exp(-r2 * 5.0) * (1.0 - smoothstep(0.85, 1.0, r2));
        vec3 sc = mix(c2, c1, exp(-r2 * 14.0));
        float alpha = a * vParams.z;
        outColor = vec4(sc * alpha * 1.6, alpha * (1.0 - glow));
        return;
    }

    float aa = max(fwidth(d), 1e-4);
    float a = clamp(0.5 - d / aa, 0.0, 1.0) * vParams.z;
    vec3 rgb = col * shade;
    outColor = vec4(rgb * a, a * (1.0 - glow));
}
