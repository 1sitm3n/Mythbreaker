#include "SceneRenderer.h"
#include "MeshAsset.h"
#include "engine/Logger.h"
#include "engine/vulkan/VulkanContext.h"
#include "engine/vulkan/VulkanSwapchain.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace myth::engine {
namespace fs = std::filesystem;
namespace {
constexpr std::uint32_t ShadowSize = 2048;
constexpr VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_SRGB;

bool isSrgbFormat(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_A8B8G8R8_SRGB_PACK32 || format == VK_FORMAT_B8G8R8_SRGB ||
           format == VK_FORMAT_R8G8B8_SRGB;
}

struct Buffer {
    vk::VulkanContext* context = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    Buffer() = default;
    ~Buffer() { reset(); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    void reset() {
        if (buffer && context && context->allocator()) vmaDestroyBuffer(context->allocator(), buffer, allocation);
        buffer = VK_NULL_HANDLE; allocation = VK_NULL_HANDLE; mapped = nullptr; context = nullptr;
    }
    void create(vk::VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory, bool map = false) {
        reset(); context = &ctx;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo alloc{}; alloc.usage = memory;
        if (map) alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo result{};
        VK_CHECK(vmaCreateBuffer(ctx.allocator(), &info, &alloc, &buffer, &allocation, &result), "Cannot allocate renderer buffer");
        mapped = result.pMappedData;
    }
    void upload(const void* data, VkDeviceSize size) {
        if (!mapped) VK_CHECK(vmaMapMemory(context->allocator(), allocation, &mapped), "Cannot map renderer buffer");
        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        VK_CHECK(vmaFlushAllocation(context->allocator(), allocation, 0, size), "Cannot flush renderer buffer");
    }
};

struct Image {
    vk::VulkanContext* context = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Image() = default;
    ~Image() { reset(); }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    void reset() {
        if (context && context->device()) {
            if (sampler) vkDestroySampler(context->device(), sampler, nullptr);
            if (view) vkDestroyImageView(context->device(), view, nullptr);
            if (image && context->allocator()) vmaDestroyImage(context->allocator(), image, allocation);
        }
        image = VK_NULL_HANDLE; view = VK_NULL_HANDLE; sampler = VK_NULL_HANDLE; allocation = VK_NULL_HANDLE; context = nullptr;
    }
    void create(vk::VulkanContext& ctx, std::uint32_t width, std::uint32_t height, VkFormat format,
                VkImageUsageFlags usage, VkImageAspectFlags aspect, bool sampled = false, bool comparison = false,
                VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT) {
        reset(); context = &ctx;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.extent = {width, height, 1};
        info.mipLevels = 1; info.arrayLayers = 1; info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL; info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage; info.samples = VK_SAMPLE_COUNT_1_BIT; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{}; allocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VK_CHECK(vmaCreateImage(ctx.allocator(), &info, &allocationInfo, &image, &allocation, nullptr), "Cannot allocate renderer image");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = format;
        viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(ctx.device(), &viewInfo, nullptr, &view), "Cannot create renderer image view");
        if (sampled) {
            VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice(), format, &properties);
            VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.magFilter = samplerInfo.minFilter = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = comparison ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : addressMode;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            samplerInfo.compareEnable = comparison; samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            samplerInfo.minLod = samplerInfo.maxLod = 0.0f;
            VK_CHECK(vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &sampler), "Cannot create renderer sampler");
        }
    }
};

struct GpuTexture {
    Image image;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

struct GpuPart {
    std::uint32_t firstIndex = 0, indexCount = 0;
    glm::vec4 color{1.0f};
    GpuTexture* albedo = nullptr;
};

struct GpuMesh {
    Buffer vertices, indices;
    std::vector<GpuPart> parts;
    glm::vec3 minimum{0.0f}, maximum{0.0f};
    bool failed = false;
    std::shared_ptr<detail::MeshAsset> animation;
};

struct GpuPose {
    Buffer vertices;
    VkDeviceSize byteSize = 0;
    GpuMesh* source = nullptr;
    int clipIndex = -1;
    float time = -1.0f;
    bool loop = false;
    glm::vec3 minimum{0}, maximum{0};
};

struct alignas(16) FrameData {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 lightViewProjection{1.0f};
    glm::vec4 cameraExposure{0.0f};
    glm::vec4 ambient{0.0f};
    glm::vec4 sunDirection{-0.45f, -1.0f, -0.3f, 0.0f};
    glm::vec4 sunColor{1.0f};
    std::array<glm::vec4, 8> pointPosition{};
    std::array<glm::vec4, 8> pointColor{};
    glm::ivec4 counts{0};
};

struct ObjectData {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
    glm::vec4 material{0.6f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(ObjectData) == 96);
static_assert(offsetof(FrameData, counts) == 448);

struct Draw {
    GpuMesh* mesh = nullptr;
    VkBuffer vertices = VK_NULL_HANDLE;
    GpuTexture* textureOverride = nullptr;
    ObjectData object;
    bool visible = true;
    bool castsShadow = true;
};

bool visibleBounds(const glm::mat4& viewProjection, const glm::mat4& model,
                   const glm::vec3& minimum, const glm::vec3& maximum) {
    const auto transform = viewProjection * model;
    std::array<glm::vec4, 8> corners;
    for (int i = 0; i < 8; ++i) corners[i] = transform * glm::vec4(
        i & 1 ? maximum.x : minimum.x, i & 2 ? maximum.y : minimum.y, i & 4 ? maximum.z : minimum.z, 1.0f);
    for (int plane = 0; plane < 6; ++plane) {
        bool allOutside = true;
        for (const auto& p : corners) {
            const bool outside = plane == 0 ? p.x < -p.w : plane == 1 ? p.x > p.w : plane == 2 ? p.y < -p.w :
                                 plane == 3 ? p.y > p.w : plane == 4 ? p.z < 0.0f : p.z > p.w;
            if (!outside) { allOutside = false; break; }
        }
        if (allOutside) return false;
    }
    return true;
}

std::string assetKey(const std::string& source, const fs::path& root) {
    if (source.starts_with("builtin:")) return source;
    return (fs::weakly_canonical(root) / fs::path(source)).lexically_normal().generic_string();
}

VkFormat findDepthFormat(vk::VulkanContext& context, bool sampled) {
    for (const auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), format, &properties);
        const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | (sampled ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT : 0);
        if ((properties.optimalTilingFeatures & required) == required) return format;
    }
    throw std::runtime_error("GPU has no supported depth format");
}

