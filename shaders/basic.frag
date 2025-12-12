#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec3 cameraPos;
    float time;
} camera;

layout(location = 0) out vec4 outColor;

void main() {
    float dist = length(fragWorldPos - camera.cameraPos);
    float fogFactor = clamp(dist / 100.0, 0.0, 1.0);
    vec3 fogColor = vec3(0.05, 0.05, 0.1);
    vec3 finalColor = mix(fragColor, fogColor, fogFactor);
    outColor = vec4(finalColor, 1.0);
}
