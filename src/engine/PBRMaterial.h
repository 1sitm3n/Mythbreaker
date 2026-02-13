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
