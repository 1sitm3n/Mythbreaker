#include "Model.h"
#include "Logger.h"
#include "vulkan/VulkanContext.h"
#include <glm/gtc/matrix_transform.hpp>

// Include stb_image header (implementation is in VulkanTexture.cpp)
#include <stb_image.h>

// tinygltf config
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#include <tiny_gltf.h>

namespace myth {

// Custom image loader for tinygltf
static bool LoadImageData(tinygltf::Image* image, const int image_idx, std::string* err,
                          std::string* warn, int req_width, int req_height,
                          const unsigned char* bytes, int size, void* user_data) {
    (void)image_idx; (void)warn; (void)req_width; (void)req_height; (void)user_data;
    
    int w, h, comp;
    unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
    
    if (!data) {
        if (err) *err = "Failed to load image: " + std::string(stbi_failure_reason());
        return false;
    }
    
    image->width = w;
    image->height = h;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.resize(w * h * 4);
    memcpy(image->image.data(), data, w * h * 4);
    
    stbi_image_free(data);
    return true;
}

void ModelInstance::updateTransform() {
    transform = glm::mat4(1.0f);
    transform = glm::translate(transform, position);
    transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    transform = glm::scale(transform, scale);
}

bool ModelLoader::loadGLTF(const std::string& path, Model& outModel) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    loader.SetImageLoader(LoadImageData, nullptr);

    bool result = false;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb") {
        result = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path);
    } else {
        result = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);
    }

    if (!warn.empty()) Logger::warn("glTF warning: " + warn);
    if (!err.empty()) { Logger::error("glTF error: " + err); return false; }
    if (!result) { Logger::error("Failed to load glTF: " + path); return false; }

    outModel.name = gltfModel.scenes.empty() ? "model" :
                    (gltfModel.scenes[0].name.empty() ? "model" : gltfModel.scenes[0].name);
    outModel.path = path;

    // Load materials
    for (const auto& mat : gltfModel.materials) {
        ModelMaterial material;
        material.name = mat.name;
        if (mat.pbrMetallicRoughness.baseColorFactor.size() >= 4) {
            material.baseColor = glm::vec4(
                static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[0]),
                static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[1]),
                static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[2]),
                static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[3])
            );
        }
        material.metallic = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor);
        material.roughness = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);
        outModel.materials.push_back(material);
    }

    if (outModel.materials.empty()) {
        ModelMaterial defaultMat;
        defaultMat.name = "default";
        defaultMat.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        outModel.materials.push_back(defaultMat);
    }

    // Load meshes - convert to engine's Vertex format
    for (const auto& mesh : gltfModel.meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

            Mesh outMesh;
            outMesh.materialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : 0;

            const float* positions = nullptr;
            const float* normals = nullptr;
            const float* texcoords = nullptr;
            size_t vertexCount = 0;

            if (primitive.attributes.count("POSITION")) {
                const auto& accessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
                const auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                const auto& buffer = gltfModel.buffers[bufferView.buffer];
                positions = reinterpret_cast<const float*>(
                    buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
                vertexCount = accessor.count;
            }

            if (primitive.attributes.count("NORMAL")) {
                const auto& accessor = gltfModel.accessors[primitive.attributes.at("NORMAL")];
                const auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                const auto& buffer = gltfModel.buffers[bufferView.buffer];
                normals = reinterpret_cast<const float*>(
                    buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }

            if (primitive.attributes.count("TEXCOORD_0")) {
                const auto& accessor = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
                const auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                const auto& buffer = gltfModel.buffers[bufferView.buffer];
                texcoords = reinterpret_cast<const float*>(
                    buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }

            // Get material color for vertex coloring
            glm::vec3 matColor(1.0f);
            if (outMesh.materialIndex < outModel.materials.size()) {
                matColor = glm::vec3(outModel.materials[outMesh.materialIndex].baseColor);
            }

            // Build vertices in engine's Vertex format
            outMesh.vertices.reserve(vertexCount);
            for (size_t i = 0; i < vertexCount; i++) {
                vk::Vertex v;
                v.position = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
                v.color = matColor;  // Use material base color
                v.texCoord = texcoords ? glm::vec2(texcoords[i * 2], texcoords[i * 2 + 1]) : glm::vec2(0);
                v.normal = normals ? glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2])
                                   : glm::vec3(0, 1, 0);
                outMesh.vertices.push_back(v);
            }

            // Load indices
            if (primitive.indices >= 0) {
                const auto& accessor = gltfModel.accessors[primitive.indices];
                const auto& bufferView = gltfModel.bufferViews[accessor.bufferView];
                const auto& buffer = gltfModel.buffers[bufferView.buffer];
                const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                outMesh.indices.reserve(accessor.count);

                switch (accessor.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                        const uint16_t* indices = reinterpret_cast<const uint16_t*>(data);
                        for (size_t i = 0; i < accessor.count; i++)
                            outMesh.indices.push_back(static_cast<uint32_t>(indices[i]));
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                        const uint32_t* indices = reinterpret_cast<const uint32_t*>(data);
                        for (size_t i = 0; i < accessor.count; i++)
                            outMesh.indices.push_back(indices[i]);
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                        for (size_t i = 0; i < accessor.count; i++)
                            outMesh.indices.push_back(static_cast<uint32_t>(data[i]));
                        break;
                    }
                }
            } else {
                for (size_t i = 0; i < vertexCount; i++)
                    outMesh.indices.push_back(static_cast<uint32_t>(i));
            }

            outMesh.indexCount = static_cast<uint32_t>(outMesh.indices.size());
            outModel.meshes.push_back(std::move(outMesh));
        }
    }

    computeBounds(outModel);

    size_t totalVerts = 0;
    for (const auto& m : outModel.meshes) totalVerts += m.vertices.size();
    Logger::info("Loaded glTF: " + path + " (" + 
                 std::to_string(outModel.meshes.size()) + " meshes, " +
                 std::to_string(outModel.materials.size()) + " materials, " +
                 std::to_string(totalVerts) + " verts)");

    return true;
}

