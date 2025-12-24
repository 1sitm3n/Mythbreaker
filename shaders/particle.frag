#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in float fragRotation;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 center = fragUV - 0.5;
    float dist = length(center) * 2.0;
    float alpha = 1.0 - smoothstep(0.0, 1.0, dist);
    float glow = exp(-dist * 3.0) * 0.5;
    
    outColor = vec4(fragColor.rgb + glow, fragColor.a * alpha);
    
    if (outColor.a < 0.01) discard;
}