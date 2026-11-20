#include "MeshAsset.h"

#include <nlohmann/json.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace myth::engine::detail {
namespace fs = std::filesystem;
namespace {
constexpr std::size_t MaximumAssetBytes = 256 * 1024 * 1024;

void computeBounds(MeshAsset& asset) {
    asset.minimum = glm::vec3(std::numeric_limits<float>::max());
    asset.maximum = glm::vec3(std::numeric_limits<float>::lowest());
    std::size_t count = 0;
    for (const auto& part : asset.parts) for (const auto& v : part.vertices) {
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z))
            throw std::runtime_error("Mesh contains non-finite vertex coordinates");
        if (!std::isfinite(v.normal.x) || !std::isfinite(v.normal.y) || !std::isfinite(v.normal.z) ||
            !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) || !std::isfinite(v.color.r) ||
            !std::isfinite(v.color.g) || !std::isfinite(v.color.b) || !std::isfinite(v.color.a))
            throw std::runtime_error("Mesh contains non-finite vertex attributes");
        asset.minimum = glm::min(asset.minimum, v.position);
        asset.maximum = glm::max(asset.maximum, v.position);
        ++count;
    }
    if (!count) throw std::runtime_error("Asset contains no triangle geometry");
}

void generateNormals(MeshPart& part) {
    for (auto& v : part.vertices) v.normal = glm::vec3(0.0f);
    for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
        auto& a = part.vertices.at(part.indices[i]);
        auto& b = part.vertices.at(part.indices[i + 1]);
        auto& c = part.vertices.at(part.indices[i + 2]);
        const glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += normal; b.normal += normal; c.normal += normal;
    }
    for (auto& v : part.vertices) {
        const float length = glm::length(v.normal);
        v.normal = length > 1e-8f ? v.normal / length : glm::vec3(0, 1, 0);
    }
}

std::vector<unsigned char> readBytes(const fs::path& path) {
    const auto size = fs::file_size(path);
    if (size > MaximumAssetBytes) throw std::runtime_error("Asset exceeds the 256 MiB import limit: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open asset: " + path.string());
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read complete asset: " + path.string());
    return bytes;
}

template <class T> T unalignedRead(const unsigned char* data) {
    T value{}; std::memcpy(&value, data, sizeof(T)); return value;
}

double componentValue(const unsigned char* bytes, int componentType, bool normalized) {
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return unalignedRead<float>(bytes);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const auto v = unalignedRead<std::uint8_t>(bytes); return normalized ? v / 255.0 : v;
    }
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        const auto v = unalignedRead<std::int8_t>(bytes); return normalized ? std::max(-1.0, v / 127.0) : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const auto v = unalignedRead<std::uint16_t>(bytes); return normalized ? v / 65535.0 : v;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        const auto v = unalignedRead<std::int16_t>(bytes); return normalized ? std::max(-1.0, v / 32767.0) : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return unalignedRead<std::uint32_t>(bytes);
    default: throw std::runtime_error("Unsupported glTF accessor component type");
    }
}

