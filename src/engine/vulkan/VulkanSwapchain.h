#pragma once

#include "VulkanTypes.h"
#include "VulkanContext.h"
#include <vector>

struct GLFWwindow;

namespace myth {
namespace vk {

class VulkanSwapchain {
public:
    void init(VulkanContext* context, GLFWwindow* window);
    void destroy();
    void recreate();
    
    bool acquireNextImage(uint32_t& imageIndex, VkSemaphore signalSemaphore);
    bool present(uint32_t imageIndex, VkSemaphore waitSemaphore);
    
    VkSwapchainKHR swapchain() const { return m_swapchain; }
    VkFormat imageFormat() const { return m_imageFormat; }
    VkExtent2D extent() const { return m_extent; }
    VkRenderPass renderPass() const { return m_renderPass; }
    VkFramebuffer framebuffer(uint32_t index) const { return m_framebuffers[index]; }
    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
    bool needsRecreation() const { return m_needsRecreation; }

private:
    void createSwapchain();
    void createImageViews();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void cleanup();
    
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps);
    VkFormat findDepthFormat();

    VulkanContext* m_context = nullptr;
    GLFWwindow* m_window = nullptr;
    
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat m_imageFormat;
    VkExtent2D m_extent;
    
    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthAllocation = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    VkFormat m_depthFormat;
    
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    
    bool m_needsRecreation = false;
};

} // namespace vk
} // namespace myth
