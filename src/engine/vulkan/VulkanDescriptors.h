#pragma once

#include "VulkanContext.h"
#include "VulkanTypes.h"
#include "VulkanTexture.h"
#include <array>

namespace myth {
namespace vk {

class DescriptorManager {
public:
    void init(VulkanContext* context);
    void destroy();
    
    void updateCameraUBO(uint32_t frameIndex, const CameraUBO& ubo);
    void bindTexture(uint32_t frameIndex, const VulkanTexture& texture);
    
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSet descriptorSet(uint32_t frameIndex) const { return m_descriptorSets[frameIndex]; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUniformBuffers();
    
    VulkanContext* m_context = nullptr;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descriptorSets;
    
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_uniformAllocations;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_uniformMapped;
};

} // namespace vk
} // namespace myth
