#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <optional>

#include <stdexcept>

#define VK_CHECK(x, msg) do { if ((x) != VK_SUCCESS) throw std::runtime_error(msg); } while(0)

namespace myth {
namespace vk {

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }
    
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0].binding = 0; attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = offsetof(Vertex, position);
        attrs[1].binding = 0; attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(Vertex, color);
        attrs[2].binding = 0; attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = offsetof(Vertex, texCoord);
        attrs[3].binding = 0; attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[3].offset = offsetof(Vertex, normal);
        return attrs;
    }
};

struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::vec3 cameraPos;
    float time;
    glm::vec3 sunDirection;
    float sunIntensity;
    glm::vec3 sunColor;
    float ambientIntensity;
    glm::vec3 skyColorTop;
    float padding1;
    glm::vec3 skyColorBottom;
    float padding2;
};

struct PushConstants {
    glm::mat4 model;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

} // namespace vk
} // namespace myth

