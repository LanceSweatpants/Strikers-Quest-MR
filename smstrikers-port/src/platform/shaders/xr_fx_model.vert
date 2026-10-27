// strikers-quest r96: a character standing in your room (the 3D evolution, xr_evo.cpp): its own
// triangles, posed and placed on the CPU (LOCAL space, metres), with the pictures of its pack.
// xr_fx.cpp draw_models; the same eye matrix as the pieces.
#version 450

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 camPos;  // the eye; w = seconds
    vec4 look;    // x white (0 its colours .. 1 the evolution's white), y alpha, z glow, w soft part
    vec4 glowRgb; // the glow's colour; w = the light from below (the theme's)
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vPos;
layout(location = 2) out vec3 vNrm;

void main() {
    vUv = inUv;
    vPos = inPos;
    vNrm = inNrm;
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
}
