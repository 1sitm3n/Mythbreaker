#include "VulkanDescriptors.h"
#include <cstring>

namespace myth {
namespace vk {

void DescriptorManager::init(VulkanContext* context) {
    m_context = context;
    createLayout();
    createPool();
    createUniformBuffers();
    createDescriptorSets();
}

void DescriptorManager::destroy() {
    for (auto& buf : m_uniformBuffers) buf.destroy();
    if (m_pool) vkDestroyDescriptorPool(m_context->device(), m_pool, nullptr);
    if (m_layout) vkDestroyDescriptorSetLayout(m_context->device(), m_layout, nullptr);
}

void DescriptorManager::updateCameraUBO(uint32_t frameIndex, const CameraUBO& ubo) {
    memcpy(m_mappedBuffers[frameIndex], &ubo, sizeof(CameraUBO));
}

void DescriptorManager::createLayout() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_context->device(), &layoutInfo, nullptr, &m_layout), "Failed to create descriptor set layout");
}

void DescriptorManager::createPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(m_context->device(), &poolInfo, nullptr, &m_pool), "Failed to create descriptor pool");
}

void DescriptorManager::createUniformBuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_uniformBuffers[i].create(m_context, sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_mappedBuffers[i] = m_uniformBuffers[i].map();
    }
}

void DescriptorManager::createDescriptorSets() {
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_layout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(m_context->device(), &allocInfo, m_sets.data()), "Failed to allocate descriptor sets");
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i].buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO);
        
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_sets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }
}

} // namespace vk
} // namespace myth
