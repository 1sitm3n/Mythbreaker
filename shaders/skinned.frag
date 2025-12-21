#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec4 fragPosLightSpace;

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
    float padding2;
} camera;

layout(set = 0, binding = 1) uniform sampler2D texSampler;
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap;

float calculateShadow(vec4 posLightSpace) {
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;
    }
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec3 sampleCoords = vec3(projCoords.xy + vec2(x, y) * texelSize, projCoords.z - camera.shadowBias);
            shadow += texture(shadowMap, sampleCoords);
        }
    }
    shadow /= 9.0;
    
    return 1.0 - shadow;
}

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 baseColor = texColor.rgb * fragColor;
    
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-camera.sunDirection);
    
    float NdotL = max(dot(N, L), 0.0);
    float shadow = calculateShadow(fragPosLightSpace);
    
    vec3 diffuse = baseColor * camera.sunColor * camera.sunIntensity * NdotL * (1.0 - shadow);
    vec3 ambient = baseColor * camera.ambientIntensity;
    
    float dist = length(camera.cameraPos - fragWorldPos);
    float fogFactor = clamp(exp(-dist * 0.01), 0.0, 1.0);
    vec3 fogColor = mix(camera.skyColorBottom, camera.skyColorTop, 0.5);
    
    vec3 finalColor = mix(fogColor, ambient + diffuse, fogFactor);
    
    outColor = vec4(finalColor, texColor.a);
}