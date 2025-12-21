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
    
    // For skinned rendering
    VkDescriptorSetLayout skinnedDescriptorSetLayout() const { return m_skinnedDescriptorSetLayout; }
    VkDescriptorSet skinnedDescriptorSet(uint32_t frameIndex) const { return m_skinnedDescriptorSets[frameIndex]; }
    void bindSkinnedDescriptor(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frameIndex);
    void setSkinnedTexture(const VulkanTexture& texture);
    
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSet descriptorSet(uint32_t frameIndex) const { return m_cameraDescriptorSets[frameIndex]; }
    
private:
    void createDescriptorSetLayout();
    void createSkinnedDescriptorSetLayout();
    void createDescriptorPool();
    void createCameraDescriptorSets();
    void createSkinnedDescriptorSets();
    void createUniformBuffers();
    void createJointMatrixBuffers();
    
    VulkanContext* m_context = nullptr;
    
    // Standard layout (camera + texture)
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_cameraDescriptorSets;
    
    // Skinned layout (camera + texture + joints)
    VkDescriptorSetLayout m_skinnedDescriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_skinnedDescriptorSets;
    
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    
    // Camera UBO
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_uniformAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_uniformMapped;
    
    // Joint matrices UBO
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_jointBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_jointAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_jointMapped;
    
    std::vector<VkDescriptorSet> m_materialSets;
};

} // namespace vk
} // namespace myth

