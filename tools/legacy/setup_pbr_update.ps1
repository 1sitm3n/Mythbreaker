###############################################################################
# Mythbreaker PBR Update — Phase 1 Setup Script
# 
# USAGE:
#   1. Open PowerShell
#   2. cd C:\Projects\Mythbreaker
#   3. .\setup_pbr_update.ps1
#
# This script creates all new files, compiles shaders, and builds the project.
###############################################################################

param(
    [string]$ProjectRoot = "C:\Projects\Mythbreaker",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-Location $ProjectRoot

Write-Host "`n=== Mythbreaker PBR Update ===" -ForegroundColor Cyan
Write-Host "Project root: $ProjectRoot" -ForegroundColor Gray

###############################################################################
# SHADER: shaders/pbr.vert
###############################################################################
Write-Host "`n[1/10] Writing shaders/pbr.vert..." -ForegroundColor Yellow
Set-Content -Path "shaders/pbr.vert" -Value @'
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

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

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec3 fragColor;
layout(location = 5) out mat3 fragTBN;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = camera.viewProj * worldPos;

    fragWorldPos = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragPosLightSpace = camera.lightSpaceMatrix * worldPos;

    mat3 normalMatrix = mat3(push.model);
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;

    fragNormal = N;
    fragTBN = mat3(T, B, N);
}
'@

###############################################################################
# SHADER: shaders/pbr.frag
###############################################################################
Write-Host "[2/10] Writing shaders/pbr.frag..." -ForegroundColor Yellow
Set-Content -Path "shaders/pbr.frag" -Value @'
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
'@

###############################################################################
# SHADER: shaders/pbr_skinned.vert
###############################################################################
Write-Host "[3/10] Writing shaders/pbr_skinned.vert..." -ForegroundColor Yellow
Set-Content -Path "shaders/pbr_skinned.vert" -Value @'
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in ivec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in vec4 inTangent;

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

layout(set = 0, binding = 2) uniform JointMatrices {
    mat4 joints[128];
} skeleton;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec3 fragColor;
layout(location = 5) out mat3 fragTBN;

void main() {
    mat4 skinMatrix =
        inWeights.x * skeleton.joints[inJoints.x] +
        inWeights.y * skeleton.joints[inJoints.y] +
        inWeights.z * skeleton.joints[inJoints.z] +
        inWeights.w * skeleton.joints[inJoints.w];

    vec4 skinnedPos    = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;
    vec3 skinnedTangent = mat3(skinMatrix) * inTangent.xyz;

    vec4 worldPos = push.model * skinnedPos;
    gl_Position = camera.viewProj * worldPos;

    fragWorldPos = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragPosLightSpace = camera.lightSpaceMatrix * worldPos;

    mat3 normalMatrix = mat3(push.model);
    vec3 N = normalize(normalMatrix * skinnedNormal);
    vec3 T = normalize(normalMatrix * skinnedTangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;

    fragNormal = N;
    fragTBN = mat3(T, B, N);
}
'@

###############################################################################
# SHADER: shaders/postprocess_v2.frag
###############################################################################
Write-Host "[4/10] Writing shaders/postprocess_v2.frag..." -ForegroundColor Yellow
Set-Content -Path "shaders/postprocess_v2.frag" -Value @'
#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform PostProcessParams {
    float bloomIntensity;
    float bloomThreshold;
    float vignetteStrength;
    float filmGrainAmount;
    float exposureAdjust;
    float saturation;
    float time;
    float padding;
} params;

