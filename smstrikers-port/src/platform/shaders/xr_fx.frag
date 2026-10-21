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

vec3 lin(vec3 srgb) { return pow(srgb, vec3(2.2)); }

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
