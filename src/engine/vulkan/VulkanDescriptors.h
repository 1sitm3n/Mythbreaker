#pragma once

#include "VulkanTypes.h"
#include "VulkanContext.h"
#include "VulkanBuffer.h"
#include <array>

namespace myth {
namespace vk {

class DescriptorManager {
public:
    void init(VulkanContext* context);
    void destroy();
    
    void updateCameraUBO(uint32_t frameIndex, const CameraUBO& ubo);
    
    VkDescriptorSetLayout descriptorSetLayout() const { return m_layout; }
    VkDescriptorSet descriptorSet(uint32_t frameIndex) const { return m_sets[frameIndex]; }

private:
    void createLayout();
    void createPool();
    void createUniformBuffers();
    void createDescriptorSets();

    VulkanContext* m_context = nullptr;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_sets{};
    std::array<VulkanBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_mappedBuffers{};
};

} // namespace vk
} // namespace myth