VkShaderModule loadShader(VkDevice device, const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Shader missing: " + path.string() + ". Build the MythbreakerShaders target.");
    const auto size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) % 4 != 0) throw std::runtime_error("Invalid SPIR-V file: " + path.string());
    std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) throw std::runtime_error("Cannot read shader: " + path.string());
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = static_cast<std::size_t>(size); info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module), "Cannot create shader module");
    return module;
}
} // namespace

struct SceneRenderer::Impl {
    vk::VulkanContext context;
    vk::VulkanSwapchain swapchain;
    GLFWwindow* window = nullptr;
    fs::path shaderDirectory;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence frameFence = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinished;
    std::uint32_t imageIndex = 0;
    bool frameAcquired = false, initialized = false, glfwBackend = false, vulkanBackend = false;
    bool pendingSwapchain = false, hasRendered = false;
    int framebufferWidth = 0, framebufferHeight = 0;
    std::uint32_t width = 1280, height = 720, requestedWidth = 1280, requestedHeight = 720;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED, shadowFormat = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout frameLayout = VK_NULL_HANDLE, textureLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet frameSet = VK_NULL_HANDLE, sceneTextureSet = VK_NULL_HANDLE;
    VkDescriptorSet viewportSet = VK_NULL_HANDLE;
    Buffer frameBuffer;
    Image color, depth, shadow;
    VkRenderPass scenePass = VK_NULL_HANDLE, shadowPass = VK_NULL_HANDLE;
    VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE, shadowFramebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, guiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline scenePipeline = VK_NULL_HANDLE, shadowPipeline = VK_NULL_HANDLE, presentPipeline = VK_NULL_HANDLE, guiPipeline = VK_NULL_HANDLE;
    std::unordered_map<std::string, std::unique_ptr<GpuMesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<GpuTexture>> textures;
    std::unordered_set<std::string> failedTextures;
    std::unordered_map<std::string, std::unique_ptr<GpuPose>> poses;
    GpuTexture* white = nullptr;
    RendererStats statistics;
    std::string assetError;

