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
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vPos;
layout(location = 2) in vec3 vNrm;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 t = texture(uTex, vUv);
    bool soft = pc.look.w > 0.5;
    if (!soft && t.a < 0.3) discard;
    vec3 v = normalize(pc.camPos.xyz - vPos);
    vec3 n = vNrm;
    float nl = length(n);
    n = nl > 1e-5 ? n / nl : v;
    if (dot(n, v) < 0.0) n = -n;  // both sides of a sheet (wings, ears) lit as the side you see
    vec3 key = normalize(vec3(0.35, 0.85, 0.4));
    float diff = max(dot(n, key), 0.0);
    float wrap = max(dot(n, -key) * 0.5 + 0.5, 0.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 lit = t.rgb * (0.48 + 0.62 * diff + 0.12 * wrap);
    lit += t.rgb * pc.glowRgb.rgb * pc.glowRgb.w * max(-n.y, 0.0);  // the theme's light from below
    lit += rim * mix(vec3(0.25), pc.glowRgb.rgb, 0.6) * (0.35 + pc.look.z);
    // the evolution's white: flat bright light, a blue-white rim
    vec3 white = vec3(1.0) * (0.9 + 0.1 * diff) + rim * vec3(0.55, 0.75, 1.0);
    vec3 col = mix(lit, white, clamp(pc.look.x, 0.0, 1.0));
    col += pc.glowRgb.rgb * pc.look.z * (0.25 + 0.75 * rim);
    float a = (soft ? t.a : 1.0) * pc.look.y;
    outColor = vec4(col * a, a);
}