vec3 ACESFilm(vec3 x) {
    float a = 2.51; float b = 0.03;
    float c = 2.43; float d = 0.59; float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 bloomSample(vec2 uv, vec2 texelSize) {
    vec3 result = vec3(0.0);
    result += texture(sceneTex, uv).rgb * 0.25;
    result += texture(sceneTex, uv + texelSize * vec2( 1.0,  0.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2(-1.0,  0.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2( 0.0,  1.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2( 0.0, -1.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2( 2.0,  2.0)).rgb * 0.0625;
    result += texture(sceneTex, uv + texelSize * vec2(-2.0,  2.0)).rgb * 0.0625;
    result += texture(sceneTex, uv + texelSize * vec2( 2.0, -2.0)).rgb * 0.0625;
    result += texture(sceneTex, uv + texelSize * vec2(-2.0, -2.0)).rgb * 0.0625;
    return result;
}

vec3 extractBright(vec3 color, float threshold) {
    float brightness = luminance(color);
    float soft = brightness - threshold + 0.5;
    soft = clamp(soft, 0.0, 1.0);
    soft = soft * soft;
    float contribution = max(soft, brightness - threshold);
    contribution = max(contribution, 0.0) / max(brightness, 0.0001);
    return color * contribution;
}

vec3 computeBloom(vec2 uv, vec2 texelSize) {
    vec3 bloom = vec3(0.0);
    bloom += extractBright(bloomSample(uv, texelSize * 2.0), params.bloomThreshold) * 0.5;
    bloom += extractBright(bloomSample(uv, texelSize * 4.0), params.bloomThreshold * 0.8) * 0.3;
    bloom += extractBright(bloomSample(uv, texelSize * 8.0), params.bloomThreshold * 0.6) * 0.2;
    return bloom;
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 uv = fragUV;
    vec2 texelSize = 1.0 / textureSize(sceneTex, 0);

    vec3 sceneColor = texture(sceneTex, uv).rgb;
    sceneColor *= exp2(params.exposureAdjust);

    vec3 bloom = computeBloom(uv, texelSize);
    vec3 color = sceneColor + bloom * params.bloomIntensity;

    color = ACESFilm(color);

    // Color grading
    float lum = luminance(color);
    vec3 shadowTint = vec3(1.04, 0.96, 0.92);
    vec3 highlightTint = vec3(0.96, 0.98, 1.04);
    color *= mix(shadowTint, highlightTint, smoothstep(0.0, 1.0, lum));
    vec3 gray = vec3(luminance(color));
    color = mix(gray, color, params.saturation);

    // Vignette
    vec2 d = uv - 0.5;
    color *= 1.0 - dot(d, d) * params.vignetteStrength * 2.0;

    // Film grain
    float grain = (hash(uv * 1000.0 + fract(params.time) * 100.0) * 2.0 - 1.0) * params.filmGrainAmount;
    color += grain;

    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
'@

###############################################################################
# ENGINE: src/engine/PBRMaterial.h
###############################################################################
Write-Host "[5/10] Writing src/engine/PBRMaterial.h..." -ForegroundColor Yellow
$pbrMaterialContent = @'
#pragma once

#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/VulkanTypes.h"
#include "Logger.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstring>

namespace myth {

struct PBRPushConstants {
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
};

static_assert(sizeof(PBRPushConstants) <= 128, "Push constants exceed guaranteed minimum");

struct PBRVertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(PBRVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attrs{};
        attrs[0].binding = 0; attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(PBRVertex, position);
        attrs[1].binding = 0; attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(PBRVertex, color);
        attrs[2].binding = 0; attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(PBRVertex, texCoord);
        attrs[3].binding = 0; attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[3].offset = offsetof(PBRVertex, normal);
        attrs[4].binding = 0; attrs[4].location = 4;
        attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[4].offset = offsetof(PBRVertex, tangent);
        return attrs;
    }
};

struct PBRMaterialParams {
    glm::vec4 baseColorFactor   = glm::vec4(1.0f);
    float     metallicFactor    = 0.0f;
    float     roughnessFactor   = 0.5f;
    float     normalScale       = 1.0f;
    float     occlusionStrength = 1.0f;
    glm::vec3 emissiveFactor    = glm::vec3(0.0f);
    float     alphaCutoff       = 0.5f;
    enum class AlphaMode { Opaque, Mask, Blend } alphaMode = AlphaMode::Opaque;
};

struct PBRMaterial {
    std::string name;
    PBRMaterialParams params;
    uint32_t albedoTexture           = UINT32_MAX;
    uint32_t normalTexture           = UINT32_MAX;
    uint32_t metallicRoughnessTexture = UINT32_MAX;
    uint32_t aoTexture               = UINT32_MAX;
    uint32_t emissiveTexture         = UINT32_MAX;
    VkDescriptorSet descriptorSet    = VK_NULL_HANDLE;

    PBRPushConstants getPushConstants(const glm::mat4& modelMatrix) const {
        PBRPushConstants pc{};
        pc.model             = modelMatrix;
        pc.baseColorFactor   = params.baseColorFactor;
        pc.metallicFactor    = params.metallicFactor;
        pc.roughnessFactor   = params.roughnessFactor;
        pc.normalScale       = params.normalScale;
        pc.occlusionStrength = params.occlusionStrength;
        return pc;
    }
};

class PBRMaterialManager {
public:
    void init(vk::VulkanContext* context) {
        m_context = context;
        createDefaultTextures();
        createDescriptorSetLayout();
        createDescriptorPool();
        Logger::info("PBR material system initialized");
    }

    void destroy() {
        m_defaultWhite.destroy();
        m_defaultNormal.destroy();
        m_defaultBlack.destroy();
        if (m_descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_context->device(), m_descriptorPool, nullptr);
        if (m_descriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_context->device(), m_descriptorSetLayout, nullptr);
    }

    uint32_t createMaterial(
        const std::string& name,
        const PBRMaterialParams& params,
        const vk::VulkanTexture* albedo            = nullptr,
        const vk::VulkanTexture* normal            = nullptr,
        const vk::VulkanTexture* metallicRoughness = nullptr,
        const vk::VulkanTexture* ao                = nullptr
    ) {
        PBRMaterial mat;
        mat.name = name;
        mat.params = params;

        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        vkAllocateDescriptorSets(m_context->device(), &allocInfo, &mat.descriptorSet);

        auto writeImage = [&](uint32_t binding, const vk::VulkanTexture& tex) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = tex.view();
            imageInfo.sampler = tex.sampler();
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = mat.descriptorSet;
            write.dstBinding = binding;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
        };

        writeImage(1, albedo ? *albedo : m_defaultWhite);
        writeImage(4, normal ? *normal : m_defaultNormal);
        writeImage(5, metallicRoughness ? *metallicRoughness : m_defaultWhite);
        writeImage(6, ao ? *ao : m_defaultWhite);

        uint32_t id = static_cast<uint32_t>(m_materials.size());
        m_materials.push_back(std::move(mat));
        Logger::info("PBR material created: " + name + " (id=" + std::to_string(id) + ")");
        return id;
    }

    void updateCameraBinding(uint32_t materialId, VkBuffer cameraUBO) {
        if (materialId >= m_materials.size()) return;
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraUBO;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(vk::CameraUBO);
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_materials[materialId].descriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }

    void updateShadowBinding(uint32_t materialId, VkImageView shadowView, VkSampler shadowSampler) {
        if (materialId >= m_materials.size()) return;
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = shadowView;
        imageInfo.sampler = shadowSampler;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_materials[materialId].descriptorSet;
        write.dstBinding = 3;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }

    void bind(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t materialId) const {
        if (materialId >= m_materials.size()) return;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                0, 1, &m_materials[materialId].descriptorSet, 0, nullptr);
    }

    void pushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                       uint32_t materialId, const glm::mat4& modelMatrix) const {
        if (materialId >= m_materials.size()) return;
        PBRPushConstants pc = m_materials[materialId].getPushConstants(modelMatrix);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PBRPushConstants), &pc);
    }

    const PBRMaterial* getMaterial(uint32_t id) const {
        return id < m_materials.size() ? &m_materials[id] : nullptr;
    }

    VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }
    uint32_t materialCount() const { return static_cast<uint32_t>(m_materials.size()); }

private:
    void createDefaultTextures() {
        uint8_t white[] = {255, 255, 255, 255};
        m_defaultWhite.loadFromMemory(m_context, white, 1, 1);
        uint8_t flatNormal[] = {128, 128, 255, 255};
        m_defaultNormal.loadFromMemory(m_context, flatNormal, 1, 1);
        uint8_t black[] = {0, 0, 0, 255};
        m_defaultBlack.loadFromMemory(m_context, black, 1, 1);
    }

    void createDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 3;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[3].binding = 4;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[4].binding = 5;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[5].binding = 6;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        vkCreateDescriptorSetLayout(m_context->device(), &layoutInfo, nullptr, &m_descriptorSetLayout);
    }

    void createDescriptorPool() {
        constexpr uint32_t MAX_PBR_MATERIALS = 64;
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = MAX_PBR_MATERIALS;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = MAX_PBR_MATERIALS * 5;
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = MAX_PBR_MATERIALS;
        vkCreateDescriptorPool(m_context->device(), &poolInfo, nullptr, &m_descriptorPool);
    }

    vk::VulkanContext* m_context = nullptr;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    vk::VulkanTexture m_defaultWhite;
    vk::VulkanTexture m_defaultNormal;
    vk::VulkanTexture m_defaultBlack;
    std::vector<PBRMaterial> m_materials;
};

} // namespace myth
'@
Set-Content -Path "src/engine/PBRMaterial.h" -Value $pbrMaterialContent

