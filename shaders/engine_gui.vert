#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in vec4 vertexColor;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 color;
layout(push_constant) uniform Transform { vec2 scale; vec2 translate; } transform;
void main() {
    uv=texCoord;
    color=vertexColor;
    gl_Position=vec4(position*transform.scale+transform.translate,0.0,1.0);
}
