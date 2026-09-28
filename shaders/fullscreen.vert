#version 450

// Generates a full-screen triangle without vertex buffer input.
// gl_VertexIndex: 0=(−1,−1), 1=(3,−1), 2=(−1,3)  → UV (0,0),(2,0),(0,2)
layout(location = 0) out vec2 outUV;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