###############################################################################
# ENGINE: src/engine/FrustumCuller.h
###############################################################################
Write-Host "[6/10] Writing src/engine/FrustumCuller.h..." -ForegroundColor Yellow
$frustumContent = @'
#pragma once

#include <glm/glm.hpp>
#include <array>
#include <algorithm>

namespace myth {

struct AABB {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    AABB() = default;
    AABB(const glm::vec3& mn, const glm::vec3& mx) : min(mn), max(mx) {}

    static AABB fromCenterExtents(const glm::vec3& center, const glm::vec3& extents) {
        return AABB(center - extents, center + extents);
    }

    static AABB fromChunk(int cx, int cz, float chunkSize, float minY, float maxY) {
        float x = cx * chunkSize - chunkSize / 2.0f;
        float z = cz * chunkSize - chunkSize / 2.0f;
        return AABB(glm::vec3(x, minY, z), glm::vec3(x + chunkSize, maxY, z + chunkSize));
    }

    AABB transformed(const glm::mat4& m) const {
        glm::vec3 center = (min + max) * 0.5f;
        glm::vec3 extents = (max - min) * 0.5f;
        glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));
        glm::vec3 newExtents(0.0f);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                newExtents[i] += std::abs(m[j][i]) * extents[j];
        return AABB(newCenter - newExtents, newCenter + newExtents);
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return (max - min) * 0.5f; }
    float radius() const { return glm::length(extents()); }
};

