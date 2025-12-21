#include "VulkanDescriptors.h"
#include "../Logger.h"
#include <cstring>

namespace myth {
namespace vk {

void DescriptorManager::init(VulkanContext* context) {
    m_context = context;
    createDescriptorSetLayout();
    createSkinnedDescriptorSetLayout();
    createShadowDescriptorSetLayout();
    createDescriptorPool();
    createUniformBuffers();
    createJointMatrixBuffers();
    createLightSpaceBuffers();
    createCameraDescriptorSets();
    createSkinnedDescriptorSets();
    createShadowDescriptorSets();
}

void DescriptorManager::destroy() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vmaDestroyBuffer(m_context->allocator(), m_uniformBuffers[i], m_uniformAllocations[i]);
        vmaDestroyBuffer(m_context->allocator(), m_jointBuffers[i], m_jointAllocations[i]);
        vmaDestroyBuffer(m_context->allocator(), m_lightSpaceBuffers[i], m_lightSpaceAllocations[i]);
    }
    vkDestroyDescriptorPool(m_context->device(), m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_context->device(), m_descriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_context->device(), m_skinnedDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_context->device(), m_shadowDescriptorSetLayout, nullptr);
}

void DescriptorManager::createDescriptorSetLayout() {
    // bindings: 0=camera, 1=texture, 3=shadowMap
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 3;  // Shadow map at binding 3
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(m_context->device(), &layoutInfo, nullptr, &m_descriptorSetLayout);
}

void DescriptorManager::createSkinnedDescriptorSetLayout() {
    // bindings: 0=camera, 1=texture, 2=joints, 3=shadowMap
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[3].binding = 3;  // Shadow map at binding 3
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(m_context->device(), &layoutInfo, nullptr, &m_skinnedDescriptorSetLayout);
}

void DescriptorManager::createShadowDescriptorSetLayout() {
    // Shadow pass only needs light space matrix
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    vkCreateDescriptorSetLayout(m_context->device(), &layoutInfo, nullptr, &m_shadowDescriptorSetLayout);
}

void DescriptorManager::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 5 + 100;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 4 + 100;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * 4 + 100;

    vkCreateDescriptorPool(m_context->device(), &poolInfo, nullptr, &m_descriptorPool);
}

void DescriptorManager::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(CameraUBO);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        vmaCreateBuffer(m_context->allocator(), &bufferInfo, &allocInfo,
                        &m_uniformBuffers[i], &m_uniformAllocations[i], &allocationInfo);
        m_uniformMapped[i] = allocationInfo.pMappedData;
    }
}

void DescriptorManager::createJointMatrixBuffers() {
    VkDeviceSize bufferSize = sizeof(JointMatricesUBO);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        vmaCreateBuffer(m_context->allocator(), &bufferInfo, &allocInfo,
                        &m_jointBuffers[i], &m_jointAllocations[i], &allocationInfo);
        m_jointMapped[i] = allocationInfo.pMappedData;

        JointMatricesUBO* joints = static_cast<JointMatricesUBO*>(m_jointMapped[i]);
        for (int j = 0; j < 128; j++) {
            joints->jointMatrices[j] = glm::mat4(1.0f);
        }
    }
}

void DescriptorManager::createLightSpaceBuffers() {
    VkDeviceSize bufferSize = sizeof(glm::mat4);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        vmaCreateBuffer(m_context->allocator(), &bufferInfo, &allocInfo,
                        &m_lightSpaceBuffers[i], &m_lightSpaceAllocations[i], &allocationInfo);
        m_lightSpaceMapped[i] = allocationInfo.pMappedData;
        
        // Initialize to identity
        glm::mat4 identity(1.0f);
        memcpy(m_lightSpaceMapped[i], &identity, sizeof(glm::mat4));
    }
}

void DescriptorManager::createCameraDescriptorSets() {
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    vkAllocateDescriptorSets(m_context->device(), &allocInfo, m_cameraDescriptorSets.data());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_cameraDescriptorSets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }
}

