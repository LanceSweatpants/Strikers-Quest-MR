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
void main() {
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
