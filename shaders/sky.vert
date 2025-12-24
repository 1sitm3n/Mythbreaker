#version 450

layout(location = 0) out vec3 fragDir;

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
    // Generate fullscreen triangle
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.9999, 1.0);
    
    // Compute view direction from screen position
    vec4 clipPos = vec4(pos, 1.0, 1.0);
    mat4 invProj = inverse(camera.proj);
    mat4 invView = inverse(camera.view);
    vec4 viewPos = invProj * clipPos;
    viewPos /= viewPos.w;
    fragDir = (invView * vec4(viewPos.xyz, 0.0)).xyz;
}