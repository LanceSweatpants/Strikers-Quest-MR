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
    // r199: the TIZM SPORTSBOOK cabinet (glowRgb.w -9, baked, its own space: x right, y up, z to the player; the normals'
    // length says what a vertex is - tools/outside/sportsbook199/cabinet199.py): its buttons sink 6 mm while pressed
    // (glowRgb.r = the pressed bits x 1/255: A B X Y START) and its joystick tilts with your left stick (look.x right,
    // look.z up), up to 19 degrees round its pivot on the panel (ALL-22's cabinet's way)
    if (pc.glowRgb.w < -8.5 && pc.glowRgb.w > -9.5) {
        float code = length(inNrm);
        const vec3 kPanelN = vec3(0.0, 0.9806, 0.1961);
        const vec3 kPivot = vec3(-0.17, 0.975, 0.425);
        if (code > 4.95 && code < 5.45) {
            int k = int(floor((code - 5.0) * 10.0 + 0.5));
            int bits = int(pc.glowRgb.r * 255.0 + 0.5);
            if (((bits >> k) & 1) != 0) p -= kPanelN * 0.006;
        } else if (code > 5.9 && code < 6.1) {
            vec2 st = vec2(pc.look.x, pc.look.z);
            float m = min(1.0, length(st));
            if (m > 0.02) {
                vec3 tilt = normalize(vec3(st.x, 0.0, -st.y));  // (right: +x; up / away from you: -z)
                vec3 axis = normalize(cross(kPanelN, tilt));
                float a = 0.33 * m, c = cos(a), s = sin(a);
                vec3 r = p - kPivot;
                r = r * c + cross(axis, r) * s + axis * dot(axis, r) * (1.0 - c);
                p = kPivot + r;
            }
        }
    }
    vec3 nrm = inNrm;
    // r202: the SLOTS handle (codes 13 chrome / 14 its ball; lever_onto_r199.py / cabinet202.py: built round its hub, the arm up):
    // pops out of its port on the right side (grows, slides, flips up) while SLOTS is up, pulled down toward you - look.w carries xr_fx's shine (how far out, in
    // hundredths) and dim (its angle: 0..1 = -90..+100 degrees, + toward you)
    if (pc.glowRgb.w < -8.5 && pc.glowRgb.w > -9.5) {
        float code = length(inNrm);
        if (code > 12.9 && code < 14.1) {
            float w = pc.look.w;
            float outK = floor(mod(w, 512.0) * 0.5) / 100.0;
            float ang = radians(-90.0 + 190.0 * floor(w / 65536.0) / 100.0);
            float slide = smoothstep(0.0, 0.55, outK);
            float grow = smoothstep(0.0, 0.75, outK) + 0.12 * sin(3.14159 * clamp((outK - 0.5) * 2.0, 0.0, 1.0));  // (a pop)
            float c = cos(ang), s = sin(ang);
            p = vec3(p.x, p.y * c - p.z * s, p.y * s + p.z * c) * grow + vec3(mix(0.33, 0.40, slide), 1.00, -0.22);
            nrm = vec3(nrm.x, nrm.y * c - nrm.z * s, nrm.y * s + nrm.z * c);
        }
    }
    vPos = p;
    vNrm = nrm;
    gl_Position = pc.viewProj * vec4(p, 1.0);
}
