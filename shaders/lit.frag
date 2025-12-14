#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragNormal;

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

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 albedo = texColor.rgb * fragColor;
    
    // Normalize the interpolated normal
    vec3 N = normalize(fragNormal);
    vec3 L = -camera.sunDirection;
    
    // Diffuse lighting
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * camera.sunColor * NdotL * camera.sunIntensity;
    
    // Ambient lighting (sky color influence)
    vec3 ambient = albedo * mix(camera.skyColorBottom, camera.skyColorTop, 0.5) * camera.ambientIntensity;
    
    vec3 color = ambient + diffuse;
    
    // Distance fog
    float dist = length(fragWorldPos - camera.cameraPos);
    float fogFactor = 1.0 - exp(-0.008 * dist);
    fogFactor = clamp(fogFactor, 0.0, 0.85);
    vec3 fogColor = mix(camera.skyColorBottom, camera.skyColorTop, 0.3);
    color = mix(color, fogColor, fogFactor);
    
    outColor = vec4(color, texColor.a);
}