#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>

namespace myth {

// A single joint/bone in the skeleton
struct Joint {
    std::string name;
    int32_t parent = -1;  // -1 means root
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);
    glm::mat4 localTransform = glm::mat4(1.0f);
    std::vector<int32_t> children;
};

// Complete skeleton
struct Skeleton {
    std::vector<Joint> joints;
    std::unordered_map<std::string, int32_t> jointNameToIndex;
    std::unordered_map<int, int32_t> nodeIndexToJoint; // glTF node index -> joint index

    int32_t findJoint(const std::string& name) const {
        auto it = jointNameToIndex.find(name);
        return it != jointNameToIndex.end() ? it->second : -1;
    }
};

// Keyframe for a single property
template<typename T>
struct Keyframe {
    float time;
    T value;
};

// Animation channel - animates one property of one joint
struct AnimationChannel {
    int32_t jointIndex = -1;
    enum class Path { Translation, Rotation, Scale } path;

    std::vector<Keyframe<glm::vec3>> translationKeys;
    std::vector<Keyframe<glm::quat>> rotationKeys;
    std::vector<Keyframe<glm::vec3>> scaleKeys;
};

// Complete animation clip
struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

// Animation playback state
struct AnimationState {
    int32_t clipIndex = -1;
    float currentTime = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = true;
};

// Skinned vertex - extends base vertex with joint data
struct SkinnedVertex {
    // Explicit layout with padding for 16-byte alignment of ivec4/vec4
    glm::vec3 position;       // 12 bytes, offset 0
    float _pad0;              // 4 bytes,  offset 12 (align color to 16)
    glm::vec3 color;          // 12 bytes, offset 16
    float _pad1;              // 4 bytes,  offset 28 (align texCoord to 32)
    glm::vec2 texCoord;       // 8 bytes,  offset 32
    glm::vec2 _pad2;          // 8 bytes,  offset 40 (align normal to 48)
    glm::vec3 normal;         // 12 bytes, offset 48
    float _pad3;              // 4 bytes,  offset 60 (align jointIndices to 64)
    glm::ivec4 jointIndices;  // 16 bytes, offset 64
    glm::vec4 jointWeights;   // 16 bytes, offset 80
    // Total: 96 bytes, all vec4/ivec4 at 16-byte aligned offsets

    static_assert(sizeof(glm::vec3) == 12, "vec3 must be 12 bytes");
    static_assert(sizeof(glm::ivec4) == 16, "ivec4 must be 16 bytes");
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = 96; // Explicit size
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 6> attrs{};
        // Hardcoded offsets matching our explicit layout
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};   // position
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 16};  // color
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, 32};     // texCoord
        attrs[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 48};  // normal
        attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SINT, 64}; // jointIndices
        attrs[5] = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 80}; // jointWeights
        return attrs;
    }
};

// Maximum joints supported (must match shader)
constexpr int MAX_JOINTS = 128;

// Animation helper functions
namespace AnimationUtils {
    // Linear interpolation for vec3
    inline glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
        return a + t * (b - a);
    }

    // Spherical interpolation for quaternions
    inline glm::quat slerp(const glm::quat& a, const glm::quat& b, float t) {
        return glm::slerp(a, b, t);
    }

    // Find keyframe index for given time
    template<typename T>
    inline size_t findKeyframe(const std::vector<Keyframe<T>>& keys, float time) {
        for (size_t i = 0; i < keys.size() - 1; i++) {
            if (time < keys[i + 1].time) return i;
        }
        return keys.size() > 0 ? keys.size() - 1 : 0;
    }

    // Sample translation at time
    inline glm::vec3 sampleTranslation(const AnimationChannel& channel, float time) {
        if (channel.translationKeys.empty()) return glm::vec3(0.0f);
        if (channel.translationKeys.size() == 1) return channel.translationKeys[0].value;

        size_t i = findKeyframe(channel.translationKeys, time);
        if (i >= channel.translationKeys.size() - 1) return channel.translationKeys.back().value;

        const auto& k0 = channel.translationKeys[i];
        const auto& k1 = channel.translationKeys[i + 1];
        float t = (time - k0.time) / (k1.time - k0.time);
        return lerp(k0.value, k1.value, t);
    }

    // Sample rotation at time
    inline glm::quat sampleRotation(const AnimationChannel& channel, float time) {
        if (channel.rotationKeys.empty()) return glm::quat(1, 0, 0, 0);
        if (channel.rotationKeys.size() == 1) return channel.rotationKeys[0].value;

        size_t i = findKeyframe(channel.rotationKeys, time);
        if (i >= channel.rotationKeys.size() - 1) return channel.rotationKeys.back().value;

        const auto& k0 = channel.rotationKeys[i];
        const auto& k1 = channel.rotationKeys[i + 1];
        float t = (time - k0.time) / (k1.time - k0.time);
        return slerp(k0.value, k1.value, t);
    }

    // Sample scale at time
    inline glm::vec3 sampleScale(const AnimationChannel& channel, float time) {
        if (channel.scaleKeys.empty()) return glm::vec3(1.0f);
        if (channel.scaleKeys.size() == 1) return channel.scaleKeys[0].value;

        size_t i = findKeyframe(channel.scaleKeys, time);
        if (i >= channel.scaleKeys.size() - 1) return channel.scaleKeys.back().value;

        const auto& k0 = channel.scaleKeys[i];
        const auto& k1 = channel.scaleKeys[i + 1];
        float t = (time - k0.time) / (k1.time - k0.time);
        return lerp(k0.value, k1.value, t);
    }
}

} // namespace myth







