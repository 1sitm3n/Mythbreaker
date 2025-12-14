#include "VulkanTexture.h"
#include "engine/Logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <random>

namespace myth {
namespace vk {

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
    : m_context(other.m_context), m_image(other.m_image), m_allocation(other.m_allocation),
      m_imageView(other.m_imageView), m_sampler(other.m_sampler),
      m_width(other.m_width), m_height(other.m_height) {
    other.m_context = nullptr;
    other.m_image = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
    other.m_imageView = VK_NULL_HANDLE;
    other.m_sampler = VK_NULL_HANDLE;
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
    if (this != &other) {
        destroy();
        m_context = other.m_context;
        m_image = other.m_image;
        m_allocation = other.m_allocation;
        m_imageView = other.m_imageView;
        m_sampler = other.m_sampler;
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_context = nullptr;
        other.m_image = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_imageView = VK_NULL_HANDLE;
        other.m_sampler = VK_NULL_HANDLE;
    }
    return *this;
}

bool VulkanTexture::loadFromFile(VulkanContext* context, const std::string& filepath) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    if (!pixels) {
        Logger::errorf("Failed to load texture: {}", filepath);
        return false;
    }
    
    bool result = loadFromMemory(context, pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    stbi_image_free(pixels);
    
    if (result) {
        Logger::infof("Loaded texture: {} ({}x{})", filepath, texWidth, texHeight);
    }
    return result;
}

bool VulkanTexture::loadFromMemory(VulkanContext* context, const uint8_t* pixels, uint32_t width, uint32_t height) {
    m_context = context;
    m_width = width;
    m_height = height;
    
    VkDeviceSize imageSize = width * height * 4;
    
    // Create staging buffer
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    vmaCreateBuffer(context->allocator(), &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr);
    
    // Copy pixels to staging buffer
    void* data;
    vmaMapMemory(context->allocator(), stagingAllocation, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(context->allocator(), stagingAllocation);
    
    // Create image
    createImage(context, width, height, VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_TILING_OPTIMAL, 
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Transition and copy
    transitionLayout(context, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(context, stagingBuffer);
    transitionLayout(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Cleanup staging
    vmaDestroyBuffer(context->allocator(), stagingBuffer, stagingAllocation);
    
    // Create view and sampler
    createImageView(context, VK_FORMAT_R8G8B8A8_SRGB);
    createSampler(context);
    
    return true;
}

VulkanTexture VulkanTexture::createCheckerboard(VulkanContext* context, uint32_t size, uint32_t squares) {
    std::vector<uint8_t> pixels(size * size * 4);
    uint32_t squareSize = size / squares;
    
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            uint32_t idx = (y * size + x) * 4;
            bool white = ((x / squareSize) + (y / squareSize)) % 2 == 0;
            uint8_t color = white ? 200 : 50;
            pixels[idx + 0] = color;
            pixels[idx + 1] = color;
            pixels[idx + 2] = color;
            pixels[idx + 3] = 255;
        }
    }
    
    VulkanTexture tex;
    tex.loadFromMemory(context, pixels.data(), size, size);
    return tex;
}

VulkanTexture VulkanTexture::createSolidColor(VulkanContext* context, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t pixels[4] = {r, g, b, a};
    VulkanTexture tex;
    tex.loadFromMemory(context, pixels, 1, 1);
    return tex;
}

VulkanTexture VulkanTexture::createNoise(VulkanContext* context, uint32_t size, uint32_t seed) {
    std::vector<uint8_t> pixels(size * size * 4);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    
    for (uint32_t i = 0; i < size * size; i++) {
        uint8_t v = static_cast<uint8_t>(dist(rng));
        pixels[i * 4 + 0] = v;
        pixels[i * 4 + 1] = v;
        pixels[i * 4 + 2] = v;
        pixels[i * 4 + 3] = 255;
    }
    
    VulkanTexture tex;
    tex.loadFromMemory(context, pixels.data(), size, size);
    return tex;
}

void VulkanTexture::destroy() {
    if (!m_context) return;
    
    if (m_sampler) {
        vkDestroySampler(m_context->device(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView) {
        vkDestroyImageView(m_context->device(), m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image) {
        vmaDestroyImage(m_context->allocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
    m_context = nullptr;
}

void VulkanTexture::createImage(VulkanContext* context, uint32_t width, uint32_t height, VkFormat format,
                                 VkImageTiling tiling, VkImageUsageFlags usage) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    vmaCreateImage(context->allocator(), &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr);
}

void VulkanTexture::createImageView(VulkanContext* context, VkFormat format) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkCreateImageView(context->device(), &viewInfo, nullptr, &m_imageView);
}

void VulkanTexture::createSampler(VulkanContext* context) {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    vkCreateSampler(context->device(), &samplerInfo, nullptr, &m_sampler);
}

void VulkanTexture::transitionLayout(VulkanContext* context, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer cmd = context->beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags srcStage, dstStage;
    
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    context->endSingleTimeCommands(cmd);
}

void VulkanTexture::copyBufferToImage(VulkanContext* context, VkBuffer buffer) {
    VkCommandBuffer cmd = context->beginSingleTimeCommands();
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {m_width, m_height, 1};
    
    vkCmdCopyBufferToImage(cmd, buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    context->endSingleTimeCommands(cmd);
}

} // namespace vk
} // namespace myth
