#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in ivec4 inJointIndices;
layout(location = 5) in vec4 inJointWeights;

// Camera UBO
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

// Joint matrices UBO
layout(set = 0, binding = 2) uniform JointMatricesUBO {
    mat4 jointMatrices[128];
} joints;

// Model transform
layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    // GPU Skinning: blend joint transforms weighted by vertex weights
    mat4 skinMatrix = 
        inJointWeights.x * joints.jointMatrices[inJointIndices.x] +
        inJointWeights.y * joints.jointMatrices[inJointIndices.y] +
        inJointWeights.z * joints.jointMatrices[inJointIndices.z] +
        inJointWeights.w * joints.jointMatrices[inJointIndices.w];
    
    // Apply skinning to position and normal
    vec4 skinnedPos = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;
    
    // Apply model transform
    vec4 worldPos = push.model * skinnedPos;
    
    gl_Position = camera.viewProj * worldPos;
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragNormal = normalize(mat3(push.model) * skinnedNormal);
    fragWorldPos = worldPos.xyz;
}
