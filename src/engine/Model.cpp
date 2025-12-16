#define GLM_ENABLE_EXPERIMENTAL
#include "Model.h"
#include "Logger.h"
#include "vulkan/VulkanContext.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <stb_image.h>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#include <tiny_gltf.h>

namespace myth {

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
    image->width = w; image->height = h; image->component = 4; image->bits = 8;
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

void ModelInstance::updateAnimation(float dt, const Model& model) {
    if (!model.hasSkeleton || model.animations.empty()) return;
    if (animState.clipIndex < 0 || animState.clipIndex >= (int)model.animations.size()) return;
    if (!animState.playing) return;
    
    const auto& clip = model.animations[animState.clipIndex];
    
    // Update time
    animState.currentTime += dt * animState.speed;
    if (animState.loop && clip.duration > 0) {
        while (animState.currentTime >= clip.duration) {
            animState.currentTime -= clip.duration;
        }
    } else if (animState.currentTime >= clip.duration) {
        animState.currentTime = clip.duration;
        animState.playing = false;
    }
    
    // Resize joint matrices if needed
    if (jointMatrices.size() != model.skeleton.joints.size()) {
        jointMatrices.resize(model.skeleton.joints.size(), glm::mat4(1.0f));
    }
    
    // Sample animation for each joint
    std::vector<glm::mat4> localTransforms(model.skeleton.joints.size());
    for (size_t i = 0; i < model.skeleton.joints.size(); i++) {
        localTransforms[i] = model.skeleton.joints[i].localTransform;
    }
    
    // Apply animation channels
    for (const auto& channel : clip.channels) {
        if (channel.jointIndex < 0 || channel.jointIndex >= (int)localTransforms.size()) continue;
        
        glm::vec3 translation(0.0f);
        glm::quat rotation(1, 0, 0, 0);
        glm::vec3 scale(1.0f);
        
        // Decompose current local transform to get defaults
        glm::vec3 skew; glm::vec4 persp;
        glm::decompose(localTransforms[channel.jointIndex], scale, rotation, translation, skew, persp);
        
        // Sample animated values
        if (!channel.translationKeys.empty()) {
            translation = AnimationUtils::sampleTranslation(channel, animState.currentTime);
        }
        if (!channel.rotationKeys.empty()) {
            rotation = AnimationUtils::sampleRotation(channel, animState.currentTime);
        }
        if (!channel.scaleKeys.empty()) {
            scale = AnimationUtils::sampleScale(channel, animState.currentTime);
        }
        
        // Recompose transform
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        localTransforms[channel.jointIndex] = T * R * S;
    }
    
    // Compute global transforms
    std::vector<glm::mat4> globalTransforms(model.skeleton.joints.size());
    for (size_t i = 0; i < model.skeleton.joints.size(); i++) {
        if (model.skeleton.joints[i].parent < 0) {
            globalTransforms[i] = localTransforms[i];
        } else {
            globalTransforms[i] = globalTransforms[model.skeleton.joints[i].parent] * localTransforms[i];
        }
    }
    
    // Compute final joint matrices (global * inverseBindMatrix)
    for (size_t i = 0; i < model.skeleton.joints.size(); i++) {
        jointMatrices[i] = globalTransforms[i] * model.skeleton.joints[i].inverseBindMatrix;
    }
}

// Helper to get node's local transform
static glm::mat4 getNodeTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 m;
        for (int i = 0; i < 16; i++) m[i/4][i%4] = static_cast<float>(node.matrix[i]);
        return m;
    }
    glm::vec3 t(0), s(1); glm::quat r(1,0,0,0);
    if (node.translation.size() == 3) t = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    if (node.rotation.size() == 4) r = glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
    if (node.scale.size() == 3) s = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
    return glm::translate(glm::mat4(1), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1), s);
}