    VkDescriptorSet textureDescriptor(const Image& image, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptorPool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &textureLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocate, &set), "Renderer texture descriptor capacity exhausted");
        VkDescriptorImageInfo imageInfo{image.sampler, image.view, layout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(context.device(), 1, &write, 0, nullptr);
        return set;
    }

    void createDescriptors() {
        std::array<VkDescriptorSetLayoutBinding, 2> frameBindings{};
        frameBindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        frameBindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<std::uint32_t>(frameBindings.size()); layoutInfo.pBindings = frameBindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(context.device(), &layoutInfo, nullptr, &frameLayout), "Cannot create scene descriptor layout");
        VkDescriptorSetLayoutBinding textureBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        layoutInfo.bindingCount = 1; layoutInfo.pBindings = &textureBinding;
        VK_CHECK(vkCreateDescriptorSetLayout(context.device(), &layoutInfo, nullptr, &textureLayout), "Cannot create material descriptor layout");
        std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096}}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; poolInfo.maxSets = 4096;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size()); poolInfo.pPoolSizes = sizes.data();
        VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &descriptorPool), "Cannot create renderer descriptor pool");
        frameBuffer.create(context, sizeof(FrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, true);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptorPool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &frameLayout;
        VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocate, &frameSet), "Cannot allocate scene descriptors");
        VkDescriptorBufferInfo bufferInfo{frameBuffer.buffer, 0, sizeof(FrameData)};
        VkDescriptorImageInfo imageInfo{shadow.sampler, shadow.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = writes[1].dstSet = frameSet;
        writes[0].descriptorCount = writes[1].descriptorCount = 1;
        writes[0].dstBinding = 0; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[0].pBufferInfo = &bufferInfo;
        writes[1].dstBinding = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(context.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        std::array<VkDescriptorSetLayout, 2> layouts{frameLayout, textureLayout};
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectData)};
        VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size()); pipelineInfo.pSetLayouts = layouts.data();
        pipelineInfo.pushConstantRangeCount = 1; pipelineInfo.pPushConstantRanges = &push;
        VK_CHECK(vkCreatePipelineLayout(context.device(), &pipelineInfo, nullptr, &pipelineLayout), "Cannot create renderer pipeline layout");
        // ImGui accepts a custom draw pipeline. Its public backend uses one
        // combined sampler and a 16-byte vertex push constant range; matching
        // that layout lets us color-manage UI without changing vendor sources.
        VkPushConstantRange guiPush{VK_SHADER_STAGE_VERTEX_BIT, 0, 4 * sizeof(float)};
        pipelineInfo.setLayoutCount = 1; pipelineInfo.pSetLayouts = &textureLayout;
        pipelineInfo.pPushConstantRanges = &guiPush;
        VK_CHECK(vkCreatePipelineLayout(context.device(), &pipelineInfo, nullptr, &guiPipelineLayout), "Cannot create UI pipeline layout");
    }

    VkRenderPass createRenderPass(bool depthOnly) {
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = depthOnly ? shadowFormat : ColorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT; attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = depthOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].format = depthFormat; attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthReference{depthOnly ? 0u : 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = depthOnly ? 0u : 1u; subpass.pColorAttachments = depthOnly ? nullptr : &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0; dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = depthOnly ? 1u : 2u; info.pAttachments = attachments.data();
        info.subpassCount = 1; info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<std::uint32_t>(dependencies.size()); info.pDependencies = dependencies.data();
        VkRenderPass pass = VK_NULL_HANDLE;
        VK_CHECK(vkCreateRenderPass(context.device(), &info, nullptr, &pass), "Cannot create scene render pass");
        return pass;
    }

    VkPipeline createPipeline(VkRenderPass pass, const std::string& vertexName, const std::string& fragmentName,
                              bool depthOnly, bool fullscreen, bool gui = false) {
        const auto vertex = loadShader(context.device(), shaderDirectory / (vertexName + ".spv"));
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            if (!fragmentName.empty()) fragment = loadShader(context.device(), shaderDirectory / (fragmentName + ".spv"));
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = "main";
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment; stages[1].pName = "main";
            const VkBool32 outputIsSrgb = isSrgbFormat(swapchain.imageFormat()) ? VK_TRUE : VK_FALSE;
            VkSpecializationMapEntry outputEntry{0, 0, sizeof(outputIsSrgb)};
            VkSpecializationInfo outputInfo{1, &outputEntry, sizeof(outputIsSrgb), &outputIsSrgb};
            if (fullscreen || gui) stages[1].pSpecializationInfo = &outputInfo;
            VkVertexInputBindingDescription binding{0, static_cast<std::uint32_t>(gui ? sizeof(ImDrawVert) : sizeof(detail::MeshVertex)), VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription, 4> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(detail::MeshVertex, position)},
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(detail::MeshVertex, normal)},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(detail::MeshVertex, uv)},
                {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(detail::MeshVertex, color)}}};
            std::array<VkVertexInputAttributeDescription, 3> guiAttributes{{
                {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
                {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
                {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)}}};
            VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            input.vertexBindingDescriptionCount = fullscreen ? 0u : 1u; input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = fullscreen ? 0u : depthOnly ? 1u : gui ? 3u : static_cast<std::uint32_t>(attributes.size());
            input.pVertexAttributeDescriptions = gui ? guiAttributes.data() : attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = 1; viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1.0f;
            raster.depthBiasEnable = depthOnly; raster.depthBiasConstantFactor = 1.25f; raster.depthBiasSlopeFactor = 1.75f;
            VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depthState.depthTestEnable = !fullscreen && !gui; depthState.depthWriteEnable = !fullscreen && !gui;
            depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachment.blendEnable = gui;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount = depthOnly ? 0u : 1u; blend.pAttachments = depthOnly ? nullptr : &blendAttachment;
            std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()); dynamic.pDynamicStates = dynamicStates.data();
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = fragment ? 2u : 1u; info.pStages = stages.data();
            info.pVertexInputState = &input; info.pInputAssemblyState = &assembly; info.pViewportState = &viewport;
            info.pRasterizationState = &raster; info.pMultisampleState = &multisample; info.pDepthStencilState = &depthState;
            info.pColorBlendState = &blend; info.pDynamicState = &dynamic; info.layout = gui ? guiPipelineLayout : pipelineLayout; info.renderPass = pass;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VK_CHECK(vkCreateGraphicsPipelines(context.device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "Cannot create renderer graphics pipeline");
            vkDestroyShaderModule(context.device(), vertex, nullptr);
            if (fragment) vkDestroyShaderModule(context.device(), fragment, nullptr);
            return pipeline;
        } catch (...) {
            vkDestroyShaderModule(context.device(), vertex, nullptr);
            if (fragment) vkDestroyShaderModule(context.device(), fragment, nullptr);
            throw;
        }
    }

    void initializeGuiVulkan() {
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_2; info.Instance = context.instance(); info.PhysicalDevice = context.physicalDevice();
        info.Device = context.device(); info.QueueFamily = context.graphicsFamily(); info.Queue = context.graphicsQueue();
        info.DescriptorPoolSize = 1024; info.RenderPass = swapchain.renderPass(); info.MinImageCount = 2;
        info.ImageCount = swapchain.imageCount(); info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.CheckVkResultFn = [](VkResult result) {
            if (result < 0) throw std::runtime_error("Dear ImGui Vulkan error: " + std::to_string(result));
        };
        if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("Cannot initialize Dear ImGui Vulkan backend");
        vulkanBackend = true;
        if (color.view) viewportSet = ImGui_ImplVulkan_AddTexture(color.sampler, color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void destroyViewport() {
        if (viewportSet && vulkanBackend) ImGui_ImplVulkan_RemoveTexture(viewportSet);
        viewportSet = VK_NULL_HANDLE;
        if (sceneTextureSet && descriptorPool) vkFreeDescriptorSets(context.device(), descriptorPool, 1, &sceneTextureSet);
        sceneTextureSet = VK_NULL_HANDLE;
        if (sceneFramebuffer) vkDestroyFramebuffer(context.device(), sceneFramebuffer, nullptr);
        sceneFramebuffer = VK_NULL_HANDLE;
        color.reset(); depth.reset(); hasRendered = false;
    }

    void createViewport() {
        color.create(context, width, height, ColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, true, false, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        depth.create(context, width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        std::array<VkImageView, 2> attachments{color.view, depth.view};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = scenePass; info.attachmentCount = static_cast<std::uint32_t>(attachments.size()); info.pAttachments = attachments.data();
        info.width = width; info.height = height; info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(context.device(), &info, nullptr, &sceneFramebuffer), "Cannot create viewport framebuffer");
        sceneTextureSet = textureDescriptor(color);
        if (vulkanBackend) viewportSet = ImGui_ImplVulkan_AddTexture(color.sampler, color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void createPresentationSemaphores() {
        for (const auto semaphore : renderFinished) if (semaphore) vkDestroySemaphore(context.device(), semaphore, nullptr);
        renderFinished.assign(swapchain.imageCount(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& semaphore : renderFinished) VK_CHECK(vkCreateSemaphore(context.device(), &info, nullptr, &semaphore), "Cannot create presentation semaphore");
    }

    void rebuildSwapchain() {
        VK_CHECK(vkDeviceWaitIdle(context.device()), "Cannot wait for resize");
        if (viewportSet && vulkanBackend) ImGui_ImplVulkan_RemoveTexture(viewportSet);
        viewportSet = VK_NULL_HANDLE;
        if (vulkanBackend) { ImGui_ImplVulkan_Shutdown(); vulkanBackend = false; }
        // The docking Vulkan backend also destroys platform viewport data,
        // including the main window handle. Restart the GLFW backend together
        // with it so the next mouse/input update never observes a null handle.
        if (glfwBackend) { ImGui_ImplGlfw_Shutdown(); glfwBackend = false; }
        if (presentPipeline) vkDestroyPipeline(context.device(), presentPipeline, nullptr);
        if (guiPipeline) vkDestroyPipeline(context.device(), guiPipeline, nullptr);
        presentPipeline = guiPipeline = VK_NULL_HANDLE;
        swapchain.recreate();
        createPresentationSemaphores();
        presentPipeline = createPipeline(swapchain.renderPass(), "engine_present.vert", "engine_present.frag", false, true);
        guiPipeline = createPipeline(swapchain.renderPass(), "engine_gui.vert", "engine_gui.frag", false, false, true);
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("Cannot restart Dear ImGui GLFW backend");
        glfwBackend = true;
        initializeGuiVulkan();
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        pendingSwapchain = false;
    }

    GpuTexture* uploadTexture(const std::string& key, const detail::TexturePixels& pixels) {
        if (const auto it = textures.find(key); it != textures.end()) return it->second.get();
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
        if (!pixels.width || !pixels.height || pixels.width > properties.limits.maxImageDimension2D || pixels.height > properties.limits.maxImageDimension2D ||
            pixels.rgba.size() != static_cast<std::size_t>(pixels.width) * pixels.height * 4)
            throw std::runtime_error("Texture dimensions are invalid or unsupported by this GPU");
        auto texture = std::make_unique<GpuTexture>();
        texture->image.create(context, pixels.width, pixels.height, VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
        Buffer staging;
        staging.create(context, pixels.rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
        staging.upload(pixels.rgba.data(), pixels.rgba.size());
        const auto upload = context.beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = texture->image.image; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(upload, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {pixels.width, pixels.height, 1};
        vkCmdCopyBufferToImage(upload, staging.buffer, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(upload, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        context.endSingleTimeCommands(upload);
        texture->descriptor = textureDescriptor(texture->image);
        auto* pointer = texture.get(); textures.emplace(key, std::move(texture)); return pointer;
    }

    GpuTexture* textureFile(const std::string& source, const fs::path& root) {
        if (source.empty()) return nullptr;
        const std::string key = assetKey(source, root);
        if (auto it = textures.find(key); it != textures.end()) return it->second.get();
        if (failedTextures.contains(key)) return white;
        try {
            if (!isSafeRelativePath(source) || !detail::insideRoot(root / source, root)) throw std::runtime_error("Texture path escapes the project");
            return uploadTexture(key, detail::loadTexture(root / source));
        } catch (const std::exception& e) {
            assetError = source + ": " + e.what(); failedTextures.insert(key); return white;
        }
    }

    void uploadMesh(GpuMesh& mesh, const detail::MeshAsset& data, const std::string& key) {
        std::vector<detail::MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::unordered_map<const detail::TexturePixels*, GpuTexture*> imageMap;
        for (const auto& part : data.parts) {
            GpuPart gpuPart;
            gpuPart.firstIndex = static_cast<std::uint32_t>(indices.size()); gpuPart.indexCount = static_cast<std::uint32_t>(part.indices.size());
            gpuPart.color = part.color; gpuPart.albedo = white;
            if (part.albedo) {
                if (auto it = imageMap.find(part.albedo.get()); it != imageMap.end()) gpuPart.albedo = it->second;
                else {
                    gpuPart.albedo = uploadTexture(key + "#image" + std::to_string(imageMap.size()), *part.albedo);
                    imageMap.emplace(part.albedo.get(), gpuPart.albedo);
                }
            }
            const auto offset = static_cast<std::uint32_t>(vertices.size());
            vertices.insert(vertices.end(), part.vertices.begin(), part.vertices.end());
            for (auto index : part.indices) indices.push_back(index + offset);
            mesh.parts.push_back(gpuPart);
        }
        if (vertices.empty() || indices.empty()) throw std::runtime_error("Mesh contains no surface geometry");
        mesh.minimum = data.minimum; mesh.maximum = data.maximum;
        // Static meshes live in device-local buffers; staging memory is released
        // only after the transfer queue work has completed.
        Buffer vertexStaging, indexStaging;
        const VkDeviceSize vertexBytes = vertices.size() * sizeof(detail::MeshVertex), indexBytes = indices.size() * sizeof(std::uint32_t);
        vertexStaging.create(context, vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
        indexStaging.create(context, indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
        vertexStaging.upload(vertices.data(), vertexBytes); indexStaging.upload(indices.data(), indexBytes);
        mesh.vertices.create(context, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        mesh.indices.create(context, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        const auto upload = context.beginSingleTimeCommands();
        VkBufferCopy vertexCopy{0, 0, vertexBytes}, indexCopy{0, 0, indexBytes};
        vkCmdCopyBuffer(upload, vertexStaging.buffer, mesh.vertices.buffer, 1, &vertexCopy);
        vkCmdCopyBuffer(upload, indexStaging.buffer, mesh.indices.buffer, 1, &indexCopy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(upload, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        context.endSingleTimeCommands(upload);
    }

    GpuMesh* meshFor(const std::string& source, const fs::path& root) {
        const auto key = assetKey(source, root);
        if (auto it = meshes.find(key); it != meshes.end()) return it->second.get();
        auto mesh = std::make_unique<GpuMesh>();
        try {
            const bool builtin = source.starts_with("builtin:");
            if (!builtin && !isSafeRelativePath(source)) throw std::runtime_error("Mesh path must be project-relative");
            auto data = builtin ? detail::builtinMesh(source) : detail::loadMesh(root / source, root);
            uploadMesh(*mesh, data, key);
            if (!data.animations.empty()) {
                // The GPU cache owns the decoded textures. Animation needs only
                // geometry, joints and clip data after that upload is complete.
                for (auto& part : data.parts) part.albedo.reset();
                mesh->animation = std::make_shared<detail::MeshAsset>(std::move(data));
            }
        } catch (const std::exception& error) {
            assetError = source + ": " + error.what();
            Logger::warn(assetError);
            mesh = std::make_unique<GpuMesh>(); mesh->failed = true;
            uploadMesh(*mesh, detail::builtinMesh("builtin:cube"), key + "#error");
        }
        auto* pointer = mesh.get(); meshes.emplace(key, std::move(mesh)); return pointer;
    }

    GpuPose* poseFor(GpuMesh& mesh, const AnimationComponent& animation, const std::string& key) {
        if (!mesh.animation) return nullptr;
        auto& entry = poses[key];
        if (!entry) entry = std::make_unique<GpuPose>();
        auto& pose = *entry;
        if (pose.source != &mesh || pose.clipIndex != animation.clipIndex || pose.time != animation.time || pose.loop != animation.loop) {
            try {
                const auto evaluated = detail::evaluateAnimation(*mesh.animation, animation.clipIndex, animation.time, animation.loop);
                const VkDeviceSize bytes = evaluated.vertices.size() * sizeof(detail::MeshVertex);
                if (!bytes) throw std::runtime_error("Animated asset contains no vertices");
                if (!pose.vertices.buffer || pose.byteSize != bytes) {
                    pose.vertices.create(context, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, true);
                    pose.byteSize = bytes;
                }
                pose.vertices.upload(evaluated.vertices.data(), bytes);
                pose.minimum = evaluated.minimum; pose.maximum = evaluated.maximum;
                pose.source = &mesh; pose.clipIndex = animation.clipIndex; pose.time = animation.time; pose.loop = animation.loop;
            } catch (const std::exception& error) {
                pose.source = nullptr;
                assetError = "Animation: " + std::string(error.what()); return nullptr;
            }
        }
        return &pose;
    }

    FrameData frameData(const Scene& scene, const RenderCamera& camera) {
        FrameData frame{};
        frame.viewProjection = camera.projection * camera.view;
        frame.cameraExposure = glm::vec4(camera.position, scene.environment.exposure);
        frame.ambient = glm::vec4(scene.environment.ambientColor, scene.environment.ambientIntensity);
        bool directional = false;
        for (const auto& entity : scene.entities()) {
            if (!entity.light || !scene.isEnabled(entity.id)) continue;
            const auto& light = *entity.light;
            const auto world = scene.worldMatrix(entity.id);
            if (light.type == LightType::Directional && !directional) {
                glm::vec3 direction = glm::mat3(world) * glm::vec3(0, 0, -1);
                if (glm::length(direction) < 1e-6f) direction = glm::vec3(-.45f, -1, -.3f);
                frame.sunDirection = glm::vec4(glm::normalize(direction), light.intensity);
                frame.sunColor = glm::vec4(light.color, 1.0f);
                directional = true;
            } else if (light.type == LightType::Point && frame.counts.x < 8) {
                frame.pointPosition[frame.counts.x] = glm::vec4(glm::vec3(world[3]), light.range);
                frame.pointColor[frame.counts.x] = glm::vec4(light.color, light.intensity);
                ++frame.counts.x;
            }
        }
        const auto direction = glm::normalize(glm::vec3(frame.sunDirection));
        const auto inverseView = glm::inverse(camera.view);
        const glm::vec3 center = camera.position - glm::vec3(inverseView[2]) * 12.0f;
        const glm::vec3 up = std::abs(glm::dot(direction, glm::vec3(0, 1, 0))) > .98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        auto projection = glm::orthoRH_ZO(-35.0f, 35.0f, -35.0f, 35.0f, .1f, 160.0f);
        projection[1][1] *= -1.0f;
        frame.lightViewProjection = projection * glm::lookAtRH(center - direction * 70.0f, center, up);
        frame.counts.y = directional && frame.sunDirection.w > 0.0f ? 1 : 0;
        return frame;
    }

    void drawMesh(const Draw& draw, bool shadows) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &draw.vertices, &offset);
        vkCmdBindIndexBuffer(command, draw.mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& part : draw.mesh->parts) {
            auto object = draw.object; object.color *= part.color;
            vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectData), &object);
            if (!shadows) {
                const auto* texture = draw.textureOverride ? draw.textureOverride : part.albedo;
                if (!texture) texture = white;
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &texture->descriptor, 0, nullptr);
            }
            vkCmdDrawIndexed(command, part.indexCount, 1, part.firstIndex, 0, 0);
            ++statistics.drawCalls; statistics.triangles += part.indexCount / 3;
        }
    }

    void setViewport(VkExtent2D extent) {
        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(command, 0, 1, &viewport); vkCmdSetScissor(command, 0, 1, &scissor);
    }

    void clearCache() {
        poses.clear();
        meshes.clear();
        for (auto& [key, texture] : textures) {
            if (texture->descriptor && descriptorPool) vkFreeDescriptorSets(context.device(), descriptorPool, 1, &texture->descriptor);
        }
        textures.clear(); failedTextures.clear(); white = nullptr; assetError.clear();
    }

    void makeWhite() {
        detail::TexturePixels pixels; pixels.width = pixels.height = 1; pixels.rgba = {255, 255, 255, 255};
        white = uploadTexture("builtin:white", pixels);
    }
};

SceneRenderer::SceneRenderer() : m_impl(std::make_unique<Impl>()) {}
SceneRenderer::~SceneRenderer() { shutdown(); }

void SceneRenderer::initialize(GLFWwindow* window, const fs::path& shaderDirectory) {
    auto& r = *m_impl;
    if (r.initialized || r.context.instance()) throw std::runtime_error("Renderer is already initialized");
    if (!window || !ImGui::GetCurrentContext()) throw std::runtime_error("Renderer needs a GLFW window and an ImGui context");
    r.window = window; r.shaderDirectory = shaderDirectory;
    try {
        r.context.init(window); r.swapchain.init(&r.context, window);
        glfwGetFramebufferSize(window, &r.framebufferWidth, &r.framebufferHeight);
        r.depthFormat = findDepthFormat(r.context, false); r.shadowFormat = findDepthFormat(r.context, true);
        r.shadow.create(r.context, ShadowSize, ShadowSize, r.shadowFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, true, true);
        r.createDescriptors();
        r.scenePass = r.createRenderPass(false); r.shadowPass = r.createRenderPass(true);
        VkFramebufferCreateInfo shadowInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        shadowInfo.renderPass = r.shadowPass; shadowInfo.attachmentCount = 1; shadowInfo.pAttachments = &r.shadow.view;
        shadowInfo.width = shadowInfo.height = ShadowSize; shadowInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(r.context.device(), &shadowInfo, nullptr, &r.shadowFramebuffer), "Cannot create shadow framebuffer");
        r.createViewport();
        r.scenePipeline = r.createPipeline(r.scenePass, "engine_scene.vert", "engine_scene.frag", false, false);
        r.shadowPipeline = r.createPipeline(r.shadowPass, "engine_shadow.vert", "", true, false);
        r.presentPipeline = r.createPipeline(r.swapchain.renderPass(), "engine_present.vert", "engine_present.frag", false, true);
        r.guiPipeline = r.createPipeline(r.swapchain.renderPass(), "engine_gui.vert", "engine_gui.frag", false, false, true);
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandInfo.commandPool = r.context.commandPool(); commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(r.context.device(), &commandInfo, &r.command), "Cannot allocate render command buffer");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(r.context.device(), &fenceInfo, nullptr, &r.frameFence), "Cannot create frame fence");
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(r.context.device(), &semaphoreInfo, nullptr, &r.imageAvailable), "Cannot create acquire semaphore");
        r.createPresentationSemaphores();
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("Cannot initialize Dear ImGui GLFW backend");
        r.glfwBackend = true; r.initializeGuiVulkan();
        r.makeWhite(); r.initialized = true;
        Logger::info("Scene renderer ready: material lighting, directional shadows, 8 point lights, frustum culling");
    } catch (...) { shutdown(); throw; }
}

void SceneRenderer::shutdown() noexcept {
    if (!m_impl) return;
    auto& r = *m_impl;
    if (!r.context.instance()) return;
    if (r.context.device()) vkDeviceWaitIdle(r.context.device());
    if (r.context.device()) {
        r.clearCache(); r.destroyViewport();
        if (r.vulkanBackend) { ImGui_ImplVulkan_Shutdown(); r.vulkanBackend = false; }
        if (r.glfwBackend) { ImGui_ImplGlfw_Shutdown(); r.glfwBackend = false; }
        for (auto pipeline : {r.scenePipeline, r.shadowPipeline, r.presentPipeline, r.guiPipeline}) if (pipeline) vkDestroyPipeline(r.context.device(), pipeline, nullptr);
        r.scenePipeline = r.shadowPipeline = r.presentPipeline = r.guiPipeline = VK_NULL_HANDLE;
        if (r.pipelineLayout) vkDestroyPipelineLayout(r.context.device(), r.pipelineLayout, nullptr);
        if (r.guiPipelineLayout) vkDestroyPipelineLayout(r.context.device(), r.guiPipelineLayout, nullptr);
        r.pipelineLayout = r.guiPipelineLayout = VK_NULL_HANDLE;
        if (r.shadowFramebuffer) vkDestroyFramebuffer(r.context.device(), r.shadowFramebuffer, nullptr);
        r.shadowFramebuffer = VK_NULL_HANDLE;
        if (r.scenePass) vkDestroyRenderPass(r.context.device(), r.scenePass, nullptr);
        if (r.shadowPass) vkDestroyRenderPass(r.context.device(), r.shadowPass, nullptr);
        r.scenePass = r.shadowPass = VK_NULL_HANDLE;
        if (r.descriptorPool) vkDestroyDescriptorPool(r.context.device(), r.descriptorPool, nullptr);
        r.descriptorPool = VK_NULL_HANDLE; r.frameSet = VK_NULL_HANDLE;
        if (r.frameLayout) vkDestroyDescriptorSetLayout(r.context.device(), r.frameLayout, nullptr);
        if (r.textureLayout) vkDestroyDescriptorSetLayout(r.context.device(), r.textureLayout, nullptr);
        r.frameLayout = r.textureLayout = VK_NULL_HANDLE;
        r.frameBuffer.reset(); r.shadow.reset();
        for (auto semaphore : r.renderFinished) if (semaphore) vkDestroySemaphore(r.context.device(), semaphore, nullptr);
        r.renderFinished.clear();
        if (r.imageAvailable) vkDestroySemaphore(r.context.device(), r.imageAvailable, nullptr);
        r.imageAvailable = VK_NULL_HANDLE;
        if (r.frameFence) vkDestroyFence(r.context.device(), r.frameFence, nullptr);
        r.frameFence = VK_NULL_HANDLE;
        r.swapchain.destroy();
    }
    r.context.destroy(); r.initialized = r.frameAcquired = r.hasRendered = false;
}

bool SceneRenderer::beginFrame() {
    auto& r = *m_impl;
    if (!r.initialized) throw std::runtime_error("Renderer is not initialized");
    if (r.frameAcquired) throw std::runtime_error("Previous frame was not submitted");
    int width = 0, height = 0;
    glfwGetFramebufferSize(r.window, &width, &height);
    if (width <= 0 || height <= 0) return false;
    VK_CHECK(vkWaitForFences(r.context.device(), 1, &r.frameFence, VK_TRUE, UINT64_MAX), "Cannot wait for renderer frame");
    if (r.pendingSwapchain || r.swapchain.needsRecreation() || width != r.framebufferWidth || height != r.framebufferHeight) r.rebuildSwapchain();
    if (r.width != r.requestedWidth || r.height != r.requestedHeight) {
        r.destroyViewport(); r.width = r.requestedWidth; r.height = r.requestedHeight; r.createViewport();
    }
    if (!r.swapchain.acquireNextImage(r.imageIndex, r.imageAvailable)) { r.pendingSwapchain = true; return false; }
    r.frameAcquired = true;
    return true;
}

void SceneRenderer::beginGuiFrame() {
    if (!m_impl->frameAcquired) throw std::runtime_error("beginFrame must precede beginGuiFrame");
    ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame();
}

void SceneRenderer::render(const Scene& scene, const RenderCamera& camera, const fs::path& projectRoot,
                           EntityId selected, ImDrawData* drawData) {
    auto& r = *m_impl;
    if (!r.frameAcquired) throw std::runtime_error("beginFrame must precede render");
    r.statistics = {};
    const auto frame = r.frameData(scene, camera);
    std::vector<Draw> draws;
    std::unordered_set<std::string> activePoses;
    for (const auto& entity : scene.entities()) {
        if (!entity.mesh || !entity.mesh->visible || !scene.isEnabled(entity.id)) continue;
        Draw draw;
        draw.object.model = scene.worldMatrix(entity.id);
        if (std::abs(glm::determinant(glm::mat3(draw.object.model))) < 1e-10f) continue;
        draw.mesh = r.meshFor(entity.mesh->source, projectRoot);
        draw.vertices = draw.mesh->vertices.buffer;
        auto minimum = draw.mesh->minimum, maximum = draw.mesh->maximum;
        if (entity.animation && draw.mesh->animation) {
            const auto key = assetKey(entity.mesh->source, projectRoot) + "#entity" + std::to_string(entity.id);
            activePoses.insert(key);
            if (const auto* pose = r.poseFor(*draw.mesh, *entity.animation, key)) {
                draw.vertices = pose->vertices.buffer; minimum = pose->minimum; maximum = pose->maximum;
                ++r.statistics.animatedInstances;
            }
        }
        draw.visible = visibleBounds(frame.viewProjection, draw.object.model, minimum, maximum);
        if (draw.visible) ++r.statistics.visibleObjects; else ++r.statistics.culledObjects;
        draw.castsShadow = entity.mesh->castsShadow;
        draw.textureOverride = r.textureFile(entity.mesh->material.albedoTexture, projectRoot);
        draw.object.color = draw.mesh->failed ? glm::vec4(1, 0, 0.7f, 1) : entity.mesh->material.baseColor;
        draw.object.material = glm::vec4(entity.mesh->material.roughness, entity.mesh->material.metallic, entity.id == selected ? 1.0f : 0.0f, 0.0f);
        draws.push_back(draw);
    }
    // A frame fence was waited before entering this function, so retired entity
    // pose buffers are no longer referenced by the GPU and can be released now.
    for (auto it = r.poses.begin(); it != r.poses.end();) {
        if (!activePoses.contains(it->first)) it = r.poses.erase(it); else ++it;
    }
    r.statistics.loadedMeshes = static_cast<std::uint32_t>(r.meshes.size());
    r.statistics.loadedTextures = static_cast<std::uint32_t>(r.textures.size());
    r.frameBuffer.upload(&frame, sizeof(frame));
    VK_CHECK(vkResetCommandBuffer(r.command, 0), "Cannot reset render command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(r.command, &begin), "Cannot begin render command buffer");
    VkClearValue shadowClear{}; shadowClear.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = r.shadowPass; pass.framebuffer = r.shadowFramebuffer; pass.renderArea = {{0, 0}, {ShadowSize, ShadowSize}};
    pass.clearValueCount = 1; pass.pClearValues = &shadowClear;
    vkCmdBeginRenderPass(r.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    r.setViewport({ShadowSize, ShadowSize});
    vkCmdBindPipeline(r.command, VK_PIPELINE_BIND_POINT_GRAPHICS, r.shadowPipeline);
    vkCmdBindDescriptorSets(r.command, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipelineLayout, 0, 1, &r.frameSet, 0, nullptr);
    if (frame.counts.y) for (const auto& draw : draws) if (draw.castsShadow) r.drawMesh(draw, true);
    vkCmdEndRenderPass(r.command);

    std::array<VkClearValue, 2> sceneClears{};
    sceneClears[0].color = {{scene.environment.skyColor.r, scene.environment.skyColor.g, scene.environment.skyColor.b, 1.0f}};
    sceneClears[1].depthStencil = {1.0f, 0};
    pass.renderPass = r.scenePass; pass.framebuffer = r.sceneFramebuffer; pass.renderArea = {{0, 0}, {r.width, r.height}};
    pass.clearValueCount = 2; pass.pClearValues = sceneClears.data();
    vkCmdBeginRenderPass(r.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    r.setViewport({r.width, r.height});
    vkCmdBindPipeline(r.command, VK_PIPELINE_BIND_POINT_GRAPHICS, r.scenePipeline);
    vkCmdBindDescriptorSets(r.command, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipelineLayout, 0, 1, &r.frameSet, 0, nullptr);
    for (const auto& draw : draws) if (draw.visible) r.drawMesh(draw, false);
    vkCmdEndRenderPass(r.command);

    std::array<VkClearValue, 2> windowClears{};
    windowClears[0].color = {{0.018f, 0.021f, 0.028f, 1.0f}}; windowClears[1].depthStencil = {1.0f, 0};
    pass.renderPass = r.swapchain.renderPass(); pass.framebuffer = r.swapchain.framebuffer(r.imageIndex); pass.renderArea = {{0, 0}, r.swapchain.extent()};
    pass.clearValueCount = 2; pass.pClearValues = windowClears.data();
    vkCmdBeginRenderPass(r.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    r.setViewport(r.swapchain.extent());
    if (drawData) ImGui_ImplVulkan_RenderDrawData(drawData, r.command, r.guiPipeline);
    else {
        vkCmdBindPipeline(r.command, VK_PIPELINE_BIND_POINT_GRAPHICS, r.presentPipeline);
        vkCmdBindDescriptorSets(r.command, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipelineLayout, 1, 1, &r.sceneTextureSet, 0, nullptr);
        vkCmdDraw(r.command, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(r.command);
    VK_CHECK(vkEndCommandBuffer(r.command), "Cannot finish render command buffer");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &r.imageAvailable; submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &r.command;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &r.renderFinished[r.imageIndex];
    VK_CHECK(vkResetFences(r.context.device(), 1, &r.frameFence), "Cannot reset frame fence");
    VK_CHECK(vkQueueSubmit(r.context.graphicsQueue(), 1, &submit, r.frameFence), "Cannot submit renderer frame");
    r.pendingSwapchain = !r.swapchain.present(r.imageIndex, r.renderFinished[r.imageIndex]);
    r.frameAcquired = false; r.hasRendered = true;
}

void SceneRenderer::setViewportSize(std::uint32_t width, std::uint32_t height) {
    m_impl->requestedWidth = std::clamp(width, 1u, 4096u);
    m_impl->requestedHeight = std::clamp(height, 1u, 4096u);
}
ImTextureID SceneRenderer::viewportTexture() const { return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_impl->viewportSet)); }
std::uint32_t SceneRenderer::viewportWidth() const { return m_impl->width; }
std::uint32_t SceneRenderer::viewportHeight() const { return m_impl->height; }
const RendererStats& SceneRenderer::stats() const { return m_impl->statistics; }
const std::string& SceneRenderer::lastAssetError() const { return m_impl->assetError; }
bool SceneRenderer::meshBounds(const std::string& source, const fs::path& root, glm::vec3& minimum, glm::vec3& maximum, EntityId instance) const {
    try {
        const auto key = assetKey(source, root);
        const auto it = m_impl->meshes.find(key);
        if (it == m_impl->meshes.end() || it->second->failed) return false;
        if (instance != InvalidEntity) {
            const auto pose = m_impl->poses.find(key + "#entity" + std::to_string(instance));
            if (pose != m_impl->poses.end() && pose->second->source == it->second.get()) {
                minimum = pose->second->minimum; maximum = pose->second->maximum; return true;
            }
        }
        minimum = it->second->minimum; maximum = it->second->maximum; return true;
    } catch (...) { return false; }
}
std::vector<AnimationClipInfo> SceneRenderer::animationClips(const std::string& source, const fs::path& root) const {
    std::vector<AnimationClipInfo> result;
    try {
        const auto it = m_impl->meshes.find(assetKey(source, root));
        if (it == m_impl->meshes.end() || !it->second->animation) return result;
        for (const auto& clip : it->second->animation->animations) result.push_back({clip.name, clip.duration});
    } catch (...) {}
    return result;
}
void SceneRenderer::waitIdle() {
    if (m_impl->context.device()) VK_CHECK(vkDeviceWaitIdle(m_impl->context.device()), "Cannot wait for GPU");
}
void SceneRenderer::clearAssetCache() {
    if (!m_impl->initialized) return;
    waitIdle(); m_impl->clearCache(); m_impl->makeWhite();
}

void SceneRenderer::captureScreenshot(const fs::path& path) {
    auto& r = *m_impl;
    if (!r.initialized || !r.hasRendered) throw std::runtime_error("Render a frame before capturing the viewport");
    waitIdle();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(r.width) * r.height * 4;
    Buffer staging;
    staging.create(r.context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, true);
    const auto copy = r.context.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = r.color.image; barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(copy, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.imageExtent = {r.width, r.height, 1};
    vkCmdCopyImageToBuffer(copy, r.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(copy, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(copy, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hostBarrier, 0, nullptr, 0, nullptr);
    r.context.endSingleTimeCommands(copy);
    VK_CHECK(vmaInvalidateAllocation(r.context.allocator(), staging.allocation, 0, bytes), "Cannot invalidate screenshot staging memory");
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create screenshot: " + path.string());
    output << "P6\n" << r.width << ' ' << r.height << "\n255\n";
    const auto* pixels = static_cast<const unsigned char*>(staging.mapped);
    for (std::size_t i = 0; i < static_cast<std::size_t>(r.width) * r.height; ++i) output.write(reinterpret_cast<const char*>(pixels + i * 4), 3);
    if (!output) throw std::runtime_error("Cannot write complete screenshot: " + path.string());
}

} // namespace myth::engine
