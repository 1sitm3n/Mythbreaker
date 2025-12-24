#version 450

layout(location = 0) in vec3 fragDir;
layout(location = 0) out vec4 outColor;

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

void main() {
    vec3 dir = normalize(fragDir);
    
    // Gradient from horizon to zenith based on Y
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyColor = mix(camera.skyColorBottom, camera.skyColorTop, pow(t, 0.7));
    
    // Sun disc
    float sunDot = dot(dir, -camera.sunDirection);
    if (sunDot > 0.995) {
        float sunDisc = smoothstep(0.995, 0.999, sunDot);
        vec3 sunCol = vec3(1.0, 0.95, 0.8) * camera.sunIntensity * 2.0;
        skyColor = mix(skyColor, sunCol, sunDisc);
    } else if (sunDot > 0.9) {
        float glow = smoothstep(0.9, 0.995, sunDot) * 0.4;
        vec3 glowColor = vec3(1.0, 0.7, 0.4) * camera.sunIntensity;
        skyColor += glowColor * glow;
    }
    
    // Stars at night (when sun intensity is low)
    if (camera.sunIntensity < 0.25) {
        float starIntensity = 1.0 - camera.sunIntensity / 0.25;
        float stars = fract(sin(dot(floor(dir * 400.0), vec3(12.9898, 78.233, 45.164))) * 43758.5453);
        if (stars > 0.997) {
            skyColor += vec3(starIntensity * (stars - 0.997) * 300.0);
        }
    }
    
    // Lightning flash
    skyColor += skyColor * camera.lightningFlash * 0.5;
    
    outColor = vec4(skyColor, 1.0);
}