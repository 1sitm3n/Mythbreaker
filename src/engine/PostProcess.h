#pragma once
#include <vulkan/vulkan.h>

namespace myth { namespace vk { class VulkanContext; } }
using VulkanContext = myth::vk::VulkanContext;

namespace myth {

class PostProcess {
public:
    void init(VulkanContext* context, VkExtent2D extent, VkRenderPass finalRenderPass);
    void destroy();
    void resize(VkExtent2D extent);
    
    // Get the render pass to render scene into
    VkRenderPass sceneRenderPass() const { return m_sceneRenderPass; }
    VkFramebuffer sceneFramebuffer() const { return m_sceneFramebuffer; }
    
    // Render post-process effects to final framebuffer
    void render(VkCommandBuffer cmd, VkDescriptorSet cameraDS, uint32_t frameIndex);
    
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorSet descriptorSet() const { return m_descriptorSet; }
    
private:
    void createSceneResources(VkExtent2D extent);
    void destroySceneResources();
    void createPipeline(VkRenderPass finalRenderPass);
    void createDescriptors();
    
    VulkanContext* m_context = nullptr;
    VkExtent2D m_extent{};
    
    // Scene rendering target
    VkImage m_sceneImage = VK_NULL_HANDLE;
    VkDeviceMemory m_sceneMemory = VK_NULL_HANDLE;
    VkImageView m_sceneView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    
    // Depth buffer for scene
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    
    // Scene render pass and framebuffer
    VkRenderPass m_sceneRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_sceneFramebuffer = VK_NULL_HANDLE;
    
    // Post-process pipeline
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    
    // Bloom resources (bright pass + blur)
    VkImage m_bloomImage = VK_NULL_HANDLE;
    VkDeviceMemory m_bloomMemory = VK_NULL_HANDLE;
    VkImageView m_bloomView = VK_NULL_HANDLE;
};

} // namespace myth


