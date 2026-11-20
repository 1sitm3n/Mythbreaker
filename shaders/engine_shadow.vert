#version 450
layout(location=0) in vec3 inPosition;
layout(set=0,binding=0) uniform Frame {
    mat4 viewProjection;
    mat4 lightViewProjection;
} frame;
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
    vec4 material;
} object;
void main() { gl_Position=frame.lightViewProjection*object.model*vec4(inPosition,1.0); }
