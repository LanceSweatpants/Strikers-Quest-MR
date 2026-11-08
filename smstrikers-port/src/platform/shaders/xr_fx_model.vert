// strikers-quest r96: a character standing in your room (the 3D evolution, xr_evo.cpp): its own
// triangles, posed and placed on the CPU (LOCAL space, metres), with the pictures of its pack.
// xr_fx.cpp draw_models; the same eye matrix as the pieces.
#version 450

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 camPos;  // the eye; w = seconds
    vec4 look;    // x white (0 its colours .. 1 the evolution's white), y alpha, z glow, w soft part
    vec4 glowRgb; // the glow's colour; w = the light from below (the theme's)
    vec4 up;      // r142: the room's up in the model's own frame (xr_fx_model.frag)
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vPos;
layout(location = 2) out vec3 vNrm;

void main() {
    vUv = inUv;
    vec3 p = inPos;
    // r163 (Lance: every special skin "moving and alive"; the binder's poses are baked now, nothing moves on the CPU for a
    // furless skin): the binder (glowRgb.w -5.5, its own frame, metres at size 1; up.w = its skin's index + 1) - slime's
    // cover wobbling (~0.7 mm), the puffer's breathing (0.5 mm): its skin's vertices (normals 1 long) on its outside (facing
    // away from its middle, shut) along their normals. tools/outside/binders163/sim163.py cover_move: the same.
    if (pc.glowRgb.w < -5.0 && pc.glowRgb.w > -6.0) {
        float nl = length(inNrm);
        int si = int(pc.up.w + 0.5) - 1;
        if (nl > 0.5 && nl < 1.5 && (si == 32 || si == 15)) {
            vec3 n = inNrm / nl;
            if (dot(n, p - vec3(0.1375, 0.0, 0.0)) > 0.0) {
                float t = pc.camPos.w;
                float amp = si == 32 ? 0.0007 * (sin(p.x * 45.0 + t * 1.6) * sin(p.y * 38.0 - t * 1.1) * 0.6 +
                                                 sin((p.x + p.y) * 70.0 + t * 2.3) * 0.4)
                                     : 0.0005 * (0.5 + 0.5 * sin(t * 1.1));
                p += n * amp;
            }
        }
    }
    vPos = p;
    vNrm = inNrm;
    gl_Position = pc.viewProj * vec4(p, 1.0);
}
