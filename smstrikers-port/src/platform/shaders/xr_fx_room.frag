// strikers-quest r61: the baked light as it is, solid.
// r62 (Lance: "i want shit more shiny"): plus the shine the bake leaves out, live, so it moves as you
// do: the four ceiling lamps' highlights, the window's, a soft reflection of the room, all through
// Fresnel (glancing looks shine more); metals take their own colour into it. Room space: x right, y up
// from the floor, z back (-z the window), as the room's vertices are. Keep the lamps in step with
// tools/suites/suite_scene.py (ceiling lights at x +-2.6, y -2.2 / 0.6 there). shine.x = gloss 0..1,
// shine.y = metal; the sky dome has none and keeps its picture.
#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 eye;
    vec4 light;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 pos;
layout(location = 2) in vec3 nrm;
layout(location = 3) in vec2 shine;
layout(location = 0) out vec4 outColor;

const vec3 kLamps[4] = vec3[](vec3(-2.6, 3.36, -0.6), vec3(2.6, 3.36, -0.6), vec3(-2.6, 3.36, 2.2), vec3(2.6, 3.36, 2.2));

void main() {
    vec3 base = texture(tex, uv).rgb;
    float gloss = shine.x;
    if (gloss < 0.01) {
        outColor = vec4(base, 1.0);
        return;
    }
    float metal = shine.y;
    vec3 N = normalize(nrm);
    vec3 V = normalize(pc.eye.xyz - pos);
    if (dot(N, V) < 0.0) N = -N;  // thin things shine on the side you see
    float nv = max(dot(N, V), 0.0);
    float shin = mix(12.0, 700.0, gloss * gloss);
    float norm = (shin + 8.0) / 25.13;  // energy-conserving Blinn-Phong: (n + 8) / 8 pi
    vec3 F0 = mix(vec3(0.08), base, metal);  // lacquered: a Pixar toy shine, twice plain plastic's
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - nv, 5.0);

    vec3 spec = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec3 Lv = kLamps[i] - pos;
        float d2 = dot(Lv, Lv);
        vec3 L = Lv * inversesqrt(d2);
        vec3 H = normalize(L + V);
        spec += pc.light.rgb * (pow(max(dot(N, H), 0.0), shin) * norm * max(dot(N, L), 0.0) * 7.0 / (1.0 + d2));
    }
    // The window: daylight (or the stadium's floodlights) from ahead, a little above.
    vec3 W = normalize(vec3(0.0, 0.3, -1.0));
    vec3 Hw = normalize(W + V);
    spec += vec3(0.85, 0.9, 1.0) * (pow(max(dot(N, Hw), 0.0), shin * 0.5) * norm * 0.35 * max(dot(N, W), 0.0));
    // A soft reflection of the room: its own colour below, the lamps' warmth above.
    vec3 R = reflect(-V, N);
    vec3 env = mix(base * 0.35, pc.light.rgb * 0.7, smoothstep(-0.25, 0.75, R.y));

    vec3 col = base * (1.0 - 0.45 * metal * gloss) + F * (spec + env * gloss) * mix(1.0, 1.8, metal);
    outColor = vec4(col, 1.0);
}
