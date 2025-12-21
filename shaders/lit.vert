#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 lightSpaceMatrix;
    vec3 cameraPos;
    float time;
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float ambientIntensity;
    vec3 skyColorTop;
    float shadowBias;
    vec3 skyColorBottom;
    float padding2;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec4 fragPosLightSpace;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = camera.viewProj * worldPos;
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(push.model) * inNormal;
    fragWorldPos = worldPos.xyz;
    fragPosLightSpace = camera.lightSpaceMatrix * worldPos;
}