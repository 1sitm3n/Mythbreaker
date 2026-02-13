#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec3 fragColor;
layout(location = 5) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

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

layout(set = 0, binding = 1) uniform sampler2D albedoMap;
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap;
layout(set = 0, binding = 4) uniform sampler2D normalMap;
layout(set = 0, binding = 5) uniform sampler2D metallicRoughnessMap;
layout(set = 0, binding = 6) uniform sampler2D aoMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
} push;

const float PI = 3.14159265359;
const float DIELECTRIC_F0 = 0.04;

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float calculateShadow(vec4 posLightSpace) {
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;
    }
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec3 sc = vec3(projCoords.xy + vec2(x, y) * texelSize, projCoords.z - camera.shadowBias);
            shadow += texture(shadowMap, sc);
        }
    }
    shadow /= 9.0;
    return 1.0 - shadow;
}

vec3 getSkyAmbient(vec3 N) {
    float up = N.y * 0.5 + 0.5;
    vec3 skyIrradiance = mix(camera.skyColorBottom, camera.skyColorTop, up);
    return skyIrradiance * camera.ambientIntensity * camera.weatherAmbient;
}

void main() {
    vec4 albedoSample = texture(albedoMap, fragTexCoord);
    vec4 baseColor = albedoSample * push.baseColorFactor * vec4(fragColor, 1.0);
    if (baseColor.a < 0.1) discard;
    vec3 albedo = baseColor.rgb;

    vec2 mr = texture(metallicRoughnessMap, fragTexCoord).gb;
    float metallic  = mr.y * push.metallicFactor;
    float roughness = mr.x * push.roughnessFactor;
    roughness = clamp(roughness, 0.04, 1.0);

    float ao = texture(aoMap, fragTexCoord).r;
    ao = mix(1.0, ao, push.occlusionStrength);

    vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    tangentNormal.xy *= push.normalScale;
    vec3 N = normalize(fragTBN * tangentNormal);

    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 L = normalize(-camera.sunDirection);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(DIELECTRIC_F0), albedo, metallic);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(HdotV, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    float shadow = calculateShadow(fragPosLightSpace);
    vec3 radiance = camera.sunColor * camera.sunIntensity;
    vec3 directLighting = (diffuse + specular) * radiance * NdotL * (1.0 - shadow);

    vec3 ambient = getSkyAmbient(N);
    vec3 kSA = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kDA = (vec3(1.0) - kSA) * (1.0 - metallic);
    vec3 ambientDiffuse  = kDA * albedo * ambient;
    vec3 ambientSpecular = kSA * ambient * (1.0 - roughness * 0.7);
    vec3 ambientLighting = (ambientDiffuse + ambientSpecular) * ao;

    vec3 color = directLighting + ambientLighting;
    color += color * camera.lightningFlash * 2.0;

    float dist = length(camera.cameraPos - fragWorldPos);
    float fogFactor = clamp(exp(-dist * camera.fogDensity), 0.0, 1.0);
    color = mix(camera.fogColor, color, fogFactor);

    outColor = vec4(color, baseColor.a);
}
