#pragma once
#include "VulkanContext.h"
#include "VulkanTypes.h"
#include "VulkanTexture.h"
#include <array>
#include <vector>
namespace myth {
namespace vk {
// Joint matrices for GPU skinning (128 joints max)
struct JointMatricesUBO {
    glm::mat4 jointMatrices[128];
};
class DescriptorManager {
public:
    void init(VulkanContext* context);
    void destroy();
    void updateCameraUBO(uint32_t frameIndex, const CameraUBO& ubo);
    void updateJointMatrices(uint32_t frameIndex, const glm::mat4* matrices, uint32_t count);
    uint32_t createMaterial(const VulkanTexture& texture);
    void bindMaterial(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frameIndex, uint32_t materialId);
    // Shadow map binding
    void setShadowMap(VkImageView shadowView, VkSampler shadowSampler);
    // For skinned rendering
    VkDescriptorSetLayout skinnedDescriptorSetLayout() const { return m_skinnedDescriptorSetLayout; }
    VkDescriptorSet skinnedDescriptorSet(uint32_t frameIndex) const { return m_skinnedDescriptorSets[frameIndex]; }
    void bindSkinnedDescriptor(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frameIndex);
    void setSkinnedTexture(const VulkanTexture& texture);
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSet descriptorSet(uint32_t frameIndex) const { return m_cameraDescriptorSets[frameIndex]; }
    
    // Shadow pass descriptor
    VkDescriptorSetLayout shadowDescriptorSetLayout() const { return m_shadowDescriptorSetLayout; }
    VkDescriptorSet shadowDescriptorSet(uint32_t frameIndex) const { return m_shadowDescriptorSets[frameIndex]; }
private:
    void createDescriptorSetLayout();
    void createSkinnedDescriptorSetLayout();
    void createShadowDescriptorSetLayout();
    void createDescriptorPool();
    void createCameraDescriptorSets();
    void createSkinnedDescriptorSets();
    void createShadowDescriptorSets();
    void createUniformBuffers();
    void createJointMatrixBuffers();
    void createLightSpaceBuffers();
    VulkanContext* m_context = nullptr;
    // Standard layout (camera + texture + shadow)
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_cameraDescriptorSets;
    // Skinned layout (camera + texture + joints + shadow)
    VkDescriptorSetLayout m_skinnedDescriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_skinnedDescriptorSets;
    // Shadow pass layout (light space matrix only)
    VkDescriptorSetLayout m_shadowDescriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_shadowDescriptorSets;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    // Camera UBO
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_uniformAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_uniformMapped;
    // Joint matrices UBO
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_jointBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_jointAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_jointMapped;
    // Light space matrix UBO (for shadow pass)
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_lightSpaceBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_lightSpaceAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_lightSpaceMapped;
    std::vector<VkDescriptorSet> m_materialSets;
    
    // Shadow map references
    VkImageView m_shadowImageView = VK_NULL_HANDLE;
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
};
} // namespace vk
} // namespace myth
