#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

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
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(-camera.sunDirection);
    
    // Diffuse lighting
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * camera.sunColor * camera.sunIntensity;
    
    // Ambient
    vec3 ambient = camera.sunColor * camera.ambientIntensity;
    
    // Sample texture
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 baseColor = fragColor * texColor.rgb;
    
    // Final color
    vec3 finalColor = baseColor * (ambient + diffuse);
    
    outColor = vec4(finalColor, 1.0);
}