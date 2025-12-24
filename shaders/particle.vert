#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float time;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inSize;
layout(location = 3) in float inRotation;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out float fragRotation;

void main() {
    vec3 cameraRight = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 cameraUp = vec3(view[0][1], view[1][1], view[2][1]);
    
    vec2 quadOffsets[6] = vec2[](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5,  0.5)
    );
    
    vec2 offset = quadOffsets[gl_VertexIndex % 6];
    
    float c = cos(inRotation);
    float s = sin(inRotation);
    vec2 rotatedOffset = vec2(
        offset.x * c - offset.y * s,
        offset.x * s + offset.y * c
    );
    
    vec3 worldPos = inPosition + 
                    cameraRight * rotatedOffset.x * inSize +
                    cameraUp * rotatedOffset.y * inSize;
    
    gl_Position = proj * view * vec4(worldPos, 1.0);
    
    fragColor = inColor;
    fragUV = offset + 0.5;
    fragRotation = inRotation;
}