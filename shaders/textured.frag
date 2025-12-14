#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec3 cameraPos;
    float time;
} camera;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    // Sample texture
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Combine with vertex color
    vec3 color = texColor.rgb * fragColor;
    
    // Distance fog
    float dist = length(fragWorldPos - camera.cameraPos);
    float fogFactor = 1.0 - exp(-0.015 * dist);
    fogFactor = clamp(fogFactor, 0.0, 0.8);
    vec3 fogColor = vec3(0.05, 0.05, 0.08);
    
    color = mix(color, fogColor, fogFactor);
    
    outColor = vec4(color, texColor.a);
}