std::vector<double> accessorValues(const tinygltf::Model& model, int accessorIndex, int expectedComponents = 0) {
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
        throw std::runtime_error("Invalid glTF accessor index");
    const auto& a = model.accessors[accessorIndex];
    const int components = tinygltf::GetNumComponentsInType(a.type);
    const int componentSize = tinygltf::GetComponentSizeInBytes(a.componentType);
    if (components <= 0 || componentSize <= 0 || (expectedComponents && components != expectedComponents))
        throw std::runtime_error("Unexpected glTF accessor layout");
    if (a.count > 16 * 1024 * 1024 || a.count * components > MaximumAssetBytes / sizeof(double))
        throw std::runtime_error("glTF accessor is too large");
    std::vector<double> result(a.count * components, 0.0);
    auto readView = [&](int viewIndex, std::size_t offset, std::size_t count, std::size_t stride, auto consume) {
        if (viewIndex < 0 || viewIndex >= static_cast<int>(model.bufferViews.size()))
            throw std::runtime_error("Invalid glTF buffer view");
        const auto& view = model.bufferViews[viewIndex];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            throw std::runtime_error("Invalid glTF buffer index");
        const auto& buffer = model.buffers[view.buffer].data;
        if (view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset || offset > view.byteLength)
            throw std::runtime_error("glTF buffer view exceeds its buffer");
        for (std::size_t i = 0; i < count; ++i) {
            const auto byteOffset = offset + i * stride;
            if (byteOffset > view.byteLength || stride > view.byteLength - byteOffset)
                throw std::runtime_error("glTF accessor exceeds its buffer view");
            consume(i, buffer.data() + view.byteOffset + byteOffset);
        }
    };
    if (a.bufferView >= 0) {
        const auto& view = model.bufferViews.at(a.bufferView);
        const int stride = a.ByteStride(view);
        if (stride < components * componentSize) throw std::runtime_error("Invalid glTF byte stride");
        // The final element need not include interleaved padding after its data.
        const auto& buffer = model.buffers.at(view.buffer).data;
        if (view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset || a.byteOffset > view.byteLength)
            throw std::runtime_error("glTF buffer view exceeds its buffer");
        for (std::size_t i = 0; i < a.count; ++i) {
            const auto offset = a.byteOffset + i * static_cast<std::size_t>(stride);
            if (offset > view.byteLength || components * componentSize > view.byteLength - offset)
                throw std::runtime_error("glTF accessor exceeds its buffer view");
            const auto* bytes = buffer.data() + view.byteOffset + offset;
            for (int c = 0; c < components; ++c)
                result[i * components + c] = componentValue(bytes + c * componentSize, a.componentType, a.normalized);
        }
    }
    if (a.sparse.isSparse) {
        if (a.sparse.count < 0 || static_cast<std::size_t>(a.sparse.count) > a.count)
            throw std::runtime_error("Invalid glTF sparse accessor count");
        const int indexSize = tinygltf::GetComponentSizeInBytes(a.sparse.indices.componentType);
        if (indexSize <= 0) throw std::runtime_error("Invalid sparse index type");
        std::vector<std::size_t> indices(a.sparse.count);
        readView(a.sparse.indices.bufferView, a.sparse.indices.byteOffset, a.sparse.count, indexSize,
            [&](std::size_t i, const unsigned char* p) {
                indices[i] = static_cast<std::size_t>(componentValue(p, a.sparse.indices.componentType, false));
                if (indices[i] >= a.count) throw std::runtime_error("Sparse glTF index out of range");
            });
        readView(a.sparse.values.bufferView, a.sparse.values.byteOffset, a.sparse.count, components * componentSize,
            [&](std::size_t i, const unsigned char* p) {
                for (int c = 0; c < components; ++c)
                    result[indices[i] * components + c] = componentValue(p + c * componentSize, a.componentType, a.normalized);
            });
    }
    for (double value : result) if (!std::isfinite(value))
        throw std::runtime_error("glTF accessor contains a non-finite value");
    return result;
}

glm::mat4 nodeMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 matrix(1.0f);
        for (int i = 0; i < 16; ++i) matrix[i / 4][i % 4] = static_cast<float>(node.matrix[i]);
        return matrix;
    }
    glm::vec3 position(0.0f), scale(1.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.translation.size() == 3) position = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    if (node.scale.size() == 3) scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
    if (node.rotation.size() == 4) rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