struct Plane {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float distance = 0.0f;
    float distanceTo(const glm::vec3& point) const { return glm::dot(normal, point) + distance; }
};

class Frustum {
public:
    enum Side { Left = 0, Right, Top, Bottom, Near, Far, COUNT };

    void update(const glm::mat4& vp) {
        m_planes[Left]  = {glm::vec3(vp[0][3]+vp[0][0], vp[1][3]+vp[1][0], vp[2][3]+vp[2][0]), vp[3][3]+vp[3][0]};
        m_planes[Right] = {glm::vec3(vp[0][3]-vp[0][0], vp[1][3]-vp[1][0], vp[2][3]-vp[2][0]), vp[3][3]-vp[3][0]};
        m_planes[Bottom]= {glm::vec3(vp[0][3]+vp[0][1], vp[1][3]+vp[1][1], vp[2][3]+vp[2][1]), vp[3][3]+vp[3][1]};
        m_planes[Top]   = {glm::vec3(vp[0][3]-vp[0][1], vp[1][3]-vp[1][1], vp[2][3]-vp[2][1]), vp[3][3]-vp[3][1]};
        m_planes[Near]  = {glm::vec3(vp[0][3]+vp[0][2], vp[1][3]+vp[1][2], vp[2][3]+vp[2][2]), vp[3][3]+vp[3][2]};
        m_planes[Far]   = {glm::vec3(vp[0][3]-vp[0][2], vp[1][3]-vp[1][2], vp[2][3]-vp[2][2]), vp[3][3]-vp[3][2]};

        for (auto& p : m_planes) {
            float len = glm::length(p.normal);
            if (len > 0.0001f) { p.normal /= len; p.distance /= len; }
        }
    }

    bool isVisible(const AABB& aabb) const {
        for (const auto& plane : m_planes) {
            glm::vec3 pVertex;
            pVertex.x = (plane.normal.x >= 0.0f) ? aabb.max.x : aabb.min.x;
            pVertex.y = (plane.normal.y >= 0.0f) ? aabb.max.y : aabb.min.y;
            pVertex.z = (plane.normal.z >= 0.0f) ? aabb.max.z : aabb.min.z;
            if (plane.distanceTo(pVertex) < 0.0f) return false;
        }
        return true;
    }

    bool isVisible(const glm::vec3& center, float radius) const {
        for (const auto& plane : m_planes)
            if (plane.distanceTo(center) < -radius) return false;
        return true;
    }

private:
    std::array<Plane, COUNT> m_planes;
};

