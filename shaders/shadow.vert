#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(set = 0, binding = 0) uniform LightSpaceUBO {
    mat4 lightSpaceMatrix;
} light;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

void main() {
    gl_Position = light.lightSpaceMatrix * push.model * vec4(inPosition, 1.0);
}