bool ModelLoader::loadOBJ(const std::string& path, Model& outModel) {
    Logger::warn("OBJ loading not implemented, use glTF instead");
    return false;
}

bool ModelLoader::load(const std::string& path, Model& outModel) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) { Logger::error("No file extension: " + path); return false; }
    std::string ext = path.substr(dotPos);
    if (ext == ".gltf" || ext == ".glb") return loadGLTF(path, outModel);
    if (ext == ".obj") return loadOBJ(path, outModel);
    Logger::error("Unsupported model format: " + ext);
    return false;
}

void ModelLoader::computeBounds(Model& model) {
    model.boundsMin = glm::vec3(FLT_MAX);
    model.boundsMax = glm::vec3(-FLT_MAX);
    for (const auto& mesh : model.meshes) {
        for (const auto& v : mesh.vertices) {
            model.boundsMin = glm::min(model.boundsMin, v.position);
            model.boundsMax = glm::max(model.boundsMax, v.position);
        }
    }
}

void ModelManager::cleanup() {
    for (auto& model : m_models) {
        if (model.vertexBuffer) vkDestroyBuffer(m_context->device(), model.vertexBuffer, nullptr);
        if (model.indexBuffer) vkDestroyBuffer(m_context->device(), model.indexBuffer, nullptr);
        if (model.vertexMemory) vkFreeMemory(m_context->device(), model.vertexMemory, nullptr);
        if (model.indexMemory) vkFreeMemory(m_context->device(), model.indexMemory, nullptr);
    }
    m_models.clear();
    m_pathToId.clear();
}

uint32_t ModelManager::loadModel(const std::string& path) {
    auto it = m_pathToId.find(path);
    if (it != m_pathToId.end()) return it->second;

    Model model;
    if (!ModelLoader::load(path, model)) return UINT32_MAX;

    uploadModelToGPU(model);

    uint32_t id = static_cast<uint32_t>(m_models.size());
    m_models.push_back(std::move(model));
    m_pathToId[path] = id;
    return id;
}

Model* ModelManager::getModel(uint32_t id) {
    return id < m_models.size() ? &m_models[id] : nullptr;
}

void ModelManager::uploadModelToGPU(Model& model) {
    std::vector<vk::Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    for (auto& mesh : model.meshes) {
        mesh.vertexOffset = static_cast<uint32_t>(allVertices.size());
        mesh.indexOffset = static_cast<uint32_t>(allIndices.size());
        allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t idx : mesh.indices) allIndices.push_back(idx);
    }

    if (allVertices.empty()) return;

    VkDeviceSize vbSize = allVertices.size() * sizeof(vk::Vertex);
    VkDeviceSize ibSize = allIndices.size() * sizeof(uint32_t);

    auto createBuffer = [this](VkDeviceSize size, VkBufferUsageFlags usage,
                                VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(m_context->device(), &bufferInfo, nullptr, &buffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_context->device(), buffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_context->physicalDevice(), &memProps);

        uint32_t memTypeIndex = 0;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((memReq.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memTypeIndex = i; break;
            }
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memTypeIndex;
        vkAllocateMemory(m_context->device(), &allocInfo, nullptr, &memory);
        vkBindBufferMemory(m_context->device(), buffer, memory, 0);
    };

    createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, model.vertexBuffer, model.vertexMemory);
    createBuffer(ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, model.indexBuffer, model.indexMemory);

    void* data;
    vkMapMemory(m_context->device(), model.vertexMemory, 0, vbSize, 0, &data);
    memcpy(data, allVertices.data(), vbSize);
    vkUnmapMemory(m_context->device(), model.vertexMemory);

    vkMapMemory(m_context->device(), model.indexMemory, 0, ibSize, 0, &data);
    memcpy(data, allIndices.data(), ibSize);
    vkUnmapMemory(m_context->device(), model.indexMemory);
}

} // namespace myth
