// strikers-quest: goal celebration effects, the pieces' shapes as signed distances in the quad's
// -1..1 square (negative inside). Shape ids match xr_fx.cpp's Shape enum. Output is premultiplied;
// a glowing piece gives up alpha so it adds light (over passthrough too).
#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor1;
layout(location = 2) in vec4 vColor2;
layout(location = 3) in vec4 vParams;
layout(location = 4) in float vShade;

layout(location = 0) out vec4 outColor;

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
    } else {                     // spark: a soft glowing dot
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