void loadAnimationData(const tinygltf::Model& model, MeshAsset& asset) {
    asset.nodes.resize(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& source = model.nodes[i];
        auto& node = asset.nodes[i];
        node.matrix = nodeMatrix(source); node.usesMatrix = source.matrix.size() == 16;
        if (source.translation.size() == 3) node.translation = glm::vec3(source.translation[0], source.translation[1], source.translation[2]);
        if (source.scale.size() == 3) node.scale = glm::vec3(source.scale[0], source.scale[1], source.scale[2]);
        if (source.rotation.size() == 4) {
            const glm::quat rotation(source.rotation[3], source.rotation[0], source.rotation[1], source.rotation[2]);
            if (glm::length(rotation) < 1e-8f) throw std::runtime_error("glTF node has a zero-length rotation quaternion");
            node.rotation = glm::normalize(rotation);
        }
        for (int child : source.children) {
            if (child < 0 || child >= static_cast<int>(asset.nodes.size()) || asset.nodes[child].parent >= 0)
                throw std::runtime_error("glTF hierarchy contains an invalid or multiply-parented node");
            asset.nodes[child].parent = static_cast<int>(i);
        }
    }
    for (const auto& source : model.animations) {
        AnimationClip clip;
        clip.name = source.name.empty() ? "Animation " + std::to_string(asset.animations.size() + 1) : source.name;
        for (const auto& sourceChannel : source.channels) {
            if (sourceChannel.target_path == "weights") continue; // Morph targets are outside the current skeletal/TRS feature set.
            AnimationChannel channel;
            if (sourceChannel.target_path == "translation") channel.path = AnimationPath::Translation;
            else if (sourceChannel.target_path == "rotation") channel.path = AnimationPath::Rotation;
            else if (sourceChannel.target_path == "scale") channel.path = AnimationPath::Scale;
            else throw std::runtime_error("Unknown glTF animation target path");
            channel.node = sourceChannel.target_node;
            if (channel.node < 0 || channel.node >= static_cast<int>(asset.nodes.size()) ||
                sourceChannel.sampler < 0 || sourceChannel.sampler >= static_cast<int>(source.samplers.size()))
                throw std::runtime_error("Invalid glTF animation channel");
            if (asset.nodes[channel.node].usesMatrix)
                throw std::runtime_error("A glTF node targeted by TRS animation must use TRS, not a matrix");
            const auto& sampler = source.samplers[sourceChannel.sampler];
            if (sampler.interpolation.empty() || sampler.interpolation == "LINEAR") channel.interpolation = Interpolation::Linear;
            else if (sampler.interpolation == "STEP") channel.interpolation = Interpolation::Step;
            else if (sampler.interpolation == "CUBICSPLINE") channel.interpolation = Interpolation::CubicSpline;
            else throw std::runtime_error("Unknown glTF animation interpolation");
            const auto times = accessorValues(model, sampler.input, 1);
            if (times.empty()) continue;
            for (double time : times) {
                if (!std::isfinite(time) || time < 0.0 || time > std::numeric_limits<float>::max() ||
                    (!channel.times.empty() && time <= channel.times.back()))
                    throw std::runtime_error("Animation times must be finite, non-negative and strictly increasing");
                channel.times.push_back(static_cast<float>(time));
            }
            const int components = channel.path == AnimationPath::Rotation ? 4 : 3;
            const auto values = accessorValues(model, sampler.output, components);
            const std::size_t multiplier = channel.interpolation == Interpolation::CubicSpline ? 3 : 1;
            if (values.size() != times.size() * multiplier * components)
                throw std::runtime_error("Animation output count does not match its key times");
            for (std::size_t i = 0; i < values.size(); i += components) {
                glm::vec4 value(0.0f);
                for (int c = 0; c < components; ++c) {
                    if (!std::isfinite(values[i + c]) || std::abs(values[i + c]) > std::numeric_limits<float>::max())
                        throw std::runtime_error("Animation contains non-finite key values");
                    value[c] = static_cast<float>(values[i + c]);
                }
                channel.values.push_back(value);
            }
            clip.duration = std::max(clip.duration, channel.times.back());
            clip.channels.push_back(std::move(channel));
        }
        if (!clip.channels.empty()) asset.animations.push_back(std::move(clip));
    }
}

