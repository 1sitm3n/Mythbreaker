#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include "VulkanTypes.h"
#include <vector>

namespace myth {
namespace vk {

class VulkanContext {
public:
    void init(GLFWwindow* window);
    void destroy();
    
    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkSurfaceKHR surface() const { return m_surface; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    uint32_t graphicsFamily() const { return m_queueFamilies.graphicsFamily.value(); }
    uint32_t presentFamily() const { return m_queueFamilies.presentFamily.value(); }
    const QueueFamilyIndices& queueFamilies() const { return m_queueFamilies; }
    VkCommandPool commandPool() const { return m_commandPool; }
    VmaAllocator allocator() const { return m_allocator; }
    
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createAllocator();
    
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace myth
