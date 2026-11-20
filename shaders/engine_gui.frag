#version 450
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D image;
layout(constant_id=0) const bool outputIsSrgb=true;
vec3 srgbToLinear(vec3 value) {
    return mix(value/12.92,pow((value+0.055)/1.055,vec3(2.4)),greaterThan(value,vec3(0.04045)));
}
vec3 linearToSrgb(vec3 value) {
    return mix(value*12.92,1.055*pow(max(value,vec3(0)),vec3(1.0/2.4))-0.055,greaterThan(value,vec3(0.0031308)));
}
void main() {
    // ImGui style/vertex colors are authored in sRGB. Decode them before
    // multiplying by the linear sampled scene and blending into an sRGB target.
    outColor=texture(image,uv)*vec4(srgbToLinear(color.rgb),color.a);
    if(!outputIsSrgb) outColor.rgb=linearToSrgb(outColor.rgb);
}