MeshAsset loadGltf(const fs::path& path, const fs::path& projectRoot) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error, warning;
    tinygltf::FsCallbacks callbacks;
    callbacks.FileExists = [projectRoot](const std::string& p, void*) {
        return insideRoot(p, projectRoot) && fs::is_regular_file(p);
    };
    callbacks.ExpandFilePath = [](const std::string& p, void*) { return p; };
    callbacks.ReadWholeFile = [projectRoot](std::vector<unsigned char>* data, std::string* err, const std::string& p, void*) {
        try {
            if (!insideRoot(p, projectRoot)) throw std::runtime_error("glTF dependency escapes the project directory");
            *data = readBytes(p); return true;
        } catch (const std::exception& e) { if (err) *err = e.what(); return false; }
    };
    callbacks.GetFileSizeInBytes = [projectRoot](std::size_t* size, std::string* err, const std::string& p, void*) {
        try {
            if (!insideRoot(p, projectRoot)) throw std::runtime_error("glTF dependency escapes the project directory");
            const auto count = fs::file_size(p);
            if (count > MaximumAssetBytes) throw std::runtime_error("glTF dependency exceeds 256 MiB");
            *size = static_cast<std::size_t>(count); return true;
        } catch (const std::exception& e) { if (err) *err = e.what(); return false; }
    };
    callbacks.WriteWholeFile = [](std::string* err, const std::string&, const std::vector<unsigned char>&, void*) {
        if (err) *err = "Renderer asset loading is read-only"; return false;
    };
    callbacks.user_data = nullptr;
    loader.SetFsCallbacks(callbacks);
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool loaded = extension == ".glb" ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
                                            : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    if (!loaded) throw std::runtime_error("glTF import failed: " + error);
    for (const auto& required : model.extensionsRequired) {
        if (required == "KHR_draco_mesh_compression" || required == "EXT_meshopt_compression")
            throw std::runtime_error("Compressed glTF requires decompression before import: " + required);
    }
    std::vector<std::shared_ptr<TexturePixels>> images(model.images.size());
    for (std::size_t i = 0; i < model.images.size(); ++i) {
        const auto& image = model.images[i];
        if (image.width <= 0 || image.height <= 0 || image.component <= 0 || image.component > 4 || image.bits != 8) continue;
        const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
        if (count > MaximumAssetBytes / 4 || image.image.size() < count * image.component) continue;
        auto pixels = std::make_shared<TexturePixels>();
        pixels->width = image.width; pixels->height = image.height; pixels->rgba.resize(count * 4, 255);
        for (std::size_t j = 0; j < count; ++j) {
            for (int c = 0; c < 3; ++c) pixels->rgba[j * 4 + c] = image.image[j * image.component + (image.component < 3 ? 0 : c)];
            if (image.component == 2 || image.component == 4) pixels->rgba[j * 4 + 3] = image.image[j * image.component + image.component - 1];
        }
        images[i] = std::move(pixels);
    }
    MeshAsset asset;
    loadAnimationData(model, asset);
    auto addMesh = [&](int meshIndex, const glm::mat4& transform, int nodeIndex) {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes.size())) throw std::runtime_error("Invalid glTF mesh index");
        const glm::mat3 normalMatrix = std::abs(glm::determinant(glm::mat3(transform))) > 1e-10f
            ? glm::transpose(glm::inverse(glm::mat3(transform))) : glm::mat3(1.0f);
        for (const auto& primitive : model.meshes[meshIndex].primitives) {
            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end()) continue;
            const auto positions = accessorValues(model, positionIt->second, 3);
            const std::size_t count = positions.size() / 3;
            if (!count) continue;
            MeshPart part;
            part.nodeIndex = nodeIndex;
            part.vertices.resize(count);
            const auto attribute = [&](const char* name, int components) {
                const auto it = primitive.attributes.find(name);
                return it == primitive.attributes.end() ? std::vector<double>{} : accessorValues(model, it->second, components);
            };
            const auto normals = attribute("NORMAL", 3);
            const auto uvs = attribute("TEXCOORD_0", 2);
            const auto colors = attribute("COLOR_0", 0);
            const auto colorIt = primitive.attributes.find("COLOR_0");
            const std::size_t colorComponents = colorIt == primitive.attributes.end() ? 0 :
                tinygltf::GetNumComponentsInType(model.accessors.at(colorIt->second).type);
            if ((!normals.empty() && normals.size() != count * 3) || (!uvs.empty() && uvs.size() != count * 2) ||
                (!colors.empty() && (colors.size() != count * colorComponents || (colorComponents != 3 && colorComponents != 4))))
                throw std::runtime_error("glTF attribute count does not match positions");
            for (std::size_t i = 0; i < count; ++i) {
                auto& vertex = part.vertices[i];
                vertex.position = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
                if (!normals.empty()) {
                    const glm::vec3 normal(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
                    vertex.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                }
                if (!uvs.empty()) vertex.uv = glm::vec2(uvs[i * 2], uvs[i * 2 + 1]);
                if (!colors.empty()) for (std::size_t c = 0; c < colorComponents; ++c) vertex.color[static_cast<int>(c)] = colors[i * colorComponents + c];
            }
            std::vector<std::uint32_t> sourceIndices;
            if (primitive.indices >= 0) {
                const auto indices = accessorValues(model, primitive.indices, 1);
                for (double index : indices) {
                    if (!std::isfinite(index) || index < 0 || index >= static_cast<double>(count) || std::floor(index) != index)
                        throw std::runtime_error("glTF triangle index out of range");
                    sourceIndices.push_back(static_cast<std::uint32_t>(index));
                }
            } else {
                sourceIndices.resize(count); std::iota(sourceIndices.begin(), sourceIndices.end(), 0u);
            }
            const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
            if (mode == TINYGLTF_MODE_TRIANGLES) {
                if (sourceIndices.size() % 3) throw std::runtime_error("Incomplete glTF triangle");
                part.indices = std::move(sourceIndices);
            } else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP || mode == TINYGLTF_MODE_TRIANGLE_FAN) {
                for (std::size_t i = 2; i < sourceIndices.size(); ++i) {
                    if (mode == TINYGLTF_MODE_TRIANGLE_FAN) part.indices.insert(part.indices.end(), {sourceIndices[0], sourceIndices[i - 1], sourceIndices[i]});
                    else if (i % 2 == 0) part.indices.insert(part.indices.end(), {sourceIndices[i - 2], sourceIndices[i - 1], sourceIndices[i]});
                    else part.indices.insert(part.indices.end(), {sourceIndices[i - 1], sourceIndices[i - 2], sourceIndices[i]});
                }
            } else continue; // Lines and points are not surface geometry.
            if (normals.empty()) generateNormals(part);
            part.localVertices = part.vertices;
            for (auto& vertex : part.vertices) {
                vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
                const auto normal = normalMatrix * vertex.normal;
                vertex.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
            }
            if (glm::determinant(glm::mat3(transform)) < 0.0f)
                for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) std::swap(part.indices[i + 1], part.indices[i + 2]);
            if (nodeIndex >= 0 && model.nodes[nodeIndex].skin >= 0) {
                const int skinIndex = model.nodes[nodeIndex].skin;
                if (skinIndex >= static_cast<int>(model.skins.size())) throw std::runtime_error("Invalid glTF skin index");
                const auto& skin = model.skins[skinIndex];
                part.joints = skin.joints;
                for (int joint : part.joints) if (joint < 0 || joint >= static_cast<int>(asset.nodes.size()))
                    throw std::runtime_error("glTF skin references an invalid joint node");
                part.inverseBindMatrices.assign(part.joints.size(), glm::mat4(1.0f));
                if (skin.inverseBindMatrices >= 0) {
                    const auto matrices = accessorValues(model, skin.inverseBindMatrices, 16);
                    if (matrices.size() < part.joints.size() * 16) throw std::runtime_error("Too few glTF inverse bind matrices");
                    for (std::size_t joint = 0; joint < part.joints.size(); ++joint)
                        for (int value = 0; value < 16; ++value) part.inverseBindMatrices[joint][value / 4][value % 4] = matrices[joint * 16 + value];
                }
                const auto jointIndices = attribute("JOINTS_0", 4), weights = attribute("WEIGHTS_0", 4);
                if (jointIndices.size() != count * 4 || weights.size() != count * 4)
                    throw std::runtime_error("Skinned glTF mesh requires four JOINTS_0 and WEIGHTS_0 values per vertex");
                part.jointIndices.resize(count); part.jointWeights.resize(count);
                for (std::size_t vertex = 0; vertex < count; ++vertex) {
                    for (int influence = 0; influence < 4; ++influence) {
                        const double joint = jointIndices[vertex * 4 + influence], weight = weights[vertex * 4 + influence];
                        if (!std::isfinite(joint) || joint < 0 || std::floor(joint) != joint || joint >= static_cast<double>(part.joints.size()) ||
                            !std::isfinite(weight) || weight < 0.0 || weight > 1.0)
                            throw std::runtime_error("Invalid glTF vertex joint or weight");
                        part.jointIndices[vertex][influence] = static_cast<int>(joint);
                        part.jointWeights[vertex][influence] = static_cast<float>(weight);
                    }
                    const auto w = part.jointWeights[vertex];
                    const float sum = w.x + w.y + w.z + w.w;
                    if (sum > 1e-8f) part.jointWeights[vertex] /= sum;
                }
            }
            if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
                const auto& material = model.materials[primitive.material].pbrMetallicRoughness;
                if (material.baseColorFactor.size() == 4)
                    part.color = glm::vec4(material.baseColorFactor[0], material.baseColorFactor[1], material.baseColorFactor[2], material.baseColorFactor[3]);
                const int textureIndex = material.baseColorTexture.index;
                if (textureIndex >= 0 && textureIndex < static_cast<int>(model.textures.size())) {
                    const int imageIndex = model.textures[textureIndex].source;
                    if (imageIndex >= 0 && imageIndex < static_cast<int>(images.size())) part.albedo = images[imageIndex];
                }
            }
            if (!part.indices.empty()) asset.parts.push_back(std::move(part));
        }
    };
    std::set<int> activeNodes;
    std::function<void(int, const glm::mat4&)> visit = [&](int index, const glm::mat4& parent) {
        if (index < 0 || index >= static_cast<int>(model.nodes.size()) || activeNodes.size() > 1024 || !activeNodes.insert(index).second)
            throw std::runtime_error("Invalid or cyclic glTF node hierarchy");
        const auto& node = model.nodes[index];
        const auto world = parent * nodeMatrix(node);
        if (node.mesh >= 0) addMesh(node.mesh, world, index);
        for (int child : node.children) visit(child, world);
        activeNodes.erase(index);
    };
    if (!model.scenes.empty()) {
        const int scene = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (scene >= static_cast<int>(model.scenes.size())) throw std::runtime_error("Invalid glTF default scene");
        for (int node : model.scenes[scene].nodes) visit(node, glm::mat4(1.0f));
    } else if (!model.nodes.empty()) {
        std::set<int> children;
        for (const auto& node : model.nodes) children.insert(node.children.begin(), node.children.end());
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) if (!children.contains(i)) visit(i, glm::mat4(1.0f));
    } else for (int i = 0; i < static_cast<int>(model.meshes.size()); ++i) addMesh(i, glm::mat4(1.0f), -1);
    computeBounds(asset);
    return asset;
}

