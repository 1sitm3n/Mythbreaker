#pragma once
#include "VulkanContext.h"
#include "VulkanTypes.h"
#include <glm/glm.hpp>

namespace myth {
namespace vk {

class ShadowMap {
public:
    void init(VulkanContext* context);
    void cleanup();
    
    // Begin/end shadow pass
    void beginShadowPass(VkCommandBuffer cmd);
    void endShadowPass(VkCommandBuffer cmd);
    
    // Compute light space matrix for directional light
    glm::mat4 computeLightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& centerPos);
    
    // Getters
    VkImageView imageView() const { return m_imageView; }
    VkSampler sampler() const { return m_sampler; }
    VkRenderPass renderPass() const { return m_renderPass; }
    VkFramebuffer framebuffer() const { return m_framebuffer; }
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }
    
private:
    void createImage();
    void createRenderPass();
    void createFramebuffer();
    void createPipeline();
    void createSampler();
    
    VulkanContext* m_context = nullptr;
    
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace myth
