#pragma once

#include "VulkanTypes.h"
#include "VulkanContext.h"
#include <vk_mem_alloc.h>

namespace myth {
namespace vk {

class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer();
    
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    
    void create(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage);
    void destroy();
    
    void* map();
    void unmap();
    void copyData(const void* data, size_t size);
    
    VkBuffer buffer() const { return m_buffer; }
    VkDeviceSize size() const { return m_size; }
    
    static void createWithStaging(VulkanContext* ctx, VulkanBuffer& dst, const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

private:
    VulkanContext* m_context = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mapped = nullptr;
};

} // namespace vk
} // namespace myth