MeshAsset loadObj(const fs::path& path, const fs::path& projectRoot) {
    // Parsing from strings prevents an OBJ's mtllib line from opening arbitrary
    // filesystem paths. Import supplies geometry, normals, UVs and vertex colors;
    // the editor's material supplies textures and physical material properties.
    const auto bytes = readBytes(path);
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    if (!reader.ParseFromString(std::string(bytes.begin(), bytes.end()), "", config))
        throw std::runtime_error("OBJ import failed: " + reader.Error());
    const auto& attributes = reader.GetAttrib();
    MeshAsset asset;
    for (const auto& shape : reader.GetShapes()) {
        MeshPart part;
        bool normalsComplete = true;
        for (const auto& index : shape.mesh.indices) {
            if (index.vertex_index < 0 || static_cast<std::size_t>(index.vertex_index) * 3 + 2 >= attributes.vertices.size())
                throw std::runtime_error("OBJ vertex index out of range");
            MeshVertex vertex;
            const std::size_t i = index.vertex_index * 3;
            vertex.position = glm::vec3(attributes.vertices[i], attributes.vertices[i + 1], attributes.vertices[i + 2]);
            if (index.normal_index >= 0 && static_cast<std::size_t>(index.normal_index) * 3 + 2 < attributes.normals.size()) {
                const auto n = index.normal_index * 3;
                const glm::vec3 normal(attributes.normals[n], attributes.normals[n + 1], attributes.normals[n + 2]);
                vertex.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
            } else normalsComplete = false;
            if (index.texcoord_index >= 0 && static_cast<std::size_t>(index.texcoord_index) * 2 + 1 < attributes.texcoords.size()) {
                const auto t = index.texcoord_index * 2;
                vertex.uv = glm::vec2(attributes.texcoords[t], 1.0f - attributes.texcoords[t + 1]);
            }
            if (attributes.colors.size() >= i + 3) vertex.color = glm::vec4(attributes.colors[i], attributes.colors[i + 1], attributes.colors[i + 2], 1);
            part.indices.push_back(static_cast<std::uint32_t>(part.vertices.size()));
            part.vertices.push_back(vertex);
        }
        if (!normalsComplete) generateNormals(part);
        if (!part.indices.empty()) asset.parts.push_back(std::move(part));
    }
    computeBounds(asset);
    return asset;
}
} // namespace

