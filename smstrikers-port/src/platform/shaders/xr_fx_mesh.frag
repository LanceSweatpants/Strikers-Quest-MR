// strikers-quest: goal celebration pieces as 3D meshes, lit: a key light from above, a fill, a
// highlight and a rim, mirror-like for gold and foil. Detail (spots, eyes, plates, swirls) is
// painted from the piece's own coordinates. Shape ids match xr_fx.cpp's Shape enum; output is
// premultiplied.
#version 450

layout(location = 0) in vec3 vLocal;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vView;
layout(location = 3) in vec4 vColor1;
layout(location = 4) in vec4 vColor2;
layout(location = 5) in vec4 vParams;
layout(location = 6) in vec2 vMatAux;

layout(location = 0) out vec4 outColor;

const vec3 kKey = vec3(0.2756, 0.9187, 0.3675);    // normalize(0.3, 1.0, 0.4)
const vec3 kFill = vec3(-0.7194, 0.2998, -0.5995); // normalize(-0.6, 0.25, -0.5)
const float kPi = 3.14159265;

vec3 lin(vec3 srgb) { return pow(srgb, vec3(2.2)); }

bool inEllipse(vec2 p, vec2 c, vec2 r) {
    vec2 d = (p - c) / r;
    return dot(d, d) < 1.0;
}

bool spotAt(vec3 d, vec3 s, float size) { return dot(d, normalize(s)) > size; }

// r54: the controllers' etched artwork. 2D distance shapes (inside < 0), cells about a unit across.
float dot2(vec2 v) { return dot(v, v); }
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
float sdHeart(vec2 p) {
    p.x = abs(p.x);
    if (p.y + p.x > 1.0) return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}
float sdHexagon(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}
float sdFlower(vec2 p, float r) {
    float d = length(p) - r * 0.55;
    for (int i = 0; i < 5; ++i) {
        float a = 6.2831853 * float(i) / 5.0;
        d = min(d, length(p - r * vec2(sin(a), cos(a))) - r * 0.62);
    }
    return d;
}
// r68c: the guests' own outlines (xr_fx make_etch, tools/outside/etchres.py): 8 x 8 cells of distance
// (0.5 = the outline, a quarter of a cell either way over the range), one per guest in roster order.
layout(set = 0, binding = 0) uniform sampler2D uEtch;
float sdGuest(int cell, vec2 p) {
    vec2 c = clamp(p, vec2(-0.5), vec2(0.5));
    float outside = length(p - c);
    vec2 uv = (vec2(float(cell % 8), float(cell / 8)) + vec2(c.x + 0.5, 0.5 - c.y)) / 8.0;
    float d = (textureLod(uEtch, uv, 0.0).r - 128.0 / 255.0) * (255.0 / 127.0) * 0.25;
    return d + outside;
}
// The captain's motif (theme = eCharacterClass + 1; 0 = no team yet; r68c: 100 + a guest's roster
// place = its own outline).
float motif(int theme, vec2 p) {
    if (theme >= 100) return sdGuest(theme - 100, p);
    int c = theme - 1;
    if (c == 0 || c == 7) return sdHeart((p + vec2(0.0, 0.32)) * 1.6) / 1.6;      // Birdo, Peach
    if (c == 1) return sdFlower(p, 0.2);                                            // Daisy
    if (c == 2) return (abs(fract((p.x + p.y) * 1.5) - 0.5) - 0.13) / 1.5;          // Donkey Kong: his tie's stripes
    if (c == 4) return sdHexagon(p, 0.3);                                           // Koopa: shell plates
    if (c == 8 || c == 11) return length(p) - (c == 11 ? 0.27 : 0.22);              // Toad, Yoshi: spots
    if (c == 9) return (abs(p.x) + abs(p.y) - 0.34) * 0.7071;                       // Waluigi: diamonds
    if (c == 10) return abs(p.y - 0.2 * (abs(fract(p.x * 1.5) - 0.5) * 4.0 - 1.0)) - 0.07; // Wario: zigzag
    return sdStar5(p, 0.36, 0.45);                                                  // Mario, Luigi, Hammer Bro, no team
}
// Etched into the body: the motif in small staggered cells over the surface, and one large inlay of
// it on each side of the handle (where the grip's origin is). `lp` = the local position (meters, grip
// space), the plane picked by the surface's own facing (from the position's screen derivatives).
vec3 etch(vec3 base, vec3 detail, vec3 lp, int theme) {
    vec3 fn = abs(cross(dFdx(lp), dFdy(lp)));
    vec2 q = fn.x >= fn.y && fn.x >= fn.z ? lp.yz : (fn.y >= fn.z ? lp.xz : lp.xy);
    bool side = fn.x >= fn.y && fn.x >= fn.z;
    // The large inlay: 3.4 cm across, in the detail colour with a dark etched outline.
    float dh = side ? motif(theme, q / 0.034) * 0.034 : 1.0;
    float aa = max(fwidth(dh), 1e-5);
    vec3 col = base;
    if (side && dh < 0.0012) {
        float inlay = 1.0 - smoothstep(-aa, aa, dh);
        float rim = 1.0 - smoothstep(0.0003, 0.0003 + aa, abs(dh - 0.0004));
        col = mix(col, detail, inlay * 0.9);
        col *= 1.0 - 0.55 * rim;
        return col;
    }
    // The small ones: 9 mm cells, every other one, rows staggered; a thin dark groove with a lighter
    // lip, kept clear of the inlay.
    vec2 uv = q / 0.009;
    uv.x += 0.5 * mod(floor(uv.y), 2.0);
    vec2 id = floor(uv);
    if (mod(id.x + id.y, 2.0) > 0.5) return col;
    float d = motif(theme, fract(uv) - 0.5) * 0.009;
    float a2 = max(fwidth(d), 1e-5);
    float groove = 1.0 - smoothstep(0.00025, 0.00025 + a2, abs(d));
    float lip = 1.0 - smoothstep(0.00025, 0.00025 + a2, abs(d + 0.0005));
    col *= 1.0 - 0.32 * groove;
    col += 0.06 * lip * (1.0 - groove);
    return col;
}

