// strikers-quest r61: the walkable cup suite (xr_fx.cpp render_room): the baked room and the world
// outside it, textured triangles placed by one matrix (the suite's place in your room, then the eye).
// r62: with each vertex's normal and shine (gloss, metal), for the live highlights (xr_fx_room.frag).
#version 450

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 eye;    // the eye in the room's own space; w = seconds
    vec4 light;  // the room's lamp colour
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inNrm;
layout(location = 3) in vec2 inShine;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 pos;
layout(location = 2) out vec3 nrm;
layout(location = 3) out vec2 shine;

void main() {
    uv = inUv;
    pos = inPos;
    nrm = inNrm;
    shine = inShine;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}
