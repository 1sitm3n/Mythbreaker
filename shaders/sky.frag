#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec3 cameraPos;
    float time;
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float ambientIntensity;
    vec3 skyColorTop;
    float padding1;
    vec3 skyColorBottom;
    float padding2;
} camera;

void main() {
    // Reconstruct view ray from screen coordinates
    vec2 ndc = fragUV * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, 1.0, 1.0);
    mat4 invViewProj = inverse(camera.viewProj);
    vec4 worldPos = invViewProj * clipPos;
    vec3 viewDir = normalize(worldPos.xyz / worldPos.w - camera.cameraPos);
    
    // Sky gradient based on view direction
    float t = viewDir.y * 0.5 + 0.5;
    t = clamp(t, 0.0, 1.0);
    vec3 skyColor = mix(camera.skyColorBottom, camera.skyColorTop, t);
    
    // Sun disc
    float sunDot = dot(viewDir, -camera.sunDirection);
    float sunDisc = smoothstep(0.995, 0.999, sunDot);
    vec3 sunGlow = camera.sunColor * pow(max(sunDot, 0.0), 64.0) * 0.5;
    
    skyColor += sunDisc * camera.sunColor * camera.sunIntensity;
    skyColor += sunGlow;
    
    outColor = vec4(skyColor, 1.0);
}