void main() {
    int shape = int(vParams.x + 0.5);
    int mat = int(vMatAux.x + 0.5);
    float aux = vMatAux.y;
    vec3 c1 = lin(vColor1.rgb);
    vec3 c2 = lin(vColor2.rgb);
    vec3 N = normalize(vNormal);
    vec3 V = normalize(vView);
    vec3 p = vLocal;

    vec3 base = mat == 0 ? c1 : (mat == 1 ? c2 : (mat == 2 ? vec3(0.86, 0.76, 0.58) : vec3(0.05, 0.035, 0.025)));
    float spec = 0.4;    // highlight strength
    float shine = 40.0;  // highlight tightness
    bool metal = false;
    float glow = 0.0;

    if (shape == 0 || shape == 15) {
        // Paper and foil: a different colour on each side, lit on the side you see.
        bool front = dot(N, V) >= 0.0;
        base = front ? c1 : c2;
        if (!front) N = -N;
        spec = 0.12;
        shine = 16.0;
        metal = shape == 15;
    } else if (shape == 1) {         // coin: all gold
        metal = true;
    } else if (shape == 2) {         // heart: glossy
        spec = 0.75;
        shine = 60.0;
    } else if (shape == 3) {         // Super Star: glossy, a little glow, two eyes on the front
        spec = 0.8;
        shine = 70.0;
        glow = 0.18;
        if (p.z > 0.05 && (inEllipse(p.xy, vec2(-0.07, 0.04), vec2(0.028, 0.07)) ||
                           inEllipse(p.xy, vec2(0.07, 0.04), vec2(0.028, 0.07)))) {
            base = vec3(0.015);
            glow = 0.0;
        }
    } else if (shape == 4) {         // petal: yellow toward the base
        base = mix(c2, c1, smoothstep(-0.44, -0.22, p.y));
        spec = 0.2;
        shine = 20.0;
    } else if (shape == 5) {         // mushroom
        if (mat == 0) {
            vec3 d = normalize((p - vec3(0.0, -0.06, 0.0)) / vec3(0.5, 0.48, 0.5));
            bool spot = spotAt(d, vec3(0.0, 1.0, 0.0), 0.93);
            for (int i = 0; i < 5; ++i) {
                float a = 0.31 + float(i) * 1.2566;
                spot = spot || spotAt(d, vec3(0.866 * cos(a), 0.45, 0.866 * sin(a)), 0.95);
            }
            if (spot) base = c2;
            spec = 0.65;
            shine = 50.0;
        } else {
            if (aux > 0.5) base *= 0.78;   // gills
            else if (p.z > 0.08 && (inEllipse(p.xy, vec2(-0.07, -0.24), vec2(0.028, 0.065)) ||
                                    inEllipse(p.xy, vec2(0.07, -0.24), vec2(0.028, 0.065)))) {
                base = vec3(0.015);        // the eyes
            }
            spec = 0.15;
            shine = 20.0;
        }
    } else if (shape == 6) {         // egg: spots
        vec3 d = normalize(p / vec3(0.36, 0.5, 0.36));
        if (spotAt(d, vec3(0.0, 0.85, 0.52), 0.94) || spotAt(d, vec3(0.8, 0.3, 0.52), 0.93) ||
            spotAt(d, vec3(-0.7, 0.2, -0.68), 0.93) || spotAt(d, vec3(0.3, -0.5, -0.8), 0.94) ||
            spotAt(d, vec3(-0.5, -0.6, 0.62), 0.93) || spotAt(d, vec3(0.9, -0.35, -0.25), 0.95) ||
            spotAt(d, vec3(-0.95, 0.25, 0.2), 0.95) || spotAt(d, vec3(0.1, -0.95, 0.1), 0.95)) {
            base = c2;
        }
        spec = 0.5;
        shine = 45.0;
    } else if (shape == 7) {         // banana: brown tips
        if (aux < 0.06 || aux > 0.94) base = c2;
        spec = 0.35;
        shine = 30.0;
    } else if (shape == 8) {         // shell: plates outlined in the rim colour
        if (mat == 0) {
            vec3 d = normalize(vec3(p.x / 0.44, p.y / 0.34, p.z / 0.44));
            float pol = acos(clamp(d.y, -1.0, 1.0));
            float seg = atan(d.z, d.x) / (kPi / 3.0);
            float edge = abs(seg - floor(seg + 0.5)) * (kPi / 3.0) * sin(pol);
            if (abs(pol - 0.62) < 0.05 || (pol > 0.62 && edge < 0.04)) base = c2;
        }
        spec = 0.7;
        shine = 60.0;
    } else if (shape == 9) {         // hammer: steel head, wooden handle
        if (mat == 1) metal = true;
        else { spec = 0.2; shine = 18.0; }
    } else if (shape == 10) {        // rose: the bloom swirls toward its heart
        if (mat == 0) {
            float r = length(p.xz);
            float swirl = sin(atan(p.z, p.x) + r * 48.0);
            float top = smoothstep(0.7, 0.85, aux);
            base *= mix(1.0, 0.62 + 0.38 * smoothstep(-0.3, 0.6, swirl), top);
            spec = 0.3;
            shine = 25.0;
        } else {
            spec = 0.25;
            shine = 25.0;
        }
    } else if (shape == 11) {        // garlic: papery creases between the cloves
        base = mix(c2 * 0.85, c1, smoothstep(0.05, 0.35, aux));
        if (p.y < -0.42) base *= 0.75;
        spec = 0.25;
        shine = 22.0;
    } else if (shape == 12) {        // bow: satin
        spec = 0.45;
        shine = 25.0;
    } else if (shape == 21) {        // r53: the controllers in your hands, glossy plastic, pressed parts lit
        spec = mat == 3 ? 0.25 : 0.6;
        shine = mat == 3 ? 24.0 : 64.0;
        glow = 1.1 * vParams.w;
        // r54: the team's artwork etched into the body (vParams.y = the captain's class + 1).
        if (mat == 0) base = etch(base, c2, p, int(vParams.y + 0.5));
    } else if (shape == 22) {        // r57: a trophy: a polished cup on a glossy base; not won yet = dull
        bool ghost = vParams.y > 0.5;
        if (mat == 0 && !ghost) metal = true;
        spec = ghost ? 0.15 : 0.6;
        shine = ghost ? 12.0 : 70.0;
        glow = vParams.w;
    } else if (shape == 25 || shape == 26) {
        // r61: the home ground's banner (25) and pennant (26): cloth in the team's main colour with
        // stripes of the second, and the captain's emblem (vParams.y = class + 1; 20 = the comeback
        // arrow) in white on a disc of the second colour; gold rods and knobs.
        if (mat == 3 || mat == 1) {
            base = mat == 3 ? vec3(0.95, 0.68, 0.16) : vec3(1.0, 0.8, 0.25);
            metal = true;
        } else {
            int theme = int(vParams.y + 0.5);
            bool banner = shape == 25;
            vec2 c = banner ? vec2(0.0, 0.12) : vec2(0.17, 0.31);
            float r = banner ? 0.13 : 0.07;
            if (banner ? abs(p.x) > 0.155 : p.x < 0.07) base = c2;
            float disc = length(p.xy - c) - r;
            vec2 q = (p.xy - c) / (1.9 * r);
            float d;
            if (theme == 20) {
                // An arrow pointing up: a triangle over a shaft.
                vec2 a = vec2(abs(q.x), q.y);
                float head = max(a.x * 0.9 + (a.y - 0.38) * 0.6, -(a.y - 0.02));
                float shaft = max(a.x - 0.11, abs(a.y + 0.2) - 0.24);
                d = min(head, shaft) * 0.8;
            } else {
                d = motif(theme, q);
            }
            float aa = max(fwidth(d), 1e-4);
            float aad = max(fwidth(disc), 1e-5);
            float inDisc = 1.0 - smoothstep(-aad, aad, disc);
            float ring = 1.0 - smoothstep(0.0, 2.0 * aad, abs(disc + 0.006));
            vec3 emb = mix(c2, vec3(1.0), 1.0 - smoothstep(-aa, aa, d));
            emb *= 1.0 - 0.6 * (1.0 - smoothstep(0.0, 2.0 * aa, abs(d)));
            base = mix(base, emb, inDisc);
            base = mix(base, vec3(1.0, 0.82, 0.3), ring * 0.9);
            // The weave.
            base *= 0.93 + 0.07 * sin(p.x * 700.0) * sin(p.y * 700.0);
            if (dot(N, V) < 0.0) N = -N;  // cloth: lit on the side you see
            spec = 0.12;
            shine = 14.0;
        }
        glow = vParams.w;
    } else if (shape == 61) {
        // r89: the home corner's rug: the captain's main colour with a border and a ring of the second, its
        // emblem (vParams.y as a banner's) in white on a disc of the second in the middle, a soft pile.
        float r = length(p.xz);
        int theme = int(vParams.y + 0.5);
        base = c1;
        if (r > 0.44 || abs(r - 0.33) < 0.016) base = c2;
        if (abs(r - 0.47) < 0.008) base = vec3(1.0, 0.82, 0.3);  // a gold thread round the border
        float disc = r - 0.2;
        vec2 q = vec2(p.x, -p.z) / (1.9 * 0.2);
        float d = motif(theme, q);
        float aa = max(fwidth(d), 1e-4);
        float aad = max(fwidth(disc), 1e-5);
        float inDisc = 1.0 - smoothstep(-aad, aad, disc);
        float ring = 1.0 - smoothstep(0.0, 2.0 * aad, abs(disc + 0.006));
        vec3 emb = mix(c2, vec3(1.0), 1.0 - smoothstep(-aa, aa, d));
        emb *= 1.0 - 0.6 * (1.0 - smoothstep(0.0, 2.0 * aa, abs(d)));
        base = mix(base, emb, inDisc);
        base = mix(base, vec3(1.0, 0.82, 0.3), ring * 0.9);
        base *= 0.9 + 0.1 * sin(p.x * 900.0 + sin(p.z * 300.0)) * sin(p.z * 900.0);  // the pile
        spec = 0.06;
        shine = 8.0;
        glow = vParams.w;
    } else if (shape == 27) {
        // r61: the Super Strike ball: white with dark panels, glossy, glowing in the second colour.
        vec3 d = normalize(p);
        bool panel = false;
        const vec3 ico[12] = vec3[12](vec3(0.0, 0.526, 0.851), vec3(0.0, 0.526, -0.851), vec3(0.0, -0.526, 0.851),
                                      vec3(0.0, -0.526, -0.851), vec3(0.526, 0.851, 0.0), vec3(0.526, -0.851, 0.0),
                                      vec3(-0.526, 0.851, 0.0), vec3(-0.526, -0.851, 0.0), vec3(0.851, 0.0, 0.526),
                                      vec3(-0.851, 0.0, 0.526), vec3(0.851, 0.0, -0.526), vec3(-0.851, 0.0, -0.526));
        for (int i = 0; i < 12; ++i) panel = panel || dot(d, ico[i]) > 0.93;
        base = panel ? vec3(0.04) : vec3(0.95);
        spec = 0.8;
        shine = 80.0;
        glow = 0.0;
        float rimGlow = pow(1.0 - max(dot(N, V), 0.0), 2.0);
        base += c2 * vParams.w * (0.25 + 1.4 * rimGlow);
    } else if (shape == 28) {
        // r61: a Boo (Luigi's suite): white, glowing faintly, his face on +Z: two tall eyes and a wide
        // open mouth with its tongue, or, shy (vParams.y), his hands over his eyes and a blush.
        vec3 d = normalize(p);
        base = vec3(0.96, 0.97, 1.0);
        spec = 0.3;
        shine = 28.0;
        glow = 0.22;
        if (d.z > 0.35) {
            vec2 f = d.xy;
            bool shy = vParams.y > 0.5;
            if (shy) {
                bool handL = inEllipse(f, vec2(-0.2, 0.13), vec2(0.17, 0.14));
                bool handR = inEllipse(f, vec2(0.2, 0.13), vec2(0.17, 0.14));
                bool edge = (handL && !inEllipse(f, vec2(-0.2, 0.13), vec2(0.15, 0.12))) ||
                            (handR && !inEllipse(f, vec2(0.2, 0.13), vec2(0.15, 0.12)));
                if (edge) base = vec3(0.7, 0.72, 0.8);
                else if (!handL && !handR && (inEllipse(f, vec2(-0.34, -0.06), vec2(0.08, 0.05)) ||
                                              inEllipse(f, vec2(0.34, -0.06), vec2(0.08, 0.05)))) {
                    base = vec3(1.0, 0.45, 0.55);  // the blush
                } else if (inEllipse(f, vec2(0.0, -0.2), vec2(0.07, 0.05))) {
                    base = vec3(0.3, 0.03, 0.06);
                }
            } else {
                if (inEllipse(f, vec2(-0.17, 0.16), vec2(0.06, 0.13)) || inEllipse(f, vec2(0.17, 0.16), vec2(0.06, 0.13))) {
                    base = vec3(0.02);
                    glow = 0.0;
                } else if (inEllipse(f, vec2(0.0, -0.13), vec2(0.3, 0.2)) && f.y < -0.07) {
                    base = inEllipse(f, vec2(0.0, -0.3), vec2(0.13, 0.07)) ? vec3(1.0, 0.35, 0.5) : vec3(0.28, 0.02, 0.05);
                    // Two little fangs at the top of the mouth.
                    vec2 t = vec2(abs(f.x) - 0.12, f.y + 0.07);
                    if (t.y > -0.06 && abs(t.x) < 0.03 * (1.0 + t.y / 0.06)) base = vec3(1.0);
                    glow = 0.0;
                }
            }
        }
    } else if (shape >= 40 && shape <= 60) {
        // r82: the themed celebrations' pieces (xr_fx_mesh.h): the surface from aux: 0 glossy, 1 metal,
        // 2 rough stone, 3 gem / ice (shining, a little glow), 4 wood.
        spec = 0.5;
        shine = 45.0;
        if (aux > 0.5 && aux < 1.5) {
            metal = true;
        } else if (aux > 1.5 && aux < 2.5) {
            float n = sin(p.x * 37.0) * sin(p.y * 41.0 + 1.3) * sin(p.z * 29.0 + 0.7);
            base *= 0.78 + 0.22 * n;
            spec = 0.06;
            shine = 10.0;
        } else if (aux > 2.5 && aux < 3.5) {
            spec = 0.95;
            shine = 90.0;
            glow = 0.25 + 0.4 * vParams.w;
        } else if (aux > 3.5) {
            float grain = sin(p.y * 90.0 + sin(p.x * 40.0) * 2.0);
            base *= 0.85 + 0.15 * grain;
            spec = 0.2;
            shine = 18.0;
        }
        if (shape == 43) {           // a Poke Ball: top half colour 1, bottom colour 2, a black band and button
            base = p.y > 0.0 ? c1 : c2;
            if (abs(p.y) < 0.045) base = vec3(0.03);
            float r = length(p.xy);
            if (p.z > 0.0 && r < 0.15) base = r < 0.1 ? vec3(0.97) : vec3(0.03);
            spec = 0.8;
            shine = 70.0;
        } else if (shape == 49) {    // a pill: its two halves
            base = p.y > 0.0 ? c1 : c2;
            spec = 0.8;
            shine = 70.0;
        } else if (shape == 41 && mat == 0 && p.z > 0.0) {  // a shield's emblem: a triangle over a bar
            vec2 q = p.xy - vec2(0.0, 0.1);
            float tri = max(abs(q.x) * 0.866 + q.y * 0.5 - 0.11, -q.y - 0.1);
            if (tri < 0.0 || (abs(p.y + 0.12) < 0.03 && abs(p.x) < 0.2)) base = c2;
        } else if (shape == 60) {    // a pearl
            spec = 0.9;
            shine = 80.0;
            glow = 0.12;
        }
    } else if (shape == 23) {        // r57: the shelf: wood with its grain, a gold trim, dark brackets
        if (mat == 0) {
            float grain = sin(p.x * 160.0 + sin(p.z * 45.0 + p.x * 9.0) * 2.5);
            base *= 0.86 + 0.14 * grain;
            spec = 0.18;
            shine = 18.0;
        } else if (mat == 1) {
            metal = true;
        } else {
            spec = 0.35;
            shine = 30.0;
        }
    }

    float key = max(dot(N, kKey), 0.0);
    float fill = max(dot(N, kFill), 0.0);
    float nv = max(dot(N, V), 0.0);
    float rim = pow(1.0 - nv, 3.0);
    vec3 col;
    if (metal) {
        // A bright sky above, a dark floor below, the key light as a hot spot.
        vec3 R = reflect(-V, N);
        float sky = smoothstep(-0.35, 0.6, R.y);
        vec3 env = mix(vec3(0.08), vec3(1.3), sky) + vec3(3.5) * pow(max(dot(R, kKey), 0.0), 90.0);
        col = base * (0.15 + 0.85 * env * (0.45 + 0.55 * (0.5 + 0.5 * dot(N, kKey)))) + base * 0.3 * rim;
    } else {
        vec3 H = normalize(kKey + V);
        float hl = pow(max(dot(N, H), 0.0), shine) * spec;
        col = base * (0.3 + 0.85 * key + 0.25 * fill) + vec3(hl) + rim * 0.22 * mix(base, vec3(1.0), 0.5);
    }
    col += base * glow;
    float a = vParams.z;
    outColor = vec4(col * a, a);
}
