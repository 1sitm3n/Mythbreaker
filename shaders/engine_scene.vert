#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;
layout(set=0,binding=0) uniform Frame {
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraExposure;
    vec4 ambient;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 pointPosition[8];
    vec4 pointColor[8];
    ivec4 counts;
} frame;
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
    vec4 material;
} object;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 uv;
layout(location=3) out vec4 color;
void main() {
    vec4 world = object.model * vec4(inPosition,1.0);
    worldPosition = world.xyz;
    normal = normalize(transpose(inverse(mat3(object.model))) * inNormal);
    uv = inUV;
    color = inColor * object.color;
    gl_Position = frame.viewProjection * world;
}
