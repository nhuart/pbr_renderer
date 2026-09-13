#version 450

layout(binding = 0) uniform SkyboxUBO {
    mat4 invProj;
    mat4 invView;
} ubo;

layout(location = 0) out vec3 fragDir;

void main() {
    // Fullscreen triangle: vertex 0,1,2 cover the whole screen
    // gl_VertexID 0 -> x = -1.0, y = -1.0
    // gl_VertexID 1 -> x =  3.0, y = -1.0
    // gl_VertexID 2 -> x = -1.0, y =  3.0
    vec2 pos = vec2((gl_VertexIndex & 1) * 4.0 - 1.0,
                    (gl_VertexIndex & 2) * 2.0 - 1.0);

    // Reconstruct view-space ray from NDC
    vec4 viewPos = ubo.invProj * vec4(pos, 1.0, 1.0);
    viewPos /= viewPos.w;

    // Transform to world space, ignore translation (w=0)
    fragDir = (ubo.invView * vec4(viewPos.xyz, 0.0)).xyz;

    // Put vertex at far plane (z=w so NDC z=1.0)
    gl_Position = vec4(pos, 1.0, 1.0);
}