bool insideRoot(const fs::path& path, const fs::path& root) {
    try {
        const auto canonicalRoot = fs::weakly_canonical(root);
        const auto canonicalPath = fs::weakly_canonical(path);
        const auto relative = canonicalPath.lexically_relative(canonicalRoot);
        if (relative.empty() || relative.is_absolute()) return false;
        for (const auto& component : relative) if (component == "..") return false;
        return true;
    } catch (...) { return false; }
}

TexturePixels loadTexture(const fs::path& path) {
    const auto bytes = readBytes(path);
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!decoded) throw std::runtime_error("Texture decode failed: " + path.string());
    const auto release = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>(decoded, stbi_image_free);
    if (width <= 0 || height <= 0 || static_cast<std::size_t>(width) * height > MaximumAssetBytes / 4)
        throw std::runtime_error("Texture dimensions exceed the import limit");
    TexturePixels pixels;
    pixels.width = width; pixels.height = height;
    pixels.rgba.assign(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
    return pixels;
}

MeshAsset loadMesh(const fs::path& path, const fs::path& projectRoot) {
    if (!insideRoot(path, projectRoot)) throw std::runtime_error("Mesh path escapes the project directory");
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".gltf" || extension == ".glb") return loadGltf(path, projectRoot);
    if (extension == ".obj") return loadObj(path, projectRoot);
    throw std::runtime_error("Unsupported mesh format: " + extension);
}

