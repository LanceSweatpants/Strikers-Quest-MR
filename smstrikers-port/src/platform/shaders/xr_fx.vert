// strikers-quest: goal celebration effects (src/platform/xr_fx.cpp). One instanced quad each for
// what is not a 3D piece (xr_fx_mesh.vert): firework sparks face the eye (zero quaternion), the
// marker too, and the game's screen flash covers the view. Compile with tools/compile-xr-shaders.ps1.
#version 450

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 camRight;
    vec4 camUp;
} pc;

layout(location = 0) in vec4 inPosSize;  // xyz LOCAL-space meters, w size (height) in meters
layout(location = 1) in vec4 inQuat;     // orientation; all zero = face the eye
layout(location = 2) in vec4 inColor1;   // sRGB unorm8x4: front / main colour
layout(location = 3) in vec4 inColor2;   // sRGB unorm8x4: back / detail colour
layout(location = 4) in vec4 inParams;   // shape id, aspect (width / height), alpha, glow (0..1)

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor1;
layout(location = 2) out vec4 vColor2;
layout(location = 3) out vec4 vParams;
layout(location = 4) out float vShade;
layout(location = 5) out vec4 vQuat;   // r168: the ball skins' preview ball (xr_fx.frag 160-162): its turn
layout(location = 6) out vec3 vRight;  //        and your eye's right / up
layout(location = 7) out vec3 vUp;

vec3 qrot(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main() {
    vec2 corner = vec2((gl_VertexIndex & 1) != 0 ? 1.0 : -1.0, (gl_VertexIndex & 2) != 0 ? 1.0 : -1.0);
    vUv = corner * 0.5 + 0.5;
    vec2 local = corner * vec2(inParams.y, 1.0) * 0.5 * inPosSize.w;
    vec3 world;
    // How squarely the piece faces the light, 0..1 (the fragment shader turns it into shading,
    // and into a glint for the metallic confetti).
    float shade = 1.0;
    vQuat = inQuat;
    vRight = pc.camRight.xyz;
    vUp = pc.camUp.xyz;
    int shapeId = int(inParams.x + 0.5);
    if (shapeId == 19) {
        // The game's screen flash across the whole view: the quad is the eye's clip square.
        gl_Position = vec4(corner, 0.0, 1.0);
        vColor1 = inColor1;
        vColor2 = inColor2;
        vParams = inParams;
        vShade = 1.0;
        return;
    }
    // r168: the preview ball faces your eye whatever its turn (its quaternion turns its surface, in xr_fx.frag)
    if (dot(inQuat, inQuat) < 0.5 || (shapeId >= 160 && shapeId <= 162)) {
        world = inPosSize.xyz + pc.camRight.xyz * local.x + pc.camUp.xyz * local.y;
    } else {
        world = inPosSize.xyz + qrot(inQuat, vec3(local, 0.0));
        vec3 n = qrot(inQuat, vec3(0.0, 0.0, 1.0));
        shade = abs(dot(n, normalize(vec3(0.3, 1.0, 0.4))));
    }
    // r214: the snow layer / footprints (xr_fx.frag 180, 182) read their spot in your room (LOCAL x, z, y): its surface's
    // noise lies in the world, so it runs on unbroken from one tile to the next
    if (shapeId == 180 || shapeId == 182) vQuat = vec4(world.x, world.z, world.y, 0.0);
    gl_Position = pc.viewProj * vec4(world, 1.0);
    vColor1 = inColor1;
    vColor2 = inColor2;
    vParams = inParams;
    vShade = shade;
}
