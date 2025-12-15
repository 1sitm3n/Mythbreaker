#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "vulkan/VulkanTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cfloat>

namespace myth {

namespace vk {
    class VulkanContext;
}

// Material from glTF
struct ModelMaterial {
    std::string name;
    glm::vec4 baseColor = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::string baseColorTexturePath;
};

// Single mesh within a model - uses engine's Vertex format
struct Mesh {
    std::vector<vk::Vertex> vertices;  // Use engine's Vertex format
    std::vector<uint32_t> indices;
    uint32_t materialIndex = 0;
    uint32_t indexCount = 0;
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
};

// Complete model with GPU resources
struct Model {
    std::string name;
    std::string path;
    std::vector<Mesh> meshes;
    std::vector<ModelMaterial> materials;
    
    // Bounding box
    glm::vec3 boundsMin = glm::vec3(FLT_MAX);
    glm::vec3 boundsMax = glm::vec3(-FLT_MAX);
    
    // GPU buffers
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    
    glm::vec3 getCenter() const { return (boundsMin + boundsMax) * 0.5f; }
    glm::vec3 getSize() const { return boundsMax - boundsMin; }
};

// Model instance for placing in world
struct ModelInstance {
    uint32_t modelId = UINT32_MAX;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);  // Euler angles in degrees
    glm::vec3 scale = glm::vec3(1.0f);
    glm::mat4 transform = glm::mat4(1.0f);
    
    void updateTransform();
};

// Static loader functions
class ModelLoader {
public:
    static bool load(const std::string& path, Model& outModel);
    static bool loadGLTF(const std::string& path, Model& outModel);
    static bool loadOBJ(const std::string& path, Model& outModel);
    static void computeBounds(Model& model);
};

// Manager for loaded models
class ModelManager {
public:
    void init(vk::VulkanContext* ctx) { m_context = ctx; }
    void cleanup();
    
    uint32_t loadModel(const std::string& path);
    Model* getModel(uint32_t id);
    
private:
    void uploadModelToGPU(Model& model);
    
    vk::VulkanContext* m_context = nullptr;
    std::vector<Model> m_models;
    std::unordered_map<std::string, uint32_t> m_pathToId;
};

} // namespace myth
