#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    // Fullscreen triangle
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.9999, 1.0);
}