void DescriptorManager::createSkinnedDescriptorSets() {
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_skinnedDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    vkAllocateDescriptorSets(m_context->device(), &allocInfo, m_skinnedDescriptorSets.data());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = m_uniformBuffers[i];
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(CameraUBO);

        VkDescriptorBufferInfo jointInfo{};
        jointInfo.buffer = m_jointBuffers[i];
        jointInfo.offset = 0;
        jointInfo.range = sizeof(JointMatricesUBO);

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_skinnedDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_skinnedDescriptorSets[i];
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &jointInfo;

        vkUpdateDescriptorSets(m_context->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void DescriptorManager::createShadowDescriptorSets() {
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_shadowDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    vkAllocateDescriptorSets(m_context->device(), &allocInfo, m_shadowDescriptorSets.data());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_lightSpaceBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(glm::mat4);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_shadowDescriptorSets[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }
}

void DescriptorManager::updateCameraUBO(uint32_t frameIndex, const CameraUBO& ubo) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        memcpy(m_uniformMapped[i], &ubo, sizeof(CameraUBO));
    }
    // Also update light space buffer
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        memcpy(m_lightSpaceMapped[i], &ubo.lightSpaceMatrix, sizeof(glm::mat4));
    }
}

void DescriptorManager::updateJointMatrices(uint32_t frameIndex, const glm::mat4* matrices, uint32_t count) {
    if (count > 128) count = 128;
    memcpy(m_jointMapped[frameIndex], matrices, count * sizeof(glm::mat4));
}

void DescriptorManager::bindSkinnedDescriptor(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frameIndex) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &m_skinnedDescriptorSets[frameIndex], 0, nullptr);
}

void DescriptorManager::setShadowMap(VkImageView shadowView, VkSampler shadowSampler) {
    m_shadowImageView = shadowView;
    m_shadowSampler = shadowSampler;
    
    // Update all descriptor sets with shadow map
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = shadowView;
        imageInfo.sampler = shadowSampler;

        // Update camera descriptor sets (binding 3)
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_cameraDescriptorSets[i];
        write.dstBinding = 3;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);

        // Update skinned descriptor sets (binding 3)
        write.dstSet = m_skinnedDescriptorSets[i];
        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }
    
    // Also update all material sets
    for (auto& materialSet : m_materialSets) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = shadowView;
        imageInfo.sampler = shadowSampler;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = materialSet;
        write.dstBinding = 3;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }
    
    Logger::info("Shadow map bound to descriptors");
}

uint32_t DescriptorManager::createMaterial(const VulkanTexture& texture) {
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    VkDescriptorSet materialSet;
    vkAllocateDescriptorSets(m_context->device(), &allocInfo, &materialSet);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffers[0];
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(CameraUBO);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture.view();
    imageInfo.sampler = texture.sampler();

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = materialSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &bufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = materialSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_context->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // If shadow map is set, add it
    if (m_shadowImageView != VK_NULL_HANDLE) {
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = m_shadowImageView;
        shadowInfo.sampler = m_shadowSampler;

        VkWriteDescriptorSet shadowWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        shadowWrite.dstSet = materialSet;
        shadowWrite.dstBinding = 3;
        shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowWrite.descriptorCount = 1;
        shadowWrite.pImageInfo = &shadowInfo;

        vkUpdateDescriptorSets(m_context->device(), 1, &shadowWrite, 0, nullptr);
    }

    uint32_t materialId = static_cast<uint32_t>(m_materialSets.size());
    m_materialSets.push_back(materialSet);
    return materialId;
}

void DescriptorManager::bindMaterial(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frameIndex, uint32_t materialId) {
    if (materialId >= m_materialSets.size()) return;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &m_materialSets[materialId], 0, nullptr);
}

void DescriptorManager::setSkinnedTexture(const VulkanTexture& texture) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture.view();
        imageInfo.sampler = texture.sampler();

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_skinnedDescriptorSets[i];
        write.dstBinding = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_context->device(), 1, &write, 0, nullptr);
    }
}

} // namespace vk
} // namespace myth