// Load skeleton from glTF skin
static void loadSkeleton(const tinygltf::Model& gltf, int skinIndex, Model& model) {
    if (skinIndex < 0 || skinIndex >= (int)gltf.skins.size()) return;
    const auto& skin = gltf.skins[skinIndex];
    
    model.hasSkeleton = true;
    model.skeleton.joints.resize(skin.joints.size());
    
    // Load inverse bind matrices
    std::vector<glm::mat4> inverseBindMatrices(skin.joints.size(), glm::mat4(1.0f));
    if (skin.inverseBindMatrices >= 0) {
        const auto& accessor = gltf.accessors[skin.inverseBindMatrices];
        const auto& bufferView = gltf.bufferViews[accessor.bufferView];
        const auto& buffer = gltf.buffers[bufferView.buffer];
        const float* data = reinterpret_cast<const float*>(
            buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
        for (size_t i = 0; i < accessor.count && i < skin.joints.size(); i++) {
            for (int j = 0; j < 16; j++) {
                inverseBindMatrices[i][j/4][j%4] = data[i * 16 + j];
            }
        }
    }
    
    // Build joint hierarchy
    for (size_t i = 0; i < skin.joints.size(); i++) {
        int nodeIndex = skin.joints[i];
        const auto& node = gltf.nodes[nodeIndex];
        
        Joint& joint = model.skeleton.joints[i];
        joint.name = node.name.empty() ? "joint_" + std::to_string(i) : node.name;
        joint.inverseBindMatrix = inverseBindMatrices[i];
        joint.localTransform = getNodeTransform(node);
        joint.parent = -1;
        
        model.skeleton.jointNameToIndex[joint.name] = static_cast<int32_t>(i);
        
        // Find parent
        for (size_t j = 0; j < skin.joints.size(); j++) {
            if (i == j) continue;
            const auto& parentNode = gltf.nodes[skin.joints[j]];
            for (int child : parentNode.children) {
                if (child == nodeIndex) {
                    joint.parent = static_cast<int32_t>(j);
                    model.skeleton.joints[j].children.push_back(static_cast<int32_t>(i));
                    break;
                }
            }
            if (joint.parent >= 0) break;
        }
    }
    
    Logger::info("  Skeleton: " + std::to_string(skin.joints.size()) + " joints");
}

// Load animations from glTF
static void loadAnimations(const tinygltf::Model& gltf, Model& model) {
    for (const auto& anim : gltf.animations) {
        AnimationClip clip;
        clip.name = anim.name.empty() ? "animation_" + std::to_string(model.animations.size()) : anim.name;
        clip.duration = 0.0f;
        
        for (const auto& channel : anim.channels) {
            if (channel.target_node < 0) continue;
            
            // Find joint index for this node
            int jointIndex = -1;
            const auto& targetNode = gltf.nodes[channel.target_node];
            auto it = model.skeleton.jointNameToIndex.find(targetNode.name);
            if (it != model.skeleton.jointNameToIndex.end()) {
                jointIndex = it->second;
            } else {
                // Try to find by node index in skin joints
                for (const auto& skin : gltf.skins) {
                    for (size_t i = 0; i < skin.joints.size(); i++) {
                        if (skin.joints[i] == channel.target_node) {
                            jointIndex = static_cast<int>(i);
                            break;
                        }
                    }
                    if (jointIndex >= 0) break;
                }
            }
            if (jointIndex < 0) continue;
            
            const auto& sampler = anim.samplers[channel.sampler];
            const auto& inputAccessor = gltf.accessors[sampler.input];
            const auto& outputAccessor = gltf.accessors[sampler.output];
            
            // Get timestamps
            const auto& inputView = gltf.bufferViews[inputAccessor.bufferView];
            const auto& inputBuffer = gltf.buffers[inputView.buffer];
            const float* times = reinterpret_cast<const float*>(
                inputBuffer.data.data() + inputView.byteOffset + inputAccessor.byteOffset);
            
            // Get values
            const auto& outputView = gltf.bufferViews[outputAccessor.bufferView];
            const auto& outputBuffer = gltf.buffers[outputView.buffer];
            const float* values = reinterpret_cast<const float*>(
                outputBuffer.data.data() + outputView.byteOffset + outputAccessor.byteOffset);
            
            AnimationChannel animChannel;
            animChannel.jointIndex = jointIndex;
            
            if (channel.target_path == "translation") {
                animChannel.path = AnimationChannel::Path::Translation;
                for (size_t i = 0; i < inputAccessor.count; i++) {
                    Keyframe<glm::vec3> kf;
                    kf.time = times[i];
                    kf.value = glm::vec3(values[i*3], values[i*3+1], values[i*3+2]);
                    animChannel.translationKeys.push_back(kf);
                    clip.duration = std::max(clip.duration, kf.time);
                }
            } else if (channel.target_path == "rotation") {
                animChannel.path = AnimationChannel::Path::Rotation;
                for (size_t i = 0; i < inputAccessor.count; i++) {
                    Keyframe<glm::quat> kf;
                    kf.time = times[i];
                    kf.value = glm::quat(values[i*4+3], values[i*4], values[i*4+1], values[i*4+2]);
                    animChannel.rotationKeys.push_back(kf);
                    clip.duration = std::max(clip.duration, kf.time);
                }
            } else if (channel.target_path == "scale") {
                animChannel.path = AnimationChannel::Path::Scale;
                for (size_t i = 0; i < inputAccessor.count; i++) {
                    Keyframe<glm::vec3> kf;
                    kf.time = times[i];
                    kf.value = glm::vec3(values[i*3], values[i*3+1], values[i*3+2]);
                    animChannel.scaleKeys.push_back(kf);
                    clip.duration = std::max(clip.duration, kf.time);
                }
            }
            
            clip.channels.push_back(animChannel);
        }
        
        if (!clip.channels.empty()) {
            Logger::info("  Animation: '" + clip.name + "' (" + 
                        std::to_string(clip.duration) + "s, " + 
                        std::to_string(clip.channels.size()) + " channels)");
            model.animations.push_back(std::move(clip));
        }
    }
}

bool ModelLoader::loadGLTF(const std::string& path, Model& outModel) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    loader.SetImageLoader(LoadImageData, nullptr);

    bool result = path.size() >= 4 && path.substr(path.size() - 4) == ".glb"
        ? loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path)
        : loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);

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
                (float)mat.pbrMetallicRoughness.baseColorFactor[0],
                (float)mat.pbrMetallicRoughness.baseColorFactor[1],
                (float)mat.pbrMetallicRoughness.baseColorFactor[2],
                (float)mat.pbrMetallicRoughness.baseColorFactor[3]);
        }
        material.metallic = (float)mat.pbrMetallicRoughness.metallicFactor;
        material.roughness = (float)mat.pbrMetallicRoughness.roughnessFactor;
        outModel.materials.push_back(material);
    }
    if (outModel.materials.empty()) {
        ModelMaterial def; def.name = "default"; def.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        outModel.materials.push_back(def);
    }

    // Find skin (if any)
    int skinIndex = -1;
    for (const auto& node : gltfModel.nodes) {
        if (node.skin >= 0) { skinIndex = node.skin; break; }
    }
    
    // Load skeleton if present
    if (skinIndex >= 0) {
        loadSkeleton(gltfModel, skinIndex, outModel);
        loadAnimations(gltfModel, outModel);
    }

    // Load meshes
    for (size_t meshIdx = 0; meshIdx < gltfModel.meshes.size(); meshIdx++) {
        const auto& mesh = gltfModel.meshes[meshIdx];
        
        // Find which node uses this mesh to get skin info
        int nodeSkinIndex = -1;
        for (const auto& node : gltfModel.nodes) {
            if (node.mesh == (int)meshIdx) { nodeSkinIndex = node.skin; break; }
        }
        
        for (const auto& primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

            Mesh outMesh;
            outMesh.materialIndex = primitive.material >= 0 ? (uint32_t)primitive.material : 0;
            outMesh.isSkinned = (nodeSkinIndex >= 0 && outModel.hasSkeleton);

            const float* positions = nullptr;
            const float* normals = nullptr;
            const float* texcoords = nullptr;
            const uint8_t* joints = nullptr;
            const float* weights = nullptr;
            int jointsComponentType = 0;
            size_t vertexCount = 0;

            if (primitive.attributes.count("POSITION")) {
                const auto& acc = gltfModel.accessors[primitive.attributes.at("POSITION")];
                const auto& view = gltfModel.bufferViews[acc.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                positions = (const float*)(buf.data.data() + view.byteOffset + acc.byteOffset);
                vertexCount = acc.count;
            }
            if (primitive.attributes.count("NORMAL")) {
                const auto& acc = gltfModel.accessors[primitive.attributes.at("NORMAL")];
                const auto& view = gltfModel.bufferViews[acc.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                normals = (const float*)(buf.data.data() + view.byteOffset + acc.byteOffset);
            }
            if (primitive.attributes.count("TEXCOORD_0")) {
                const auto& acc = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
                const auto& view = gltfModel.bufferViews[acc.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                texcoords = (const float*)(buf.data.data() + view.byteOffset + acc.byteOffset);
            }
            if (primitive.attributes.count("JOINTS_0")) {
                const auto& acc = gltfModel.accessors[primitive.attributes.at("JOINTS_0")];
                const auto& view = gltfModel.bufferViews[acc.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                joints = buf.data.data() + view.byteOffset + acc.byteOffset;
                jointsComponentType = acc.componentType;
            }
            if (primitive.attributes.count("WEIGHTS_0")) {
                const auto& acc = gltfModel.accessors[primitive.attributes.at("WEIGHTS_0")];
                const auto& view = gltfModel.bufferViews[acc.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                weights = (const float*)(buf.data.data() + view.byteOffset + acc.byteOffset);
            }

            glm::vec3 matColor(1.0f);
            if (outMesh.materialIndex < outModel.materials.size()) {
                matColor = glm::vec3(outModel.materials[outMesh.materialIndex].baseColor);
            }

            // Build vertices
            if (outMesh.isSkinned && joints && weights) {
                outMesh.skinnedVertices.reserve(vertexCount);
                for (size_t i = 0; i < vertexCount; i++) {
                    SkinnedVertex v;
                    v.position = glm::vec3(positions[i*3], positions[i*3+1], positions[i*3+2]);
                    v.color = matColor;
                    v.texCoord = texcoords ? glm::vec2(texcoords[i*2], texcoords[i*2+1]) : glm::vec2(0);
                    v.normal = normals ? glm::vec3(normals[i*3], normals[i*3+1], normals[i*3+2]) : glm::vec3(0,1,0);
                    
                    // Joint indices
                    if (jointsComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        v.jointIndices = glm::ivec4(joints[i*4], joints[i*4+1], joints[i*4+2], joints[i*4+3]);
                    } else if (jointsComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        const uint16_t* j16 = (const uint16_t*)joints;
                        v.jointIndices = glm::ivec4(j16[i*4], j16[i*4+1], j16[i*4+2], j16[i*4+3]);
                    }
                    v.jointWeights = glm::vec4(weights[i*4], weights[i*4+1], weights[i*4+2], weights[i*4+3]);
                    
                    outMesh.skinnedVertices.push_back(v);
                }
            } else {
                outMesh.vertices.reserve(vertexCount);
                for (size_t i = 0; i < vertexCount; i++) {
                    vk::Vertex v;
                    v.position = glm::vec3(positions[i*3], positions[i*3+1], positions[i*3+2]);
                    v.color = matColor;
                    v.texCoord = texcoords ? glm::vec2(texcoords[i*2], texcoords[i*2+1]) : glm::vec2(0);
                    v.normal = normals ? glm::vec3(normals[i*3], normals[i*3+1], normals[i*3+2]) : glm::vec3(0,1,0);
                    outMesh.vertices.push_back(v);
                }
            }

            // Load indices
            if (primitive.indices >= 0) {
                const auto& acc = gltfModel.accessors[primitive.indices];
                const auto& view = gltfModel.bufferViews[acc.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                const uint8_t* data = buf.data.data() + view.byteOffset + acc.byteOffset;
                outMesh.indices.reserve(acc.count);
                for (size_t i = 0; i < acc.count; i++) {
                    uint32_t idx = 0;
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        idx = ((const uint16_t*)data)[i];
                    else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                        idx = ((const uint32_t*)data)[i];
                    else idx = data[i];
                    outMesh.indices.push_back(idx);
                }
            } else {
                for (size_t i = 0; i < vertexCount; i++) outMesh.indices.push_back((uint32_t)i);
            }

            outMesh.indexCount = (uint32_t)outMesh.indices.size();
            outModel.meshes.push_back(std::move(outMesh));
        }
    }

    computeBounds(outModel);

    size_t totalVerts = 0;
    for (const auto& m : outModel.meshes) {
        totalVerts += m.isSkinned ? m.skinnedVertices.size() : m.vertices.size();
    }
    Logger::info("Loaded glTF: " + path + " (" + std::to_string(outModel.meshes.size()) + " meshes, " +
                 std::to_string(totalVerts) + " verts" + 
                 (outModel.hasSkeleton ? ", animated" : "") + ")");

    return true;
}

bool ModelLoader::loadOBJ(const std::string& path, Model& outModel) {
    Logger::warn("OBJ loading not implemented");
    return false;
}

bool ModelLoader::load(const std::string& path, Model& outModel) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) { Logger::error("No extension: " + path); return false; }
    std::string ext = path.substr(dot);
    if (ext == ".gltf" || ext == ".glb") return loadGLTF(path, outModel);
    if (ext == ".obj") return loadOBJ(path, outModel);
    Logger::error("Unsupported format: " + ext);
    return false;
}

void ModelLoader::computeBounds(Model& model) {
    model.boundsMin = glm::vec3(FLT_MAX);
    model.boundsMax = glm::vec3(-FLT_MAX);
    for (const auto& mesh : model.meshes) {
        if (mesh.isSkinned) {
            for (const auto& v : mesh.skinnedVertices) {
                model.boundsMin = glm::min(model.boundsMin, v.position);
                model.boundsMax = glm::max(model.boundsMax, v.position);
            }
        } else {
            for (const auto& v : mesh.vertices) {
                model.boundsMin = glm::min(model.boundsMin, v.position);
                model.boundsMax = glm::max(model.boundsMax, v.position);
            }
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
    uint32_t id = (uint32_t)m_models.size();
    m_models.push_back(std::move(model));
    m_pathToId[path] = id;
    return id;
}

Model* ModelManager::getModel(uint32_t id) {
    return id < m_models.size() ? &m_models[id] : nullptr;
}

void ModelManager::uploadModelToGPU(Model& model) {
    // For now, upload as static vertices (skinned models use same buffer, skinning done in shader)
    // We upload skinned vertices as regular Vertex for now (will add skinned pipeline later)
    std::vector<vk::Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    for (auto& mesh : model.meshes) {
        mesh.vertexOffset = (uint32_t)allVertices.size();
        mesh.indexOffset = (uint32_t)allIndices.size();
        
        if (mesh.isSkinned) {
            // Convert skinned to regular for now (animation will be CPU-side initially)
            for (const auto& sv : mesh.skinnedVertices) {
                vk::Vertex v;
                v.position = sv.position;
                v.color = sv.color;
                v.texCoord = sv.texCoord;
                v.normal = sv.normal;
                allVertices.push_back(v);
            }
        } else {
            allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        }
        
        for (uint32_t idx : mesh.indices) allIndices.push_back(idx);
    }

    if (allVertices.empty()) return;

    VkDeviceSize vbSize = allVertices.size() * sizeof(vk::Vertex);
    VkDeviceSize ibSize = allIndices.size() * sizeof(uint32_t);

    auto createBuffer = [this](VkDeviceSize size, VkBufferUsageFlags usage,
                                VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(m_context->device(), &info, nullptr, &buffer);
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(m_context->device(), buffer, &req);
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(m_context->physicalDevice(), &props);
        uint32_t memType = 0;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((req.memoryTypeBits & (1<<i)) && (props.memoryTypes[i].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memType = i; break;
            }
        }
        VkMemoryAllocateInfo alloc{}; alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size; alloc.memoryTypeIndex = memType;
        vkAllocateMemory(m_context->device(), &alloc, nullptr, &memory);
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

