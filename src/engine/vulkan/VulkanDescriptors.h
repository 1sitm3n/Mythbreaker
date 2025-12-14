#pragma once

#include "VulkanContext.h"
#include "VulkanTypes.h"
#include "VulkanTexture.h"
#include <array>
#include <vector>

namespace myth {
namespace vk {

class DescriptorManager {
public:
    void init(VulkanContext* context);
    void destroy();
    
    void updateCameraUBO(uint32_t frameIndex, const CameraUBO& ubo);
    uint32_t createMaterial(const VulkanTexture& texture);
    void bindMaterial(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frameIndex, uint32_t materialId);
    
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSet descriptorSet(uint32_t frameIndex) const { return m_cameraDescriptorSets[frameIndex]; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createCameraDescriptorSets();
    void createUniformBuffers();
    
    VulkanContext* m_context = nullptr;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_cameraDescriptorSets;
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_uniformAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_uniformMapped;
    
    std::vector<VkDescriptorSet> m_materialSets;
};

} // namespace vk
} // namespace myth
