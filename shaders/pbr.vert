#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

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
    float fogDensity;
    vec3 fogColor;
    float lightningFlash;
    float weatherAmbient;
    float padding1;
    float padding2;
    float padding3;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec3 fragColor;
layout(location = 5) out mat3 fragTBN;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = camera.viewProj * worldPos;

    fragWorldPos = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragPosLightSpace = camera.lightSpaceMatrix * worldPos;

    mat3 normalMatrix = mat3(push.model);
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;

    fragNormal = N;
    fragTBN = mat3(T, B, N);
}