AnimatedVertices evaluateAnimation(const MeshAsset& asset, int clipIndex, float time, bool loop) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(asset.animations.size()))
        throw std::runtime_error("Animation clip index is out of range");
    if (!std::isfinite(time)) throw std::runtime_error("Animation time must be finite");
    const auto& clip = asset.animations[clipIndex];
    const float duration = clip.duration;
    if (loop && duration > 0.0f) {
        time = std::fmod(time, duration);
        if (time < 0.0f) time += duration;
    } else time = std::clamp(time, 0.0f, duration);
    auto nodes = asset.nodes;
    auto sample = [time](const AnimationChannel& channel) {
        const bool cubic = channel.interpolation == Interpolation::CubicSpline;
        auto valueAt = [&](std::size_t key) { return channel.values.at(cubic ? key * 3 + 1 : key); };
        if (channel.times.empty()) throw std::runtime_error("Animation channel has no keys");
        if (channel.times.size() == 1 || time <= channel.times.front()) return valueAt(0);
        if (time >= channel.times.back()) return valueAt(channel.times.size() - 1);
        const auto next = static_cast<std::size_t>(std::upper_bound(channel.times.begin(), channel.times.end(), time) - channel.times.begin());
        const auto previous = next - 1;
        const float interval = channel.times[next] - channel.times[previous];
        const float t = (time - channel.times[previous]) / interval;
        const auto a = valueAt(previous), b = valueAt(next);
        if (channel.interpolation == Interpolation::Step) return a;
        if (cubic) {
            const float t2 = t * t, t3 = t2 * t;
            const auto outTangent = channel.values.at(previous * 3 + 2) * interval;
            const auto inTangent = channel.values.at(next * 3) * interval;
            return (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * outTangent +
                   (-2 * t3 + 3 * t2) * b + (t3 - t2) * inTangent;
        }
        if (channel.path == AnimationPath::Rotation) {
            const glm::quat qa(a.w, a.x, a.y, a.z), qb(b.w, b.x, b.y, b.z);
            if (glm::length(qa) < 1e-8f || glm::length(qb) < 1e-8f)
                throw std::runtime_error("Animation contains a zero-length quaternion");
            const auto q = glm::slerp(glm::normalize(qa), glm::normalize(qb), t);
            return glm::vec4(q.x, q.y, q.z, q.w);
        }
        return glm::mix(a, b, t);
    };
    for (const auto& channel : clip.channels) {
        if (channel.node < 0 || channel.node >= static_cast<int>(nodes.size())) throw std::runtime_error("Invalid animation target node");
        const auto value = sample(channel);
        auto& node = nodes[channel.node];
        switch (channel.path) {
        case AnimationPath::Translation: node.translation = glm::vec3(value); break;
        case AnimationPath::Scale: node.scale = glm::vec3(value); break;
        case AnimationPath::Rotation: {
            const glm::quat quaternion(value.w, value.x, value.y, value.z);
            if (glm::length(quaternion) < 1e-8f) throw std::runtime_error("Animation evaluates to a zero-length quaternion");
            node.rotation = glm::normalize(quaternion); break;
        }
        }
    }
    std::vector<glm::mat4> global(nodes.size(), glm::mat4(1.0f));
    std::vector<std::uint8_t> state(nodes.size(), 0);
    std::function<const glm::mat4&(int, std::size_t)> evaluateNode = [&](int index, std::size_t depth) -> const glm::mat4& {
        if (index < 0 || index >= static_cast<int>(nodes.size()) || depth > 1024) throw std::runtime_error("Invalid animation node hierarchy");
        if (state[index] == 2) return global[index];
        if (state[index] == 1) throw std::runtime_error("Cyclic animation node hierarchy");
        state[index] = 1;
        const auto& node = nodes[index];
        const auto local = node.usesMatrix ? node.matrix : glm::translate(glm::mat4(1), node.translation) *
            glm::mat4_cast(node.rotation) * glm::scale(glm::mat4(1), node.scale);
        global[index] = node.parent < 0 ? local : evaluateNode(node.parent, depth + 1) * local;
        state[index] = 2;
        return global[index];
    };
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) evaluateNode(i, 0);
    AnimatedVertices result;
    result.minimum = glm::vec3(std::numeric_limits<float>::max());
    result.maximum = glm::vec3(std::numeric_limits<float>::lowest());
    std::size_t vertexCount = 0;
    for (const auto& part : asset.parts) vertexCount += part.vertices.size();
    result.vertices.reserve(vertexCount);
    for (const auto& part : asset.parts) {
        const bool local = !part.localVertices.empty();
        const auto& vertices = local ? part.localVertices : part.vertices;
        const glm::mat4 nodeWorld = local && part.nodeIndex >= 0 ? global.at(part.nodeIndex) : glm::mat4(1.0f);
        std::vector<glm::mat4> joints;
        if (!part.jointIndices.empty()) {
            if (part.jointIndices.size() != vertices.size() || part.jointWeights.size() != vertices.size() ||
                part.inverseBindMatrices.size() != part.joints.size()) throw std::runtime_error("Invalid skin vertex or inverse bind count");
            joints.reserve(part.joints.size());
            for (std::size_t j = 0; j < part.joints.size(); ++j) joints.push_back(global.at(part.joints[j]) * part.inverseBindMatrices[j]);
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto vertex = vertices[i];
            glm::mat4 matrix = nodeWorld;
            if (!joints.empty()) {
                const auto weights = part.jointWeights[i];
                if (weights.x + weights.y + weights.z + weights.w > 1e-8f) {
                    matrix = glm::mat4(0.0f);
                    for (int j = 0; j < 4; ++j) if (weights[j] > 0.0f) matrix += joints.at(part.jointIndices[i][j]) * weights[j];
                }
            }
            vertex.position = glm::vec3(matrix * glm::vec4(vertex.position, 1.0f));
            const auto basis = glm::mat3(matrix);
            const auto normal = (std::abs(glm::determinant(basis)) > 1e-10f ? glm::transpose(glm::inverse(basis)) : glm::mat3(1.0f)) * vertex.normal;
            vertex.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
                !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z))
                throw std::runtime_error("Animation produced non-finite vertex data");
            result.minimum = glm::min(result.minimum, vertex.position); result.maximum = glm::max(result.maximum, vertex.position);
            result.vertices.push_back(vertex);
        }
    }
    return result;
}

