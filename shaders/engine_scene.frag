#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 color;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform Frame {
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraExposure;
    vec4 ambient;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 pointPosition[8];
    vec4 pointColor[8];
    ivec4 counts;
} frame;
layout(set=0,binding=1) uniform sampler2DShadow shadowMap;
layout(set=1,binding=0) uniform sampler2D albedoMap;
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
    vec4 material;
} object;
const float PI=3.14159265359;
vec3 brdf(vec3 N,vec3 V,vec3 L,vec3 radiance,vec3 albedo,float metallic,float roughness) {
    float NdotL=max(dot(N,L),0.0),NdotV=max(dot(N,V),0.001);
    vec3 H=normalize(V+L);
    float NdotH=max(dot(N,H),0.0),HdotV=max(dot(H,V),0.0);
    float a=roughness*roughness,a2=a*a;
    float denominator=NdotH*NdotH*(a2-1.0)+1.0;
    float D=a2/max(PI*denominator*denominator,0.00001);
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float G=(NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k));
    vec3 F0=mix(vec3(0.04),albedo,metallic);
    vec3 F=F0+(1.0-F0)*pow(1.0-HdotV,5.0);
    vec3 specular=D*G*F/max(4.0*NdotV*NdotL,0.0001);
    vec3 diffuse=(1.0-F)*(1.0-metallic)*albedo/PI;
    return (diffuse+specular)*radiance*NdotL;
}
float sunVisibility(vec3 N,vec3 L) {
    if(frame.counts.y==0) return 1.0;
    vec4 clip=frame.lightViewProjection*vec4(worldPosition,1.0);
    vec3 coordinate=clip.xyz/clip.w;
    coordinate.xy=coordinate.xy*0.5+0.5;
    if(coordinate.z<=0.0||coordinate.z>=1.0||any(lessThan(coordinate.xy,vec2(0)))||any(greaterThan(coordinate.xy,vec2(1)))) return 1.0;
    float bias=max(0.00035*(1.0-dot(N,L)),0.00008);
    vec2 texel=1.0/vec2(textureSize(shadowMap,0));
    float visibility=0.0;
    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++)
        visibility+=texture(shadowMap,vec3(coordinate.xy+vec2(x,y)*texel,coordinate.z-bias));
    return visibility/9.0;
}
vec3 aces(vec3 value) {
    return clamp((value*(2.51*value+0.03))/(value*(2.43*value+0.59)+0.14),0.0,1.0);
}
void main() {
    vec4 base=texture(albedoMap,uv)*color;
    if(base.a<0.05) discard;
    vec3 albedo=max(base.rgb,vec3(0.0));
    vec3 N=normalize(normal),V=normalize(frame.cameraExposure.xyz-worldPosition);
    if(!gl_FrontFacing) N=-N;
    float roughness=clamp(object.material.x,0.06,1.0),metallic=clamp(object.material.y,0.0,1.0);
    vec3 lighting=albedo*frame.ambient.rgb*frame.ambient.a;
    vec3 L=normalize(-frame.sunDirection.xyz);
    lighting+=brdf(N,V,L,frame.sunColor.rgb*frame.sunDirection.w,albedo,metallic,roughness)*sunVisibility(N,L);
    for(int i=0;i<frame.counts.x;i++) {
        vec3 delta=frame.pointPosition[i].xyz-worldPosition;
        float distance=length(delta),range=frame.pointPosition[i].w;
        float fade=pow(clamp(1.0-pow(distance/max(range,0.001),4.0),0.0,1.0),2.0);
        vec3 radiance=frame.pointColor[i].rgb*frame.pointColor[i].a*fade/max(distance*distance,0.25);
        lighting+=brdf(N,V,delta/max(distance,0.001),radiance,albedo,metallic,roughness);
    }
    if(object.material.z>0.5) {
        float rim=pow(1.0-max(dot(N,V),0.0),2.5);
        lighting+=vec3(0.95,0.48,0.12)*(0.04+rim*0.7);
    }
    // The sRGB render target performs the output transfer function.
    outColor=vec4(aces(lighting*frame.cameraExposure.w),1.0);
}