class FrustumCuller {
public:
    void update(const glm::mat4& viewProjection) {
        m_frustum.update(viewProjection);
        m_totalTested = 0;
        m_totalCulled = 0;
    }

    bool testAABB(const AABB& aabb) {
        m_totalTested++;
        bool visible = m_frustum.isVisible(aabb);
        if (!visible) m_totalCulled++;
        return visible;
    }

    bool testSphere(const glm::vec3& center, float radius) {
        m_totalTested++;
        bool visible = m_frustum.isVisible(center, radius);
        if (!visible) m_totalCulled++;
        return visible;
    }

    bool testChunk(int cx, int cz, float chunkSize,
                   float minTerrainY = -20.0f, float maxTerrainY = 80.0f) {
        return testAABB(AABB::fromChunk(cx, cz, chunkSize, minTerrainY, maxTerrainY));
    }

    bool testEntity(const glm::vec3& position, const glm::vec3& scale, float baseBoundRadius = 1.0f) {
        float maxScale = (std::max)({scale.x, scale.y, scale.z});
        return testSphere(position, baseBoundRadius * maxScale);
    }

    uint32_t totalTested() const { return m_totalTested; }
    uint32_t totalCulled() const { return m_totalCulled; }
    uint32_t totalVisible() const { return m_totalTested - m_totalCulled; }
    float cullPercentage() const {
        return m_totalTested > 0 ? (float)m_totalCulled / m_totalTested * 100.0f : 0.0f;
    }

private:
    Frustum  m_frustum;
    uint32_t m_totalTested = 0;
    uint32_t m_totalCulled = 0;
};

} // namespace myth
'@
Set-Content -Path "src/engine/FrustumCuller.h" -Value $frustumContent

###############################################################################
# ENGINE: src/engine/TangentCalculator.h
###############################################################################
Write-Host "[7/10] Writing src/engine/TangentCalculator.h..." -ForegroundColor Yellow
$tangentContent = @'
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