MeshAsset builtinMesh(const std::string& source) {
    MeshAsset asset;
    MeshPart part;
    if (source == "builtin:cube") {
        const std::array<glm::vec3, 6> normals = {glm::vec3(0, 0, 1), {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
        const std::array<glm::vec3, 6> tangents = {glm::vec3(1, 0, 0), {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
        for (std::size_t face = 0; face < 6; ++face) {
            const auto normal = normals[face], right = tangents[face], up = glm::cross(normal, right);
            const std::uint32_t base = static_cast<std::uint32_t>(part.vertices.size());
            for (auto corner : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
                MeshVertex vertex;
                vertex.position = (normal + right * corner.x + up * corner.y) * 0.5f;
                vertex.normal = normal; vertex.uv = corner * 0.5f + 0.5f;
                part.vertices.push_back(vertex);
            }
            part.indices.insert(part.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    } else if (source == "builtin:plane") {
        for (auto corner : {glm::vec2(-.5f, -.5f), glm::vec2(-.5f, .5f), glm::vec2(.5f, .5f), glm::vec2(.5f, -.5f)}) {
            MeshVertex vertex; vertex.position = glm::vec3(corner.x, 0, corner.y); vertex.normal = glm::vec3(0, 1, 0); vertex.uv = corner + .5f;
            part.vertices.push_back(vertex);
        }
        part.indices = {0, 1, 2, 0, 2, 3};
    } else if (source == "builtin:sphere") {
        constexpr int rings = 20, segments = 32;
        constexpr float pi = 3.14159265358979323846f;
        for (int y = 0; y <= rings; ++y) for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / segments, v = static_cast<float>(y) / rings;
            MeshVertex vertex;
            vertex.normal = glm::vec3(std::sin(v * pi) * std::cos(u * 2 * pi), std::cos(v * pi), std::sin(v * pi) * std::sin(u * 2 * pi));
            vertex.position = vertex.normal * 0.5f; vertex.uv = glm::vec2(u, v);
            part.vertices.push_back(vertex);
        }
        for (int y = 0; y < rings; ++y) for (int x = 0; x < segments; ++x) {
            const std::uint32_t a = y * (segments + 1) + x, b = a + segments + 1;
            part.indices.insert(part.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    } else throw std::runtime_error("Unknown builtin mesh: " + source);
    asset.parts.push_back(std::move(part)); computeBounds(asset); return asset;
}

} // namespace myth::engine::detail
