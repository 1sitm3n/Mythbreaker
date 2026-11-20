#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(set=1,binding=0) uniform sampler2D sceneImage;
layout(constant_id=0) const bool outputIsSrgb=true;
vec3 linearToSrgb(vec3 value) {
    return mix(value*12.92,1.055*pow(max(value,vec3(0)),vec3(1.0/2.4))-0.055,greaterThan(value,vec3(0.0031308)));
}
void main() {
    outColor=texture(sceneImage,uv);
    if(!outputIsSrgb) outColor.rgb=linearToSrgb(outColor.rgb);
}