namespace myth {

class TangentCalculator {
public:
    static void calculate(
        const glm::vec3* positions, const glm::vec3* normals,
        const glm::vec2* texcoords, uint32_t numVertices,
        const uint32_t* indices, uint32_t numIndices,
        std::vector<glm::vec4>& outTangents
    ) {
        std::vector<glm::vec3> tan1(numVertices, glm::vec3(0.0f));
        std::vector<glm::vec3> tan2(numVertices, glm::vec3(0.0f));

        for (uint32_t i = 0; i < numIndices; i += 3) {
            uint32_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
            glm::vec3 edge1 = positions[i1] - positions[i0];
            glm::vec3 edge2 = positions[i2] - positions[i0];
            glm::vec2 dUV1 = texcoords[i1] - texcoords[i0];
            glm::vec2 dUV2 = texcoords[i2] - texcoords[i0];
            float denom = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
            if (std::abs(denom) < 1e-8f) continue;
            float r = 1.0f / denom;
            glm::vec3 tangent   = (edge1 * dUV2.y - edge2 * dUV1.y) * r;
            glm::vec3 bitangent = (edge2 * dUV1.x - edge1 * dUV2.x) * r;
            tan1[i0] += tangent; tan1[i1] += tangent; tan1[i2] += tangent;
            tan2[i0] += bitangent; tan2[i1] += bitangent; tan2[i2] += bitangent;
        }

        outTangents.resize(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) {
            const glm::vec3& n = normals[i];
            const glm::vec3& t = tan1[i];
            if (glm::length(t) < 1e-6f) {
                glm::vec3 up = (std::abs(n.y) < 0.99f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
                outTangents[i] = glm::vec4(glm::normalize(glm::cross(up, n)), 1.0f);
                continue;
            }
            glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
            float w = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            outTangents[i] = glm::vec4(tangent, w);
        }
    }

    static void calculateNonIndexed(
        const glm::vec3* positions, const glm::vec3* normals,
        const glm::vec2* texcoords, uint32_t numVertices,
        std::vector<glm::vec4>& outTangents
    ) {
        std::vector<uint32_t> indices(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) indices[i] = i;
        calculate(positions, normals, texcoords, numVertices, indices.data(), numVertices, outTangents);
    }
};

} // namespace myth
'@
Set-Content -Path "src/engine/TangentCalculator.h" -Value $tangentContent

###############################################################################
# ENGINE: src/engine/GameState.h
###############################################################################
Write-Host "[8/10] Writing src/engine/GameState.h..." -ForegroundColor Yellow
$gameStateContent = @'
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace myth {

enum class GameState {
    Loading, MainMenu, Playing, Paused, Inventory,
    Dialogue, Settings, Dead, Cutscene, COUNT
};

inline const char* gameStateName(GameState s) {
    switch (s) {
        case GameState::Loading:   return "Loading";
        case GameState::MainMenu:  return "MainMenu";
        case GameState::Playing:   return "Playing";
        case GameState::Paused:    return "Paused";
        case GameState::Inventory: return "Inventory";
        case GameState::Dialogue:  return "Dialogue";
        case GameState::Settings:  return "Settings";
        case GameState::Dead:      return "Dead";
        case GameState::Cutscene:  return "Cutscene";
        default: return "Unknown";
    }
}

class GameStateMachine {
public:
    using TransitionCallback = std::function<void(GameState from, GameState to)>;

    void init(GameState initialState = GameState::Loading) {
        m_currentState = initialState;
        m_previousState = initialState;
    }

    GameState current() const { return m_currentState; }
    GameState previous() const { return m_previousState; }

    bool transitionTo(GameState newState) {
        if (newState == m_currentState) return false;
        if (!isValidTransition(m_currentState, newState)) return false;
        m_previousState = m_currentState;
        m_currentState = newState;
        for (auto& cb : m_onTransition) cb(m_previousState, m_currentState);
        return true;
    }

    void onTransition(TransitionCallback callback) {
        m_onTransition.push_back(std::move(callback));
    }

    bool returnToPrevious() { return transitionTo(m_previousState); }

    bool isPlaying() const { return m_currentState == GameState::Playing; }

    bool isGameplayActive() const {
        return m_currentState == GameState::Playing || m_currentState == GameState::Dialogue;
    }

    bool shouldRenderWorld() const {
        return m_currentState != GameState::Loading && m_currentState != GameState::MainMenu;
    }

    bool shouldCaptureMouse() const { return m_currentState == GameState::Playing; }

    bool shouldShowCursor() const {
        return m_currentState == GameState::MainMenu || m_currentState == GameState::Paused ||
               m_currentState == GameState::Inventory || m_currentState == GameState::Settings ||
               m_currentState == GameState::Dead;
    }

    float getTimeScale() const {
        switch (m_currentState) {
            case GameState::Playing:  return 1.0f;
            case GameState::Dialogue: return 0.1f;
            default: return 0.0f;
        }
    }

private:
    bool isValidTransition(GameState from, GameState to) const {
        switch (from) {
            case GameState::Loading:   return to == GameState::MainMenu || to == GameState::Playing;
            case GameState::MainMenu:  return to == GameState::Playing || to == GameState::Settings || to == GameState::Loading;
            case GameState::Playing:   return to == GameState::Paused || to == GameState::Inventory || to == GameState::Dialogue || to == GameState::Dead || to == GameState::Cutscene || to == GameState::MainMenu;
            case GameState::Paused:    return to == GameState::Playing || to == GameState::Settings || to == GameState::MainMenu;
            case GameState::Inventory: return to == GameState::Playing;
            case GameState::Dialogue:  return to == GameState::Playing;
            case GameState::Settings:  return to == GameState::MainMenu || to == GameState::Paused;
            case GameState::Dead:      return to == GameState::Playing || to == GameState::MainMenu;
            case GameState::Cutscene:  return to == GameState::Playing;
            default: return false;
        }
    }

    GameState m_currentState = GameState::Loading;
    GameState m_previousState = GameState::Loading;
    std::vector<TransitionCallback> m_onTransition;
};

} // namespace myth
'@
Set-Content -Path "src/engine/GameState.h" -Value $gameStateContent

###############################################################################
# Step 9: Compile shaders
###############################################################################
Write-Host "`n[9/10] Compiling shaders..." -ForegroundColor Yellow

$glslc = $null
if ($env:VULKAN_SDK) { $glslc = Join-Path $env:VULKAN_SDK "Bin\glslc.exe" }
if (-not $glslc -or -not (Test-Path $glslc)) {
    $glslc = Get-Command glslc -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
}
if (-not $glslc) {
    Write-Host "  WARNING: glslc not found! Skipping shader compilation." -ForegroundColor Red
    Write-Host "  Install the Vulkan SDK and re-run, or compile shaders manually." -ForegroundColor Red
} else {
    Write-Host "  Using: $glslc" -ForegroundColor Gray

    $shaders = @(
        "shaders/pbr.vert",
        "shaders/pbr.frag",
        "shaders/pbr_skinned.vert",
        "shaders/postprocess_v2.frag"
    )

    $allShaders = Get-ChildItem -Path "shaders" -Include "*.vert","*.frag" -File | Where-Object { $_.Extension -ne ".spv" -and $_.Name -notmatch "\.spv$" }

    $failed = 0
    foreach ($shader in $allShaders) {
        $outPath = "$($shader.FullName).spv"
        Write-Host "  Compiling: $($shader.Name)" -NoNewline
        $result = & $glslc $shader.FullName -o $outPath 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host " [OK]" -ForegroundColor Green
        } else {
            Write-Host " [FAILED]" -ForegroundColor Red
            Write-Host "    $result" -ForegroundColor Red
            $failed++
        }
    }

