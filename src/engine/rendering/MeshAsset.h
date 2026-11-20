#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace myth::engine::detail {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
};

struct TexturePixels {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

struct MeshPart {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    glm::vec4 color{1.0f};
    std::shared_ptr<TexturePixels> albedo;
    int nodeIndex = -1;
    std::vector<MeshVertex> localVertices;
    std::vector<glm::ivec4> jointIndices;
    std::vector<glm::vec4> jointWeights;
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBindMatrices;
};

struct AnimationNode {
    int parent = -1;
    glm::vec3 translation{0.0f}, scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::mat4 matrix{1.0f};
    bool usesMatrix = false;
};

enum class AnimationPath { Translation, Rotation, Scale };
enum class Interpolation { Linear, Step, CubicSpline };
struct AnimationChannel {
    int node = -1;
    AnimationPath path = AnimationPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;
    // Quaternion components are stored as x,y,z,w. Cubic spline channels contain
    // [in tangent, value, out tangent] triples per key, exactly as glTF defines.
    std::vector<glm::vec4> values;
};
struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct MeshAsset {
    std::vector<MeshPart> parts;
    glm::vec3 minimum{0.0f}, maximum{0.0f};
    std::vector<AnimationNode> nodes;
    std::vector<AnimationClip> animations;
};

struct AnimatedVertices {
    std::vector<MeshVertex> vertices; // Concatenated in MeshAsset::parts order.
    glm::vec3 minimum{0.0f}, maximum{0.0f};
};

MeshAsset builtinMesh(const std::string& source);
MeshAsset loadMesh(const std::filesystem::path& path, const std::filesystem::path& projectRoot);
TexturePixels loadTexture(const std::filesystem::path& path);
bool insideRoot(const std::filesystem::path& path, const std::filesystem::path& root);
AnimatedVertices evaluateAnimation(const MeshAsset& asset, int clipIndex, float time, bool loop);

} // namespace myth::engine::detail
