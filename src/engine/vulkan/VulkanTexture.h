#pragma once

#include "VulkanContext.h"
#include <string>
#include <vector>

namespace myth {
namespace vk {

class VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() { destroy(); }
    
    // No copy
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    
    // Move
    VulkanTexture(VulkanTexture&& other) noexcept;
    VulkanTexture& operator=(VulkanTexture&& other) noexcept;
    
    // Create from file
    bool loadFromFile(VulkanContext* context, const std::string& filepath);
    
    // Create from raw pixel data (RGBA)
    bool loadFromMemory(VulkanContext* context, const uint8_t* pixels, uint32_t width, uint32_t height);
    
    // Create procedural textures
    static VulkanTexture createCheckerboard(VulkanContext* context, uint32_t size, uint32_t squares);
    static VulkanTexture createSolidColor(VulkanContext* context, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    static VulkanTexture createNoise(VulkanContext* context, uint32_t size, uint32_t seed = 0);
    
    void destroy();
    
    VkImageView view() const { return m_imageView; }
    VkSampler sampler() const { return m_sampler; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    bool isValid() const { return m_image != VK_NULL_HANDLE; }

private:
    void createImage(VulkanContext* context, uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage);
    void createImageView(VulkanContext* context, VkFormat format);
    void createSampler(VulkanContext* context);
    void transitionLayout(VulkanContext* context, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VulkanContext* context, VkBuffer buffer);

    VulkanContext* m_context = nullptr;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace vk
} // namespace myth
