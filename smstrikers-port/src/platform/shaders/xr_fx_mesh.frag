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
// r130: a box (half size h) and a line from a to b, for the new pieces' painted details.
float sdBox2(vec2 p, vec2 h) {
    vec2 d = abs(p) - h;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}
// r68c: the guests' own outlines (xr_fx make_etch, tools/outside/etchres.py): 16 x 32 cells (r135; r121 16 x 16) of
// distance (0.5 = the outline, a quarter of a cell either way over the range), one per guest in roster order.
layout(set = 0, binding = 0) uniform sampler2D uEtch;
float sdGuest(int cell, vec2 p) {
    vec2 c = clamp(p, vec2(-0.5), vec2(0.5));
    float outside = length(p - c);
    // r135: 16 across, 32 down (cell i where the 16 x 16 atlas had it, cells 256-511 below)
    vec2 uv = (vec2(float(cell % 16), float(cell / 16)) + vec2(c.x + 0.5, 0.5 - c.y)) / vec2(16.0, 32.0);
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
            // r124: the stripes' edges smoothed over a pixel (a hard edge stair-stepped and shimmered)
            float se = banner ? abs(p.x) - 0.155 : 0.07 - p.x;
            base = mix(base, c2, smoothstep(-0.5, 0.5, se / max(fwidth(se), 1e-5)));
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
            // The weave. r117 (Lance: the banners "blurry as fuck"): faded out where it's finer than a
            // couple of pixels (there it only shimmered and muddied the cloth)
            float wf = clamp(1.0 - 280.0 * max(fwidth(p.x), fwidth(p.y)), 0.0, 1.0);
            base *= 1.0 - 0.07 * wf + 0.07 * wf * sin(p.x * 700.0) * sin(p.y * 700.0);
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
    } else if ((shape >= 40 && shape <= 60) || (shape >= 80 && shape <= 133)) {  // r124: 80-89 the Marvel heroes' pieces (r127: 90-92; r130: 93-118; r135: 119-133)
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
        } else if (shape == 83 && mat == 2) {  // r124: Captain America's shield: its ring and star white, not cream
            base = vec3(0.88);
        } else if (shape == 60) {    // a pearl
            spec = 0.9;
            shine = 80.0;
            glow = 0.12;
        }
        // r130: the new roster's light pieces (xr_fx_mesh.h): the detail painted here instead of modelled. A piece
        // set to glow (its drop's glow, vParams.w) glows.
        if (shape >= 93) {
            glow = max(glow, 0.55 * vParams.w);
            if (shape == 93 && vParams.w > 0.05) {
                // a crescent moon set to glow (Umbreon's rings, a dart of light): lit from within, not chrome
                metal = false;
                spec = 0.9;
                shine = 80.0;
            } else if (shape == 94 && abs(p.z) > 0.03) {
                // the "4" badge's faces: a ring and a 4 in colour 2 (mirrored on the back, so it reads there too)
                vec2 q = vec2(p.z > 0.0 ? p.x : -p.x, p.y);
                float ring = abs(length(q) - 0.36) - 0.028;
                float stem = sdBox2(q - vec2(0.07, -0.02), vec2(0.045, 0.24));
                float crossbar = sdBox2(q - vec2(-0.03, -0.08), vec2(0.17, 0.04));
                float diag = sdSeg(q, vec2(-0.19, -0.08), vec2(0.06, 0.22)) - 0.042;
                float d = min(min(ring, stem), min(crossbar, diag));
                float aa = max(fwidth(d), 1e-4);
                base = mix(base, c2, 1.0 - smoothstep(-aa, aa, d));
            } else if (shape == 98 || shape == 101 || shape == 102) {
                // a horn, a talon, a spike: colour 1 at its base out to colour 2 at its point
                base = mix(c1, c2, smoothstep(-0.1, 0.45, p.y));
            } else if (shape == 100) {
                // a fossil / spiral shell: a spiral groove carved in both faces (each whorl about half the next, as an
                // ammonite's), each whorl shaded across so the spiral steps show, ribs across the whorls in colour 2, a
                // dark hollow in the middle
                float r = length(p.xy);
                float th = atan(p.y, p.x);
                float s = fract(th / 6.2831853 - log(max(r, 1e-3) / 0.48) * 1.3);
                float groove = 1.0 - smoothstep(0.0, 0.08, min(s, 1.0 - s));
                float rib = smoothstep(0.2, 0.8, 0.5 + 0.5 * sin(th * 18.0));
                base = mix(base, c2, 0.35 * rib * (1.0 - groove));
                base *= (0.72 + 0.28 * s) * (1.0 - 0.6 * groove) * mix(0.35, 1.0, smoothstep(0.03, 0.07, r));
            } else if (shape == 103 && mat == 0) {
                // a toadstool's cap: spots in colour 2
                vec3 d = normalize(p - vec3(0.0, -0.04, 0.0));
                bool spot = spotAt(d, vec3(0.0, 1.0, 0.0), 0.94);
                for (int i = 0; i < 5; ++i) {
                    float a = 0.5 + float(i) * 1.2566;
                    spot = spot || spotAt(d, vec3(0.9 * cos(a), 0.55, 0.9 * sin(a)), 0.95);
                }
                if (spot) base = c2;
                spec = 0.6;
                shine = 50.0;
            } else if (shape == 104) {
                // a lily pad: veins out from its notch in colour 2, waxy
                vec2 q = p.xy - vec2(0.04, 0.0);
                float vein = 1.0 - smoothstep(0.0, 0.06, abs(sin(atan(q.y, q.x) * 4.5)));
                base = mix(base, c2, 0.6 * vein * smoothstep(0.05, 0.15, length(q)));
                spec = 0.6;
                shine = 50.0;
            } else if (shape == 105) {
                // wool: matte and fuzzy
                base *= 0.86 + 0.14 * sin(p.x * 61.0) * sin(p.y * 57.0 + 1.1) * sin(p.z * 63.0 + 0.4);
                spec = 0.05;
                shine = 8.0;
            } else if (shape == 106 || shape == 119 || shape == 120 || shape == 121) {
                // r135 (Lance: the leaves "look like green pills"): a real leaf (106 oval, 119 maple, 120 long blade) and a
                // feather (121), each one thin sheet lit on the side you see: the top waxy, the underside paler and duller
                // with its veins standing out; the leaf's midrib and side veins (the maple's five main veins out from its
                // stem) in colour 2, paler and yellower toward the stem, darker at the very edge, a little light through
                // it; the feather's fine barbs and its fluffy down (colour 2) at the base. The stem / quill (mat 1).
                bool front = dot(N, V) >= 0.0;
                if (!front) N = -N;
                metal = false;
                if (mat == 1) {
                    base = mix(c2, c1, 0.35) * 0.85;
                    spec = 0.15;
                    shine = 16.0;
                } else {
                    float s = clamp(aux - 5.0, -1.0, 1.0);  // across the blade (-1 .. 1), 0 on the midrib
                    float as = abs(s);
                    float along = 0.0, vein = 0.0;
                    if (shape == 119) {
                        vec2 q = p.xy - vec2(0.0, -0.18);
                        float r = length(q);
                        along = clamp(r / 0.68, 0.0, 1.0);
                        float d = 1.0, outAlong = 0.0;  // to the nearest main vein, and how far out along it
                        for (int i = 0; i < 5; ++i) {
                            float a = radians(i == 0 ? 90.0 : (i == 1 ? 30.0 : (i == 2 ? 150.0 : (i == 3 ? -18.0 : 198.0))));
                            vec2 dir = vec2(cos(a), sin(a));
                            float t = dot(q, dir);
                            float di = t > 0.0 ? abs(q.x * dir.y - q.y * dir.x) : r;
                            if (di < d) {
                                d = di;
                                outAlong = t;
                            }
                        }
                        float w = 0.012 * (1.0 - 0.7 * along);
                        float ad = max(fwidth(d), 1e-4);
                        vein = 1.0 - smoothstep(w - ad, w + ad, d);
                        // side veins off each main vein, slanting out toward its lobe's tip
                        float ph = outAlong * 13.0 - d * 9.0;
                        float dv = abs(fract(ph + 0.5) - 0.5);
                        float adv = max(fwidth(ph), 1e-3);
                        vein = max(vein, 0.45 * (1.0 - smoothstep(0.07 - adv, 0.07 + adv, dv)) * smoothstep(0.02, 0.05, d) *
                                             (1.0 - smoothstep(0.16, 0.24, d)) * smoothstep(0.1, 0.2, along));
                        as = along;
                    } else {
                        along = clamp(shape == 120 ? (p.y + 0.42) / 0.92 : (shape == 121 ? (p.y + 0.3) / 0.78 : (p.y + 0.36) / 0.86),
                                      0.0, 1.0);
                        float aa = max(fwidth(s), 1e-3);
                        float mid = 1.0 - smoothstep(0.09 - aa, 0.09 + aa, as);
                        // side veins out from the midrib and on toward the tip (the feather: fine barbs close together)
                        float k = shape == 121 ? 26.0 : (shape == 120 ? 9.0 : 7.0);
                        float ph = along * k - pow(as, 0.85) * (shape == 121 ? 3.0 : 1.4);
                        float dv = abs(fract(ph + 0.5) - 0.5);
                        float wv = shape == 121 ? 0.18 : 0.09 * (1.0 - 0.7 * as);
                        float adv = max(fwidth(ph), 1e-3);
                        float side = (1.0 - smoothstep(wv - adv, wv + adv, dv)) * smoothstep(0.08, 0.16, as) *
                                     (1.0 - smoothstep(0.8, 0.95, as)) * smoothstep(0.03, 0.1, along) *
                                     (1.0 - smoothstep(0.85, 0.97, along));
                        vein = max(mid, (shape == 121 ? 0.35 : 0.6) * side);
                    }
                    vec3 leaf = c1;
                    if (shape == 121) {
                        leaf = mix(c2, c1, smoothstep(0.05, 0.3, along));
                    } else {
                        leaf = mix(c1 * vec3(1.12, 1.08, 0.78), c1, smoothstep(0.0, 0.45, along));
                        leaf *= 1.0 - 0.22 * smoothstep(0.72, 1.0, as);
                    }
                    if (front) {
                        base = mix(leaf, c2, 0.65 * vein);
                        spec = shape == 121 ? 0.12 : 0.35;
                        shine = shape == 121 ? 14.0 : 34.0;
                    } else {
                        float l = dot(leaf, vec3(0.3, 0.59, 0.11));
                        base = mix(leaf, vec3(l), 0.3) * 1.25 + 0.04;
                        base = mix(base, c2 * 1.1, 0.75 * vein);
                        spec = 0.08;
                        shine = 12.0;
                    }
                    // the light through it: lit from the far side (an underside under the sky, a leaf against the
                    // light) it glows in its own colour, as a real leaf does; a feather hardly
                    glow = max(glow, 0.05 + (shape == 121 ? 0.12 : 0.55) * max(dot(-N, kKey), 0.0));
                }
            } else if (shape == 109) {
                // an impact burst: colour 1 in the middle, colour 2 out on its spikes, a little glow
                float d = length(p.xy) - 0.3;
                float aa = max(fwidth(d), 1e-4);
                base = mix(c1, c2, smoothstep(-aa, aa, d));
                glow = max(glow, 0.12);
            } else if (shape == 110 && mat == 0) {
                // a spinning top: stripes of colour 1 and 2 round its body
                float d = sin(p.y * 28.0);
                float aa = max(fwidth(d), 1e-4);
                base = mix(c1, c2, smoothstep(-aa, aa, d));
            } else if (shape == 114) {
                // a lotus: colour 2 at its heart out to colour 1 at the petals' tips; lit on the side you see
                base = mix(c2, c1, smoothstep(0.08, 0.4, length(p.xz)));
                if (dot(N, V) < 0.0) N = -N;
                spec = 0.3;
                shine = 30.0;
            } else if (shape == 115) {
                // lips: glossy, the line between them in colour 2
                float d = abs(p.y - (-0.03 + 0.12 * p.x * p.x)) - 0.012;
                float aa = max(fwidth(d), 1e-4);
                base = mix(c2, base, clamp(smoothstep(-aa, aa, d) + step(0.44, abs(p.x)), 0.0, 1.0));
                spec = 0.9;
                shine = 80.0;
            } else if (shape == 116 || shape == 117) {
                // a heart, a star: glossy
                spec = 0.75;
                shine = 60.0;
            } else if (shape == 118 && abs(p.z) > 0.008) {
                // a dog tag: its hole and three stamped lines
                float lines = 1.0;
                for (int i = 0; i < 3; ++i) {
                    lines = min(lines, sdBox2(p.xy - vec2(-0.02, 0.06 - 0.1 * float(i)), vec2(0.12 - 0.02 * float(i), 0.012)));
                }
                float aa = max(fwidth(lines), 1e-4);
                base *= 1.0 - 0.35 * (1.0 - smoothstep(-aa, aa, lines));
                if (length(p.xy - vec2(0.0, 0.22)) < 0.035) base = vec3(0.02);
            // r135: the r135 roster's light pieces (xr_fx_mesh.h r135)
            } else if (shape == 122) {
                // a fish: wet and glossy, its belly paler (colour 2), faint scales, a dark eye with a glint each side
                if (mat == 0) {
                    base = mix(mix(c1, c2, 0.75), c1, smoothstep(-0.12, 0.04, p.y));
                    base *= 0.93 + 0.07 * sin(p.x * 70.0 + abs(p.y) * 40.0) * sin(p.y * 70.0);
                    vec2 e = vec2(p.x - 0.29, p.y - 0.035);
                    if (abs(p.z) > 0.02 && length(e) < 0.036) base = length(e - vec2(0.01, 0.012)) < 0.011 ? vec3(0.95) : vec3(0.02);
                }
                spec = 0.8;
                shine = 70.0;
            } else if (shape == 123) {
                // a screw: the thread wound round its shank, a cross slot in its head
                if (p.y < 0.3 && p.y > -0.42) {
                    float th = sin((p.y * 9.0 + atan(p.z, p.x) / 6.2831853) * 6.2831853);
                    base *= 0.68 + 0.32 * smoothstep(-0.4, 0.4, th);
                }
                if (p.y > 0.44 && ((abs(p.x) < 0.025 && abs(p.z) < 0.17) || (abs(p.z) < 0.025 && abs(p.x) < 0.17))) base *= 0.2;
            } else if (shape == 124) {
                // a horseshoe magnet: painted (colour 1), its poles bare metal (colour 2)
                if (p.y < -0.27) {
                    base = c2;
                    metal = true;
                } else {
                    spec = 0.7;
                    shine = 60.0;
                }
            } else if (shape == 125) {
                // a crab's pincer: colour 1 out to colour 2 at the fingers' tips, knobbly, glossy
                base = mix(c1, c2, smoothstep(0.12, 0.42, p.y));
                base *= 0.9 + 0.1 * sin(p.x * 50.0) * sin(p.y * 47.0);
                spec = 0.6;
                shine = 50.0;
            } else if (shape == 126) {
                // a scallop shell: lit on the side you see, ribs fanning out from its hinge, growth bands in colour 2
                if (dot(N, V) < 0.0) N = -N;
                vec2 q = p.xy - vec2(0.0, -0.32);
                float rib = 0.5 + 0.5 * cos(atan(q.y, q.x) * 18.0);
                base = mix(c1, c2, 0.55 * smoothstep(0.35, 0.65, sin(length(q) * 24.0)));
                base *= 0.78 + 0.22 * rib;
                spec = 0.5;
                shine = 40.0;
            } else if (shape == 127) {
                // a curling wave: deep at its foot, lighter up its face, foam (colour 2) on its crest and lip
                base = mix(c1 * 0.7, c1 * 1.15, smoothstep(-0.45, 0.2, p.y));
                float foam = smoothstep(0.24, 0.36, p.y + 0.35 * max(p.x - 0.1, 0.0));
                base = mix(base, c2, foam * (0.75 + 0.25 * sin(p.x * 61.0 + p.y * 37.0)));
                spec = 0.85;
                shine = 70.0;
            } else if (shape == 128) {
                // a pixel cube (Porygon's): each face 3 x 3 pixels in its two colours, dark seams, a little glow
                vec3 ap = abs(p);
                vec2 f = ap.x >= ap.y && ap.x >= ap.z ? p.yz : (ap.y >= ap.z ? p.xz : p.xy);
                vec2 cell = floor((f + 0.3) / 0.2);
                base = mix(c1, c2, mod(cell.x + cell.y, 2.0));
                vec2 g = abs(fract((f + 0.3) / 0.2) - 0.5);
                base *= 1.0 - 0.6 * smoothstep(0.42, 0.48, max(g.x, g.y));
                glow = max(glow, 0.15);
                spec = 0.8;
                shine = 70.0;
            } else if (shape == 129) {
                // a light egg: spots in colour 2 (none to see when it is colour 1)
                vec3 d = normalize(p / vec3(0.36, 0.5, 0.36));
                if (spotAt(d, vec3(0.0, 0.85, 0.52), 0.94) || spotAt(d, vec3(0.8, 0.3, 0.52), 0.93) ||
                    spotAt(d, vec3(-0.7, 0.2, -0.68), 0.93) || spotAt(d, vec3(0.3, -0.5, -0.8), 0.94) ||
                    spotAt(d, vec3(-0.5, -0.6, 0.62), 0.93) || spotAt(d, vec3(0.9, -0.35, -0.25), 0.95)) {
                    base = c2;
                }
                spec = 0.55;
                shine = 45.0;
            } else if (shape == 130) {
                // a shuriken: a hole in its middle, honed bright toward its points
                float r = length(p.xy);
                base *= 0.75 + 0.45 * smoothstep(0.22, 0.48, r);
                if (r < 0.055) base = vec3(0.02);
            } else if (shape == 131) {
                // a lab flask: clear glass (colour 1, shining) with a glowing potion (colour 2) in its bottom
                if (p.y < -0.16) {
                    base = c2;
                    glow = max(glow, 0.35);
                    spec = 0.6;
                } else {
                    base = mix(c1, vec3(1.0), 0.3);
                    spec = 0.95;
                    shine = 90.0;
                }
                if (abs(p.y + 0.16) < 0.012) base = mix(c2, vec3(1.0), 0.5);
            } else if (shape == 132) {
                // a bullet: a groove round its casing
                if (abs(p.y + 0.4) < 0.015) base *= 0.45;
            } else if (shape == 133) {
                // a brick chunk: rough clay with old mortar (colour 2) along its edges
                vec3 ap = abs(p) / vec3(0.45, 0.14, 0.22);
                if (step(0.88, ap.x) + step(0.8, ap.y) + step(0.85, ap.z) >= 2.0) base = mix(base, c2, 0.8);
            }
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