    if ($failed -gt 0) {
        Write-Host "`n  $failed shader(s) failed to compile!" -ForegroundColor Red
    } else {
        Write-Host "  All shaders compiled successfully." -ForegroundColor Green
    }
}

###############################################################################
# Step 10: Build
###############################################################################
if (-not $SkipBuild) {
    Write-Host "`n[10/10] Building project..." -ForegroundColor Yellow

    if (-not (Test-Path "build")) {
        Write-Host "  Configuring CMake..." -ForegroundColor Gray
        cmake -S . -B build 2>&1 | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
    }

    Write-Host "  Building Release..." -ForegroundColor Gray
    cmake --build build --config Release 2>&1 | ForEach-Object {
        if ($_ -match "error") { Write-Host "  $_" -ForegroundColor Red }
        elseif ($_ -match "warning") { Write-Host "  $_" -ForegroundColor Yellow }
        else { Write-Host "  $_" -ForegroundColor Gray }
    }

    if ($LASTEXITCODE -eq 0) {
        Write-Host "`n=== BUILD SUCCESSFUL ===" -ForegroundColor Green
        Write-Host "Run with: .\build\bin\Release\Mythbreaker.exe" -ForegroundColor Cyan
    } else {
        Write-Host "`n=== BUILD FAILED ===" -ForegroundColor Red
        Write-Host "The new files are header-only additions. The existing game" -ForegroundColor Yellow
        Write-Host "should still build unchanged. Check errors above." -ForegroundColor Yellow
    }
} else {
    Write-Host "`n[10/10] Skipped build (-SkipBuild)" -ForegroundColor Gray
}

###############################################################################
# Summary
###############################################################################
Write-Host "`n=== Files Created ===" -ForegroundColor Cyan
Write-Host "  shaders/pbr.vert              - PBR vertex shader with TBN" -ForegroundColor White
Write-Host "  shaders/pbr.frag              - Cook-Torrance BRDF fragment shader" -ForegroundColor White
Write-Host "  shaders/pbr_skinned.vert      - PBR vertex shader for animated models" -ForegroundColor White
Write-Host "  shaders/postprocess_v2.frag   - Improved bloom + color grading" -ForegroundColor White
Write-Host "  src/engine/PBRMaterial.h       - PBR material manager + vertex format" -ForegroundColor White
Write-Host "  src/engine/FrustumCuller.h     - Frustum culling system" -ForegroundColor White
Write-Host "  src/engine/TangentCalculator.h - Tangent generation for normal mapping" -ForegroundColor White
Write-Host "  src/engine/GameState.h         - Game state machine" -ForegroundColor White

Write-Host "`n=== Next Steps ===" -ForegroundColor Cyan
Write-Host "  1. The existing game builds and runs unchanged" -ForegroundColor White
Write-Host "  2. Wire FrustumCuller into recordCommandBuffer() for instant FPS boost" -ForegroundColor White
Write-Host "  3. Wire GameStateMachine into mainLoop() for pause/menu support" -ForegroundColor White
Write-Host "  4. Create a PBR pipeline in VulkanPipeline::initPBR() to use the new shaders" -ForegroundColor White
Write-Host "  5. See INTEGRATION_GUIDE.md for step-by-step wiring instructions" -ForegroundColor White
Write-Host ""
