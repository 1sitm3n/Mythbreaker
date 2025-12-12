#include "VulkanBuffer.h"
#include <cstring>

namespace myth {
namespace vk {

VulkanBuffer::~VulkanBuffer() { destroy(); }

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : m_context(other.m_context), m_buffer(other.m_buffer), m_allocation(other.m_allocation), m_size(other.m_size), m_mapped(other.m_mapped) {
    other.m_context = nullptr; other.m_buffer = VK_NULL_HANDLE; other.m_allocation = VK_NULL_HANDLE; other.m_size = 0; other.m_mapped = nullptr;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_context = other.m_context; m_buffer = other.m_buffer; m_allocation = other.m_allocation; m_size = other.m_size; m_mapped = other.m_mapped;
        other.m_context = nullptr; other.m_buffer = VK_NULL_HANDLE; other.m_allocation = VK_NULL_HANDLE; other.m_size = 0; other.m_mapped = nullptr;
    }
    return *this;
}

void VulkanBuffer::create(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage) {
    m_context = ctx;
    m_size = size;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    if (memUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocInfoResult;
    VK_CHECK(vmaCreateBuffer(ctx->allocator(), &bufferInfo, &allocInfo, &m_buffer, &m_allocation, &allocInfoResult), "Failed to create buffer");
    if (memUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) m_mapped = allocInfoResult.pMappedData;
}

void VulkanBuffer::destroy() {
    if (m_buffer && m_context) {
        vmaDestroyBuffer(m_context->allocator(), m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE; m_allocation = VK_NULL_HANDLE; m_mapped = nullptr;
    }
}

void* VulkanBuffer::map() {
    if (!m_mapped) vmaMapMemory(m_context->allocator(), m_allocation, &m_mapped);
    return m_mapped;
}

void VulkanBuffer::unmap() {
    if (m_mapped) { vmaUnmapMemory(m_context->allocator(), m_allocation); m_mapped = nullptr; }
}

void VulkanBuffer::copyData(const void* data, size_t size) {
    void* mapped = map();
    memcpy(mapped, data, size);
}

void VulkanBuffer::createWithStaging(VulkanContext* ctx, VulkanBuffer& dst, const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    VulkanBuffer staging;
    staging.create(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.copyData(data, size);
    dst.create(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.buffer(), dst.buffer(), 1, &copyRegion);
    ctx->endSingleTimeCommands(cmd);
}

} // namespace vk
} // namespace myth
