#include <random>
#include "engine/Logger.h"
#include "engine/Timer.h"
#include "engine/Input.h"
#include "engine/Noise.h"
#include "engine/Item.h"
#include "engine/RegionState.h"
#include "engine/SaveLoad.h"
#include "engine/ecs/World.h"
#include "engine/ecs/Systems.h"
#include "engine/vulkan/VulkanContext.h"
#include "engine/vulkan/VulkanSwapchain.h"
#include "engine/vulkan/VulkanPipeline.h"
#include "engine/UI.h"
#include "engine/Model.h"
#include "engine/vulkan/VulkanBuffer.h"
#include "engine/vulkan/VulkanDescriptors.h"
#include "engine/vulkan/VulkanTexture.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

using namespace myth;
using namespace myth::vk;
using namespace myth::ecs;

struct ChunkCoord { int x, z; bool operator==(const ChunkCoord& o) const { return x==o.x && z==o.z; } };
struct ChunkCoordHash { size_t operator()(const ChunkCoord& c) const { return std::hash<int>()(c.x)^(std::hash<int>()(c.z)<<16); } };

// Terrain height function using noise
float terrainHeight(float worldX, float worldZ) {
    const float scale = 0.02f;
    const float heightScale = 8.0f;
    
    // Base terrain with FBM
    float h = Noise::fbm(worldX * scale, worldZ * scale, 5, 2.0f, 0.5f);
    
    // Add some ridged noise for variation
    float ridged = Noise::ridged(worldX * scale * 0.5f, worldZ * scale * 0.5f, 3);
    h = h * 0.7f + ridged * 0.3f;
    
    return h * heightScale;
}

struct Chunk {
    ChunkCoord coord;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    static constexpr int RESOLUTION = 16; // vertices per side
    
    void generate(float chunkSize) {
        vertices.clear();
        indices.clear();
        
        float step = chunkSize / (RESOLUTION - 1);
        float worldX = coord.x * chunkSize;
        float worldZ = coord.z * chunkSize;
        float uvStep = 1.0f / (RESOLUTION - 1);
        
        // Generate vertices
        for (int z = 0; z < RESOLUTION; z++) {
            for (int x = 0; x < RESOLUTION; x++) {
                float wx = worldX + x * step - chunkSize / 2.0f;
                float wz = worldZ + z * step - chunkSize / 2.0f;
                float h = terrainHeight(wx, wz);
                
                Vertex v;
                v.position = glm::vec3(wx, h, wz);
                v.color = glm::vec3(1.0f);
                v.texCoord = glm::vec2(x * uvStep * 4.0f, z * uvStep * 4.0f); // Tile texture
                v.normal = glm::vec3(0, 1, 0); // Will calculate properly
                vertices.push_back(v);
            }
        }
        
        // Calculate normals from height differences
        for (int z = 0; z < RESOLUTION; z++) {
            for (int x = 0; x < RESOLUTION; x++) {
                float wx = worldX + x * step - chunkSize / 2.0f;
                float wz = worldZ + z * step - chunkSize / 2.0f;
                
                float hL = terrainHeight(wx - step, wz);
                float hR = terrainHeight(wx + step, wz);
                float hD = terrainHeight(wx, wz - step);
                float hU = terrainHeight(wx, wz + step);
                
                glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * step, hD - hU));
                vertices[z * RESOLUTION + x].normal = normal;
            }
        }
        
        // Generate indices
        for (int z = 0; z < RESOLUTION - 1; z++) {
            for (int x = 0; x < RESOLUTION - 1; x++) {
                uint32_t topLeft = z * RESOLUTION + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * RESOLUTION + x;
                uint32_t bottomRight = bottomLeft + 1;
                
                // Two triangles per quad
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
    }
};

class ChunkManager {
public:
    float chunkSize = 32.0f;
    int loadRadius = 4;
    
    void update(const glm::vec3& playerPos) {
        int px = static_cast<int>(floor(playerPos.x / chunkSize));
        int pz = static_cast<int>(floor(playerPos.z / chunkSize));
        
        for (int x = px - loadRadius; x <= px + loadRadius; x++) {
            for (int z = pz - loadRadius; z <= pz + loadRadius; z++) {
                ChunkCoord coord{x, z};
                if (m_chunks.find(coord) == m_chunks.end()) {
                    Chunk chunk; chunk.coord = coord;
                    chunk.generate(chunkSize);
                    m_chunks[coord] = std::move(chunk);
                    m_dirty = true;
                }
            }
        }
        
        std::vector<ChunkCoord> toUnload;
        for (auto& [coord, chunk] : m_chunks) {
            if (abs(coord.x - px) > loadRadius + 1 || abs(coord.z - pz) > loadRadius + 1)
                toUnload.push_back(coord);
        }
        for (const auto& coord : toUnload) { m_chunks.erase(coord); m_dirty = true; }
    }
    
    void forceRebuild() { m_dirty = true; }
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }
    
    void buildMesh(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        verts.clear(); inds.clear();
        for (auto& [coord, chunk] : m_chunks) {
            uint32_t base = static_cast<uint32_t>(verts.size());
            verts.insert(verts.end(), chunk.vertices.begin(), chunk.vertices.end());
            for (uint32_t idx : chunk.indices) inds.push_back(base + idx);
        }
    }
    
    float getHeightAt(float x, float z) const {
        return terrainHeight(x, z);
    }
    
private:
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_chunks;
    bool m_dirty = false;
};

std::vector<Vertex> createCube(float size) {
    float s = size / 2.0f; glm::vec3 w(1.0f);
    return {
        {{-s,-s,s},w,{0,0},{0,0,1}},{{s,-s,s},w,{1,0},{0,0,1}},{{s,s,s},w,{1,1},{0,0,1}},{{-s,s,s},w,{0,1},{0,0,1}},
        {{s,-s,-s},w,{0,0},{0,0,-1}},{{-s,-s,-s},w,{1,0},{0,0,-1}},{{-s,s,-s},w,{1,1},{0,0,-1}},{{s,s,-s},w,{0,1},{0,0,-1}},
        {{-s,s,s},w,{0,0},{0,1,0}},{{s,s,s},w,{1,0},{0,1,0}},{{s,s,-s},w,{1,1},{0,1,0}},{{-s,s,-s},w,{0,1},{0,1,0}},
        {{-s,-s,-s},w,{0,0},{0,-1,0}},{{s,-s,-s},w,{1,0},{0,-1,0}},{{s,-s,s},w,{1,1},{0,-1,0}},{{-s,-s,s},w,{0,1},{0,-1,0}},
        {{s,-s,s},w,{0,0},{1,0,0}},{{s,-s,-s},w,{1,0},{1,0,0}},{{s,s,-s},w,{1,1},{1,0,0}},{{s,s,s},w,{0,1},{1,0,0}},
        {{-s,-s,-s},w,{0,0},{-1,0,0}},{{-s,-s,s},w,{1,0},{-1,0,0}},{{-s,s,s},w,{1,1},{-1,0,0}},{{-s,s,-s},w,{0,1},{-1,0,0}}
    };
}

std::vector<Vertex> createPlayerMesh(float w, float h) {
    float hw = w/2.0f; glm::vec3 c(1.0f);
    return {
        {{-hw,0,hw},c,{0,0},{0,0,1}},{{hw,0,hw},c,{1,0},{0,0,1}},{{hw,h,hw},c,{1,1},{0,0,1}},{{-hw,h,hw},c,{0,1},{0,0,1}},
        {{hw,0,-hw},c,{0,0},{0,0,-1}},{{-hw,0,-hw},c,{1,0},{0,0,-1}},{{-hw,h,-hw},c,{1,1},{0,0,-1}},{{hw,h,-hw},c,{0,1},{0,0,-1}},
        {{-hw,h,hw},c,{0,0},{0,1,0}},{{hw,h,hw},c,{1,0},{0,1,0}},{{hw,h,-hw},c,{1,1},{0,1,0}},{{-hw,h,-hw},c,{0,1},{0,1,0}},
        {{-hw,0,-hw},c,{0,0},{0,-1,0}},{{hw,0,-hw},c,{1,0},{0,-1,0}},{{hw,0,hw},c,{1,1},{0,-1,0}},{{-hw,0,hw},c,{0,1},{0,-1,0}},
        {{hw,0,hw},c,{0,0},{1,0,0}},{{hw,0,-hw},c,{1,0},{1,0,0}},{{hw,h,-hw},c,{1,1},{1,0,0}},{{hw,h,hw},c,{0,1},{1,0,0}},
        {{-hw,0,-hw},c,{0,0},{-1,0,0}},{{-hw,0,hw},c,{1,0},{-1,0,0}},{{-hw,h,hw},c,{1,1},{-1,0,0}},{{-hw,h,-hw},c,{0,1},{-1,0,0}}
    };
}

std::vector<uint32_t> createBoxIndices(uint32_t base) {
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; f++) { uint32_t b = base + f * 4; idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3}); }
    return idx;
}

struct MeshInfo { uint32_t indexStart, indexCount; int32_t vertexOffset; };

class Application {
public:
    void run() { initWindow(); initVulkan(); mainLoop(); cleanup(); }
private:
    GLFWwindow* m_window = nullptr; VulkanContext m_context; VulkanSwapchain m_swapchain; DescriptorManager m_descriptors;
    VulkanPipeline m_skyPipeline;
    VulkanPipeline m_uiPipeline;
    HUDState m_hud;
    std::vector<UIVertex> m_uiVertices;
    VulkanBuffer m_uiVB;
    VulkanPipeline m_litPipeline;
    VulkanPipeline m_skinnedPipeline;
    VulkanBuffer m_terrainVB, m_terrainIB; uint32_t m_terrainIndexCount = 0;
    VulkanBuffer m_staticVB, m_staticIB; std::vector<MeshInfo> m_meshes;
    VulkanTexture m_groundTexture, m_stoneTexture, m_playerTexture, m_enemyTexture;
    VulkanTexture m_npcTexture;
    uint32_t m_groundMaterial = 0, m_stoneMaterial = 0, m_playerMaterial = 0, m_enemyMaterial = 0;
    uint32_t m_npcMaterial = 0;
    ModelManager m_modelManager;
    std::vector<ModelInstance> m_modelInstances;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailable, m_renderFinished;
    std::vector<VkFence> m_inFlight;
    uint32_t m_currentFrame = 0; bool m_framebufferResized = false;
    World m_world; std::vector<Entity> m_enemyEntities; std::vector<Entity> m_npcEntities;
    DialogueSystem m_dialogue; ChunkManager m_chunks; RegionStateMachine m_regions;
    bool m_mouseCaptured = true; float m_scrollDelta = 0.0f;
    Timer m_timer; float m_logTimer = 0.0f, m_totalPlayTime = 0.0f;
    RegionVisuals m_currentVisuals; RegionState m_lastLoggedState = RegionState::Stable;
    
    glm::vec3 m_sunDirection = glm::normalize(glm::vec3(0.5f, -0.8f, 0.3f));
    float m_sunIntensity = 1.2f;
    glm::vec3 m_sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
    float m_ambientIntensity = 0.3f;
    glm::vec3 m_skyColorTop = glm::vec3(0.4f, 0.6f, 0.9f);
    glm::vec3 m_skyColorBottom = glm::vec3(0.7f, 0.8f, 0.95f);

    void initWindow() {
        glfwInit(); glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(1280, 720, "Mythbreaker - NPC & Dialogue", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) {
            reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_framebufferResized = true;
        });
        glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double y) {
            reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_scrollDelta = static_cast<float>(y);
        });
        Input::instance().init(m_window);
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    void initVulkan() {
        Logger::info("=== MYTHBREAKER ENGINE ===");
        Logger::info("SkinnedVertex size: " + std::to_string(sizeof(SkinnedVertex)));
        Logger::info("  position offset: " + std::to_string(offsetof(SkinnedVertex, position)));
        Logger::info("  _pad0 offset: " + std::to_string(offsetof(SkinnedVertex, _pad0)));
        Logger::info("  color offset: " + std::to_string(offsetof(SkinnedVertex, color)));
        Logger::info("  _pad1 offset: " + std::to_string(offsetof(SkinnedVertex, _pad1)));
        Logger::info("  texCoord offset: " + std::to_string(offsetof(SkinnedVertex, texCoord)));
        Logger::info("  _pad2 offset: " + std::to_string(offsetof(SkinnedVertex, _pad2)));
        Logger::info("  normal offset: " + std::to_string(offsetof(SkinnedVertex, normal)));
        Logger::info("  _pad3 offset: " + std::to_string(offsetof(SkinnedVertex, _pad3)));
        Logger::info("  jointIndices offset: " + std::to_string(offsetof(SkinnedVertex, jointIndices)));
        Logger::info("  jointWeights offset: " + std::to_string(offsetof(SkinnedVertex, jointWeights)));
        Logger::info("Version 0.8.0 - Milestone 15: NPC & Dialogue");
        m_context.init(m_window);
        m_swapchain.init(&m_context, m_window);
        m_descriptors.init(&m_context);
        m_skyPipeline.initSky(&m_context, &m_swapchain, &m_descriptors, "shaders/sky.vert.spv", "shaders/sky.frag.spv");
        m_litPipeline.init(&m_context, &m_swapchain, &m_descriptors, "shaders/lit.vert.spv", "shaders/lit.frag.spv");
        m_skinnedPipeline.initSkinned(&m_context, &m_swapchain, &m_descriptors, "shaders/skinned.vert.spv", "shaders/skinned.frag.spv");
        m_uiPipeline.initUI(&m_context, &m_swapchain, "shaders/ui.vert.spv", "shaders/ui.frag.spv");
        createUIBuffers();
        m_currentVisuals = RegionVisuals::forState(RegionState::Stable);
        createTextures();
        createMeshes();
        createEntities();
        createSyncObjects();
        m_chunks.update(glm::vec3(0));
        rebuildTerrain();
        spawnPickups();
        spawnEnemies();
        spawnNPCs();
        
        // Initialize model manager and load models
        m_modelManager.init(&m_context);
        loadModels();

        Logger::info("Terrain: 32m chunks, 16x16 vertices each");
        Logger::info("LMB=Attack|F=Talk|I=Inv|E=Use|Q=Drop|1-5=Slot");
    }

    void createUIBuffers() {
        // Create UI vertex buffer (dynamic, host-visible)
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = 50000 * sizeof(UIVertex);  // Enough for many quads
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        m_uiVB.create(&m_context, 50000 * sizeof(UIVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    
    void loadModels() {
        // Load the animated Fox model
        uint32_t foxId = m_modelManager.loadModel("assets/models/Fox.glb");
        
        if (foxId != UINT32_MAX) {
            Model* foxModel = m_modelManager.getModel(foxId);
            
            // Create fox instances
            for (int i = 0; i < 3; i++) {
                ModelInstance inst;
                inst.modelId = foxId;
                float x = 10.0f + i * 8.0f;
                float z = 20.0f;
                float y = m_chunks.getHeightAt(x, z);
                inst.position = glm::vec3(x, y, z);
                inst.rotation = glm::vec3(0, -90.0f + i * 30.0f, 0);
                inst.scale = glm::vec3(0.05f);  // Fox is large
                inst.updateTransform();
                
                // Start animation if available
                if (foxModel && !foxModel->animations.empty()) {
                    inst.animState.clipIndex = 0;
                    inst.animState.playing = true;
                    inst.animState.loop = true;
                    inst.animState.speed = 1.0f;
                }
                
                m_modelInstances.push_back(inst);
            }
            Logger::info("Created " + std::to_string(m_modelInstances.size()) + " fox instances");
        }
        
        // Also load Duck
        uint32_t duckId = m_modelManager.loadModel("assets/models/Duck.glb");
        if (duckId != UINT32_MAX) {
            for (int i = 0; i < 3; i++) {
                ModelInstance inst;
                inst.modelId = duckId;
                float x = -15.0f + i * 10.0f;
                float z = 25.0f;
                inst.position = glm::vec3(x, m_chunks.getHeightAt(x, z), z);
                inst.rotation = glm::vec3(0, i * 45.0f, 0);
                inst.scale = glm::vec3(0.01f);
                inst.updateTransform();
                m_modelInstances.push_back(inst);
            }
        }
    }

        void createTextures() {
        std::mt19937 rng(42);
        std::vector<uint8_t> groundPixels(256 * 256 * 4);
        for (int i = 0; i < 256 * 256; i++) {
            float n = (rng() % 100) / 100.0f;
            uint8_t b = static_cast<uint8_t>(50 + n * 50);
            groundPixels[i*4+0] = static_cast<uint8_t>(b * 0.6f);   // More green for grass
            groundPixels[i*4+1] = static_cast<uint8_t>(b * 0.8f);
            groundPixels[i*4+2] = static_cast<uint8_t>(b * 0.3f);
            groundPixels[i*4+3] = 255;
        }
        m_groundTexture.loadFromMemory(&m_context, groundPixels.data(), 256, 256);
        m_groundMaterial = m_descriptors.createMaterial(m_groundTexture);
        
        std::vector<uint8_t> stonePixels(128 * 128 * 4);
        for (int i = 0; i < 128 * 128; i++) {
            float n = (rng() % 100) / 100.0f;
            uint8_t b = static_cast<uint8_t>(100 + n * 80);
            stonePixels[i*4+0] = b;
            stonePixels[i*4+1] = static_cast<uint8_t>(b*0.95f);
            stonePixels[i*4+2] = static_cast<uint8_t>(b*0.9f);
            stonePixels[i*4+3] = 255;
        }
        m_stoneTexture.loadFromMemory(&m_context, stonePixels.data(), 128, 128);
        m_stoneMaterial = m_descriptors.createMaterial(m_stoneTexture);
        
        std::vector<uint8_t> playerPixels(64 * 64 * 4);
        for (int i = 0; i < 64 * 64; i++) {
            float n = (rng() % 20) / 100.0f;
            playerPixels[i*4+0] = static_cast<uint8_t>(220 + n * 20);
            playerPixels[i*4+1] = static_cast<uint8_t>(180 + n * 20);
            playerPixels[i*4+2] = static_cast<uint8_t>(140 + n * 20);
            playerPixels[i*4+3] = 255;
        }
        m_playerTexture.loadFromMemory(&m_context, playerPixels.data(), 64, 64);
        m_playerMaterial = m_descriptors.createMaterial(m_playerTexture);
        
        // Enemy texture - red/dark
        std::vector<uint8_t> enemyPixels(64 * 64 * 4);
        for (int i = 0; i < 64 * 64; i++) {
            float n = (rng() % 30) / 100.0f;
            enemyPixels[i*4+0] = static_cast<uint8_t>(180 + n * 40);  // R - red
            enemyPixels[i*4+1] = static_cast<uint8_t>(40 + n * 20);   // G - dark
            enemyPixels[i*4+2] = static_cast<uint8_t>(40 + n * 20);   // B - dark
            enemyPixels[i*4+3] = 255;
        }
        m_enemyTexture.loadFromMemory(&m_context, enemyPixels.data(), 64, 64);
        m_enemyMaterial = m_descriptors.createMaterial(m_enemyTexture);
        
        // NPC texture - blue/cyan for friendly
        std::vector<uint8_t> npcPixels(64 * 64 * 4);
        for (int i = 0; i < 64 * 64; i++) {
            float n = (rng() % 30) / 100.0f;
            npcPixels[i*4+0] = static_cast<uint8_t>(60 + n * 30);   // R - low
            npcPixels[i*4+1] = static_cast<uint8_t>(140 + n * 40);  // G - medium
            npcPixels[i*4+2] = static_cast<uint8_t>(200 + n * 40);  // B - high (blue)
            npcPixels[i*4+3] = 255;
        }
        m_npcTexture.loadFromMemory(&m_context, npcPixels.data(), 64, 64);
        m_npcMaterial = m_descriptors.createMaterial(m_npcTexture);
        
        // Set a default texture for skinned models
        m_descriptors.setSkinnedTexture(m_stoneTexture);
    }

    void createMeshes() {
        std::vector<Vertex> verts; std::vector<uint32_t> inds; m_meshes.resize(2);
        auto cube = createCube(1.0f);
        m_meshes[0].vertexOffset = static_cast<int32_t>(verts.size());
        verts.insert(verts.end(), cube.begin(), cube.end());
        auto cubeIdx = createBoxIndices(0);
        m_meshes[0].indexStart = static_cast<uint32_t>(inds.size());
        inds.insert(inds.end(), cubeIdx.begin(), cubeIdx.end());
        m_meshes[0].indexCount = static_cast<uint32_t>(cubeIdx.size());
        
        auto player = createPlayerMesh(0.6f, 1.8f);
        m_meshes[1].vertexOffset = static_cast<int32_t>(verts.size());
        verts.insert(verts.end(), player.begin(), player.end());
        auto playerIdx = createBoxIndices(0);
        m_meshes[1].indexStart = static_cast<uint32_t>(inds.size());
        inds.insert(inds.end(), playerIdx.begin(), playerIdx.end());
        m_meshes[1].indexCount = static_cast<uint32_t>(playerIdx.size());
        
        VulkanBuffer::createWithStaging(&m_context, m_staticVB, verts.data(), sizeof(Vertex)*verts.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_staticIB, inds.data(), sizeof(uint32_t)*inds.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    void createEntities() {
        float startHeight = m_chunks.getHeightAt(0, 0) + 1.0f;
        Entity player = m_world.createPlayer(glm::vec3(0, startHeight, 0));
        auto& r = m_world.renderables.get(player);
        r.indexStart = m_meshes[1].indexStart;
        r.indexCount = m_meshes[1].indexCount;
        r.vertexOffset = m_meshes[1].vertexOffset;
        
        // Add stats to player
        m_world.stats.add(player, Stats{});
        
        // Add inventory with starting items
        Inventory inv;
        inv.addItem(ItemId::HealthPotion, 3);
        inv.addItem(ItemId::Bread, 5);
        inv.addItem(ItemId::WoodenSword, 1);
        m_world.inventories.add(player, inv);
        
        // Add combat ability to player
        Combat playerCombat;
        playerCombat.damage = 25.0f;
        playerCombat.attackRange = 2.5f;
        playerCombat.attackCooldown = 0.5f;
        m_world.combats.add(player, playerCombat);
        
        m_world.createCamera(player);
        
        // Place landmarks on terrain
        for (int x = -80; x <= 80; x += 40) {
            for (int z = -80; z <= 80; z += 40) {
                if (x == 0 && z == 0) continue;
                float terrainH = m_chunks.getHeightAt(static_cast<float>(x), static_cast<float>(z));
                float h = 2.0f + (rand() % 100) / 50.0f;
                Entity e = m_world.createLandmark(
                    glm::vec3(static_cast<float>(x), terrainH + h/2, static_cast<float>(z)),
                    glm::vec3(2.0f, h, 2.0f),
                    static_cast<float>(rand() % 360)
                );
                auto& lr = m_world.renderables.get(e);
                lr.indexStart = m_meshes[0].indexStart;
                lr.indexCount = m_meshes[0].indexCount;
                lr.vertexOffset = m_meshes[0].vertexOffset;
            }
        }
    }

    void rebuildTerrain() {
        std::vector<Vertex> v; std::vector<uint32_t> i;
        m_chunks.buildMesh(v, i);
        m_terrainIndexCount = static_cast<uint32_t>(i.size());
        if (m_terrainIndexCount == 0) return;
        m_terrainVB.destroy(); m_terrainIB.destroy();
        VulkanBuffer::createWithStaging(&m_context, m_terrainVB, v.data(), sizeof(Vertex)*v.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_terrainIB, i.data(), sizeof(uint32_t)*i.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_chunks.clearDirty();
    }

    void createSyncObjects() {
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = m_context.commandPool();
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        vkAllocateCommandBuffers(m_context.device(), &ai, m_commandBuffers.data());
        
        m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinished.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(m_context.device(), &si, nullptr, &m_imageAvailable[i]);
            vkCreateSemaphore(m_context.device(), &si, nullptr, &m_renderFinished[i]);
            vkCreateFence(m_context.device(), &fi, nullptr, &m_inFlight[i]);
        }
    }

    void saveGame() {
        SaveData data; data.playTime = m_totalPlayTime;
        if (m_world.playerEntity != NULL_ENTITY) {
            const auto& t = m_world.transforms.get(m_world.playerEntity);
            data.playerPosition = t.position; data.playerYaw = t.rotation.y;
        }
        if (m_world.cameraEntity != NULL_ENTITY) {
            const auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity);
            if (cam) { data.cameraYaw = cam->yaw; data.cameraPitch = cam->pitch; data.cameraDistance = cam->distance; }
        }
        auto rc = m_regions.currentRegion(); const auto& rd = m_regions.getCurrentRegionData();
        data.regions.push_back({rc.x, rc.z, static_cast<int>(rd.state), rd.realityPressure});
        if (SaveManager::save(data)) Logger::info("*** SAVED ***");
    }
    
    void loadGame() {
        SaveData data;
        if (!SaveManager::load(data)) { Logger::error("Load failed!"); return; }
        m_totalPlayTime = data.playTime;
        if (m_world.playerEntity != NULL_ENTITY) {
            auto& t = m_world.transforms.get(m_world.playerEntity);
            t.position = data.playerPosition; t.rotation.y = data.playerYaw;
            if (auto* c = m_world.playerControllers.tryGet(m_world.playerEntity)) c->targetYaw = data.playerYaw;
            if (auto* v = m_world.velocities.tryGet(m_world.playerEntity)) v->linear = glm::vec3(0);
        }
        if (m_world.cameraEntity != NULL_ENTITY) {
            if (auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity)) {
                cam->yaw = data.cameraYaw; cam->pitch = data.cameraPitch; cam->distance = data.cameraDistance;
            }
        }
        for (const auto& rs : data.regions) {
            auto& region = m_regions.getOrCreateRegion({rs.x, rs.z});
            region.state = static_cast<RegionState>(rs.state);
            region.realityPressure = rs.pressure;
        }
        m_currentVisuals = m_regions.getCurrentVisuals();
        m_lastLoggedState = m_regions.getCurrentRegionData().state;
        if (m_world.playerEntity != NULL_ENTITY) {
            m_chunks.update(m_world.transforms.get(m_world.playerEntity).position);
            m_chunks.forceRebuild();
            vkDeviceWaitIdle(m_context.device());
            rebuildTerrain();
        }
        Logger::info("*** LOADED ***");
    }

    void spawnEnemies() {
        struct EnemySpawn { float x, z; float hp; float dmg; };
        std::vector<EnemySpawn> spawns = {
            {20, 20, 50, 10},
            {-25, 15, 50, 10},
            {30, -20, 75, 15},
            {-20, -25, 75, 15},
            {40, 40, 100, 20},
            {-40, 40, 100, 20},
        };
        for (const auto& spawn : spawns) {
            spawnEnemy(spawn.x, spawn.z, spawn.hp, spawn.dmg);
        }
        Logger::infof("Spawned {} enemies", spawns.size());
        Logger::infof("Enemy entity list size: {}", m_enemyEntities.size());
    }

    void spawnNPCs() {
        struct NPCSpawn { float x, z; int type; };
        std::vector<NPCSpawn> spawns = {
            {5, -10, 0},   // Wanderer
            {-15, -15, 1}, // Merchant
            {25, 5, 2},    // Sage
            {-10, 25, 3},  // Guard
            {0, 35, 4},    // Mystic
        };
        
        for (const auto& spawn : spawns) {
            spawnNPC(spawn.x, spawn.z, spawn.type);
        }
        Logger::infof("Spawned {} NPCs", spawns.size());
    }
    
    Entity spawnNPC(float x, float z, int type) {
        float h = m_chunks.getHeightAt(x, z) + 1.0f;
        Entity e = m_world.createEntity(glm::vec3(x, h, z), {0,0,0}, {0.8f, 1.8f, 0.8f});
        
        NPC npc;
        switch (type) {
            case 0: npc = NPCTemplates::createWanderer(); break;
            case 1: npc = NPCTemplates::createMerchant(); break;
            case 2: npc = NPCTemplates::createSage(); break;
            case 3: npc = NPCTemplates::createGuard(); break;
            case 4: npc = NPCTemplates::createMystic(); break;
            default: npc = NPCTemplates::createWanderer(); break;
        }
        
        m_world.npcs.add(e, npc);
        m_world.npcTags.add(e, NPCTag{});
        
        Renderable r;
        r.meshId = 1; // Player mesh
        r.indexStart = m_meshes[1].indexStart;
        r.indexCount = m_meshes[1].indexCount;
        r.vertexOffset = m_meshes[1].vertexOffset;
        m_world.renderables.add(e, r);
        
        m_npcEntities.push_back(e);
        return e;
    }
    
    Entity spawnEnemy(float x, float z, float hp, float dmg) {
        float h = m_chunks.getHeightAt(x, z) + 1.0f;
        Entity e = m_world.createEntity(glm::vec3(x, h, z), {0,0,0}, {1.2f, 1.8f, 1.2f});
        
        Health health; health.current = hp; health.max = hp;
        m_world.healths.add(e, health);
        
        Enemy enemy;
        enemy.homePosition = glm::vec3(x, h, z);
        enemy.patrolTarget = enemy.homePosition;
        enemy.damage = dmg;
        m_world.enemies.add(e, enemy);
        m_world.enemyTags.add(e, EnemyTag{});
        
        m_world.velocities.add(e, Velocity{});
        
        Renderable r;
        r.meshId = static_cast<uint32_t>(MeshId::Cube);
        r.indexStart = m_meshes[0].indexStart;
        r.indexCount = m_meshes[0].indexCount;
        r.vertexOffset = m_meshes[0].vertexOffset;
        m_world.renderables.add(e, r);
        m_enemyEntities.push_back(e);
        return e;
    }
    
    void drawUIQuad(glm::vec2 pos, glm::vec2 size, glm::vec4 color, float screenW, float screenH) {
        float x1 = (pos.x / screenW) * 2.0f - 1.0f;
        float y1 = (pos.y / screenH) * 2.0f - 1.0f;
        float x2 = ((pos.x + size.x) / screenW) * 2.0f - 1.0f;
        float y2 = ((pos.y + size.y) / screenH) * 2.0f - 1.0f;
        
        m_uiVertices.push_back({{x1, y1}, {0, 0}, color});
        m_uiVertices.push_back({{x2, y1}, {1, 0}, color});
        m_uiVertices.push_back({{x2, y2}, {1, 1}, color});
        m_uiVertices.push_back({{x1, y1}, {0, 0}, color});
        m_uiVertices.push_back({{x2, y2}, {1, 1}, color});
        m_uiVertices.push_back({{x1, y2}, {0, 1}, color});
    }
    
    void drawUIBar(glm::vec2 pos, glm::vec2 size, float fill, glm::vec4 fillColor, glm::vec4 bgColor, float screenW, float screenH) {
        // Background
        drawUIQuad(pos, size, bgColor, screenW, screenH);
        // Fill
        if (fill > 0) {
            drawUIQuad(pos, {size.x * fill, size.y}, fillColor, screenW, screenH);
        }
        // Border
        glm::vec4 borderColor = {0.2f, 0.2f, 0.2f, 1.0f};
        float bw = 2.0f;
        drawUIQuad({pos.x - bw, pos.y - bw}, {size.x + bw*2, bw}, borderColor, screenW, screenH);
        drawUIQuad({pos.x - bw, pos.y + size.y}, {size.x + bw*2, bw}, borderColor, screenW, screenH);
        drawUIQuad({pos.x - bw, pos.y}, {bw, size.y}, borderColor, screenW, screenH);
        drawUIQuad({pos.x + size.x, pos.y}, {bw, size.y}, borderColor, screenW, screenH);
    }
    
    void renderUI(VkCommandBuffer cmd, uint32_t screenW, uint32_t screenH) {
        m_uiVertices.clear();
        float w = static_cast<float>(screenW);
        float h = static_cast<float>(screenH);
        
        // Health bar (red)
        float barW = 200.0f, barH = 20.0f;
        float margin = 20.0f;
        drawUIBar({margin, margin}, {barW, barH}, m_hud.healthPercent, 
                  {0.8f, 0.2f, 0.2f, 1.0f}, {0.3f, 0.1f, 0.1f, 0.8f}, w, h);
        
        // Stamina bar (green/yellow)
        glm::vec4 stamColor = m_hud.isExhausted ? glm::vec4(0.8f, 0.6f, 0.2f, 1.0f) : glm::vec4(0.2f, 0.8f, 0.3f, 1.0f);
        drawUIBar({margin, margin + barH + 5}, {barW, barH}, m_hud.staminaPercent,
                  stamColor, {0.1f, 0.2f, 0.1f, 0.8f}, w, h);
        
        // Mana bar (blue)
        drawUIBar({margin, margin + (barH + 5) * 2}, {barW, barH}, m_hud.manaPercent,
                  {0.2f, 0.3f, 0.9f, 1.0f}, {0.1f, 0.1f, 0.3f, 0.8f}, w, h);
        
        // Crosshair
        if (m_hud.showCrosshair && !m_hud.isDead) {
            float cx = w / 2.0f;
            float cy = h / 2.0f;
            float cSize = 12.0f;
            float cThick = 2.0f;
            glm::vec4 cColor = {1, 1, 1, 0.7f};
            drawUIQuad({cx - cSize, cy - cThick/2}, {cSize * 2, cThick}, cColor, w, h);
            drawUIQuad({cx - cThick/2, cy - cSize}, {cThick, cSize * 2}, cColor, w, h);
        }
        
        // Interact prompt
        if (m_hud.showInteractPrompt && !m_hud.isDead) {
            float promptW = 220.0f;
            float promptH = 35.0f;
            float px = (w - promptW) / 2.0f;
            float py = h / 2.0f - 80.0f;
            drawUIQuad({px, py}, {promptW, promptH}, {0, 0, 0, 0.7f}, w, h);
            drawUIQuad({px, py - 2}, {promptW, 2}, {0.4f, 0.6f, 0.9f, 1.0f}, w, h);
            drawUIQuad({px, py + promptH}, {promptW, 2}, {0.4f, 0.6f, 0.9f, 1.0f}, w, h);
        }
        
        // Dialogue box
        if (m_hud.showDialogue) {
            float boxH = 100.0f;
            float boxMargin = 30.0f;
            float boxW = w - boxMargin * 2;
            float by = h - boxH - boxMargin;
            
            // Background
            drawUIQuad({boxMargin, by}, {boxW, boxH}, {0, 0, 0, 0.85f}, w, h);
            
            // Border
            glm::vec4 bCol = {0.5f, 0.4f, 0.3f, 1.0f};
            drawUIQuad({boxMargin, by - 3}, {boxW, 3}, bCol, w, h);
            drawUIQuad({boxMargin, by + boxH}, {boxW, 3}, bCol, w, h);
            drawUIQuad({boxMargin - 3, by}, {3, boxH}, bCol, w, h);
            drawUIQuad({boxMargin + boxW, by}, {3, boxH}, bCol, w, h);
            
            // Speaker name box
            drawUIQuad({boxMargin + 10, by + 8}, {140, 24}, {0.3f, 0.4f, 0.6f, 0.9f}, w, h);
        }
        
        // Damage flash
        if (m_hud.showDamageFlash) {
            float intensity = m_hud.damageFlashTimer / 0.3f;
            glm::vec4 flashColor = {0.7f, 0.1f, 0.1f, intensity * 0.35f};
            float edge = 80.0f;
            drawUIQuad({0, 0}, {w, edge}, flashColor, w, h);
            drawUIQuad({0, h - edge}, {w, edge}, flashColor, w, h);
            drawUIQuad({0, 0}, {edge, h}, flashColor, w, h);
            drawUIQuad({w - edge, 0}, {edge, h}, flashColor, w, h);
        }
        
        // Death overlay
        if (m_hud.isDead) {
            drawUIQuad({0, 0}, {w, h}, {0, 0, 0, 0.75f}, w, h);
            float boxW2 = 350.0f, boxH2 = 80.0f;
            float bx = (w - boxW2) / 2.0f;
            float by2 = (h - boxH2) / 2.0f;
            drawUIQuad({bx, by2}, {boxW2, boxH2}, {0.6f, 0.1f, 0.1f, 0.95f}, w, h);
            drawUIQuad({bx, by2 - 3}, {boxW2, 3}, {0.9f, 0.2f, 0.2f, 1.0f}, w, h);
            drawUIQuad({bx, by2 + boxH2}, {boxW2, 3}, {0.9f, 0.2f, 0.2f, 1.0f}, w, h);
        }
        
        // Upload and draw
        if (!m_uiVertices.empty()) {
            m_uiVB.copyData(m_uiVertices.data(), m_uiVertices.size() * sizeof(UIVertex));
            
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline.pipeline());
            VkBuffer uiBuffers[] = {m_uiVB.buffer()};
            VkDeviceSize uiOffsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, uiBuffers, uiOffsets);
            vkCmdDraw(cmd, static_cast<uint32_t>(m_uiVertices.size()), 1, 0, 0);
        }
    }
    
    void updateHUD(float dt) {
        // Update player stats display
        if (auto* stats = m_world.stats.tryGet(m_world.playerEntity)) {
            m_hud.healthPercent = stats->health / stats->maxHealth;
            m_hud.staminaPercent = stats->stamina / stats->maxStamina;
            m_hud.manaPercent = stats->mana / stats->maxMana;
            m_hud.isExhausted = stats->isExhausted;
            m_hud.isDead = stats->isDead;
        }
        
        // Update dialogue display
        m_hud.showDialogue = m_dialogue.isInDialogue;
        m_hud.dialogueSpeaker = m_dialogue.currentSpeaker;
        m_hud.dialogueText = m_dialogue.currentText;
        m_hud.dialogueTimer = m_dialogue.displayTimer;
        
        // Check for nearby NPCs for interact prompt
        m_hud.showInteractPrompt = false;
        if (auto* playerT = m_world.transforms.tryGet(m_world.playerEntity)) {
            glm::vec3 playerPos = playerT->position;
            for (Entity e : m_npcEntities) {
                auto* t = m_world.transforms.tryGet(e);
                auto* npc = m_world.npcs.tryGet(e);
                if (t && npc) {
                    float dist = glm::length(t->position - playerPos);
                    if (dist < npc->interactRadius) {
                        m_hud.showInteractPrompt = true;
                        m_hud.nearbyNPCName = npc->name;
                        m_hud.interactText = "Press F to talk to " + npc->name;
                        break;
                    }
                }
            }
        }
        
        // Update damage flash
        if (m_hud.damageFlashTimer > 0) {
            m_hud.damageFlashTimer -= dt;
            m_hud.showDamageFlash = true;
        } else {
            m_hud.showDamageFlash = false;
        }
    }
    
    void updateNPCs(float dt) {
        for (Entity e : m_npcEntities) {
            auto* npc = m_world.npcs.tryGet(e);
            auto* transform = m_world.transforms.tryGet(e);
            if (!npc || !transform) continue;
            
            npc->update(dt);
            
            // Face player if nearby
            auto* playerT = m_world.transforms.tryGet(m_world.playerEntity);
            if (playerT) {
                float dist = glm::length(playerT->position - transform->position);
                if (dist < npc->interactRadius * 1.5f) {
                    glm::vec3 toPlayer = playerT->position - transform->position;
                    float targetYaw = glm::degrees(atan2(toPlayer.x, toPlayer.z));
                    // Smooth rotation
                    float diff = targetYaw - transform->rotation.y;
                    while (diff > 180.0f) diff -= 360.0f;
                    while (diff < -180.0f) diff += 360.0f;
                    transform->rotation.y += diff * dt * 3.0f;
                }
            }
            
            // Apply idle bob
            float baseHeight = m_chunks.getHeightAt(transform->position.x, transform->position.z) + 0.9f;
            transform->position.y = baseHeight + npc->idleBobOffset;
        }
    }

    void updateEnemyAI(float dt) {
        if (m_world.playerEntity == NULL_ENTITY) return;
        const auto& playerPos = m_world.transforms.get(m_world.playerEntity).position;
        auto* playerStats = m_world.stats.tryGet(m_world.playerEntity);
        
        std::vector<Entity> deadEnemies;
        
        m_world.enemies.each([&](Entity e, Enemy& enemy) {
            auto* health = m_world.healths.tryGet(e);
            auto* transform = m_world.transforms.tryGet(e);
            auto* velocity = m_world.velocities.tryGet(e);
            if (!health || !transform || !velocity) return;
            
            enemy.update(dt);
            
            // Check if dead
            if (health->isDead) {
                enemy.state = AIState::Dead;
                deadEnemies.push_back(e);
                return;
            }
            
            float distToPlayer = glm::length(playerPos - transform->position);
            glm::vec3 dirToPlayer = distToPlayer > 0.1f ? 
                glm::normalize(playerPos - transform->position) : glm::vec3(0);
            
            // State machine
            switch (enemy.state) {
                case AIState::Idle:
                    velocity->linear = glm::vec3(0);
                    if (distToPlayer < enemy.aggroRange) {
                        enemy.state = AIState::Chase;
                    } else if (enemy.patrolWaitTimer <= 0) {
                        // Pick new patrol point
                        float angle = static_cast<float>(rand()) / RAND_MAX * 6.28f;
                        float dist = static_cast<float>(rand()) / RAND_MAX * enemy.patrolRadius;
                        enemy.patrolTarget = enemy.homePosition + glm::vec3(cos(angle) * dist, 0, sin(angle) * dist);
                        enemy.state = AIState::Patrol;
                    }
                    break;
                    
                case AIState::Patrol: {
                    glm::vec3 toTarget = enemy.patrolTarget - transform->position;
                    toTarget.y = 0;
                    float dist = glm::length(toTarget);
                    if (dist < 1.0f) {
                        enemy.state = AIState::Idle;
                        enemy.patrolWaitTimer = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f;
                    } else {
                        glm::vec3 dir = glm::normalize(toTarget);
                        velocity->linear.x = dir.x * enemy.moveSpeed * 0.5f;
                        velocity->linear.z = dir.z * enemy.moveSpeed * 0.5f;
                    }
                    if (distToPlayer < enemy.aggroRange) {
                        enemy.state = AIState::Chase;
                    }
                    break;
                }
                    
                case AIState::Chase:
                    if (distToPlayer > enemy.aggroRange * 1.5f) {
                        enemy.state = AIState::Idle;
                        velocity->linear = glm::vec3(0);
                    } else if (distToPlayer < enemy.attackRange) {
                        enemy.state = AIState::Attack;
                        velocity->linear = glm::vec3(0);
                    } else {
                        velocity->linear.x = dirToPlayer.x * enemy.moveSpeed;
                        velocity->linear.z = dirToPlayer.z * enemy.moveSpeed;
                        // Face player
                        transform->rotation.y = glm::degrees(atan2(dirToPlayer.x, dirToPlayer.z));
                    }
                    break;
                    
                case AIState::Attack:
                    velocity->linear = glm::vec3(0);
                    if (distToPlayer > enemy.attackRange * 1.2f) {
                        enemy.state = AIState::Chase;
                    } else if (enemy.canAttack() && playerStats && !playerStats->isDead) {
                        // Attack player
                        playerStats->takeDamage(enemy.damage);
                        enemy.cooldownTimer = enemy.attackCooldown;
                        Logger::infof("Enemy hit you for {:.0f} damage! HP: {:.0f}/{:.0f}", 
                            enemy.damage, playerStats->health, playerStats->maxHealth);
                        m_hud.damageFlashTimer = 0.3f;
                    }
                    break;
                    
                case AIState::Dead:
                    break;
            }
            
            // Apply movement to terrain
            transform->position.x += velocity->linear.x * dt;
            transform->position.z += velocity->linear.z * dt;
            float groundH = m_chunks.getHeightAt(transform->position.x, transform->position.z);
            transform->position.y = groundH + 0.9f;
        });
        
        // Respawn dead enemies after delay
        for (Entity e : deadEnemies) {
            auto* enemy = m_world.enemies.tryGet(e);
            if (enemy) {
                // Respawn at home position
                Logger::info("Enemy defeated! +10 XP");
                float x = enemy->homePosition.x;
                float z = enemy->homePosition.z;
                auto* health = m_world.healths.tryGet(e);
                float maxHp = health ? health->max : 50.0f;
                float dmg = enemy->damage;
                // Remove from enemy list
                m_enemyEntities.erase(std::remove(m_enemyEntities.begin(), m_enemyEntities.end(), e), m_enemyEntities.end());
                m_world.destroyEntity(e);
                // Delayed respawn - for now just respawn immediately offset
                spawnEnemy(x + 5.0f, z + 5.0f, maxHp, dmg);
            }
        }
    }
    
    void tryInteractWithNPC() {
        if (m_dialogue.isInDialogue) {
            m_dialogue.skipOrAdvance();
            return;
        }
        
        auto* playerT = m_world.transforms.tryGet(m_world.playerEntity);
        if (!playerT) return;
        
        glm::vec3 playerPos = playerT->position;
        Entity closestNPC = NULL_ENTITY;
        float closestDist = 999.0f;
        
        for (Entity e : m_npcEntities) {
            auto* t = m_world.transforms.tryGet(e);
            auto* npc = m_world.npcs.tryGet(e);
            if (!t || !npc) continue;
            
            float dist = glm::length(t->position - playerPos);
            if (dist < npc->interactRadius && dist < closestDist) {
                closestDist = dist;
                closestNPC = e;
            }
        }
        
        if (closestNPC != NULL_ENTITY) {
            auto* npc = m_world.npcs.tryGet(closestNPC);
            if (npc) {
                const DialogueLine* line = npc->getNextLine();
                if (line) {
                    m_dialogue.startDialogue(closestNPC, line);
                    Logger::info("[" + line->speaker + "]: " + line->text);
                }
            }
        }
    }

    void playerAttack() {
        if (m_world.playerEntity == NULL_ENTITY) return;
        auto* combat = m_world.combats.tryGet(m_world.playerEntity);
        auto* stats = m_world.stats.tryGet(m_world.playerEntity);
        if (!combat || !stats || stats->isDead) return;
        
        if (!combat->canAttack()) return;
        combat->startAttack();
        
        const auto& playerPos = m_world.transforms.get(m_world.playerEntity).position;
        const auto& playerRot = m_world.transforms.get(m_world.playerEntity).rotation;
        float yaw = glm::radians(playerRot.y);
        glm::vec3 attackDir = glm::vec3(sin(yaw), 0, cos(yaw));
        
        // Find enemies in range and in front of player
        m_world.enemies.each([&](Entity e, Enemy& enemy) {
            auto* health = m_world.healths.tryGet(e);
            auto* transform = m_world.transforms.tryGet(e);
            if (!health || !transform || health->isDead) return;
            
            glm::vec3 toEnemy = transform->position - playerPos;
            float dist = glm::length(toEnemy);
            
            if (dist < combat->attackRange) {
                // Check if roughly in front (dot product > 0)
                glm::vec3 dirToEnemy = dist > 0.1f ? glm::normalize(toEnemy) : glm::vec3(0);
                float dot = glm::dot(attackDir, glm::vec3(dirToEnemy.x, 0, dirToEnemy.z));
                
                if (dot > 0.3f || dist < 1.5f) {  // In front or very close
                    health->takeDamage(combat->damage);
                    enemy.hitFlashTimer = 0.2f;
                    Logger::infof("Hit enemy for {:.0f} damage! Enemy HP: {:.0f}/{:.0f}",
                        combat->damage, health->current, health->max);
                }
            }
        });
    }
    void spawnPickups() {
        struct PickupSpawn { float x, z; ItemId item; uint8_t count; };
        std::vector<PickupSpawn> spawns = {
            {5, 5, ItemId::HealthPotion, 2},
            {-8, 3, ItemId::Apple, 3},
            {10, -5, ItemId::Stone, 10},
            {-5, -10, ItemId::MythicShard, 1},
            {15, 10, ItemId::IronOre, 5},
            {-12, 8, ItemId::Crystal, 2},
            {8, -12, ItemId::StaminaPotion, 1},
            {-15, -5, ItemId::Wood, 8},
        };
        for (const auto& spawn : spawns) {
            float h = m_chunks.getHeightAt(spawn.x, spawn.z) + 0.5f;
            Entity e = m_world.createEntity(glm::vec3(spawn.x, h, spawn.z), {0,0,0}, {0.4f, 0.4f, 0.4f});
            Pickup pickup;
            pickup.itemId = spawn.item;
            pickup.amount = spawn.count;
            m_world.pickups.add(e, pickup);
            Renderable r;
            r.meshId = static_cast<uint32_t>(MeshId::Cube);
            r.indexStart = m_meshes[0].indexStart;
            r.indexCount = m_meshes[0].indexCount;
            r.vertexOffset = m_meshes[0].vertexOffset;
            m_world.renderables.add(e, r);
        }
        Logger::infof("Spawned {} pickups", spawns.size());
    }
    
    void collectPickups() {
        if (m_world.playerEntity == NULL_ENTITY) return;
        const auto& playerPos = m_world.transforms.get(m_world.playerEntity).position;
        auto* inv = m_world.inventories.tryGet(m_world.playerEntity);
        if (!inv) return;
        std::vector<Entity> toDestroy;
        m_world.pickups.each([&](Entity e, Pickup& pickup) {
            if (pickup.collected) return;
            const auto* t = m_world.transforms.tryGet(e);
            if (!t) return;
            float dist = glm::length(playerPos - t->position);
            if (dist < pickup.pickupRadius) {
                uint8_t remaining = inv->addItem(pickup.itemId, pickup.amount);
                if (remaining < pickup.amount) {
                    const auto& def = ItemDatabase::get(pickup.itemId);
                    Logger::infof("Picked up: {} x{}", def.name, pickup.amount - remaining);
                    if (remaining == 0) { pickup.collected = true; toDestroy.push_back(e); }
                    else { pickup.amount = remaining; }
                }
            }
        });
        for (Entity e : toDestroy) m_world.destroyEntity(e);
    }
    
    void useSelectedItem() {
        if (m_world.playerEntity == NULL_ENTITY) return;
        auto* inv = m_world.inventories.tryGet(m_world.playerEntity);
        auto* stats = m_world.stats.tryGet(m_world.playerEntity);
        if (!inv || !stats) return;
        auto& slot = inv->selectedItem();
        if (slot.isEmpty()) return;
        const auto& def = ItemDatabase::get(slot.id);
        if (def.type == ItemType::Consumable) {
            bool used = false;
            if (slot.id == ItemId::HealthPotion || slot.id == ItemId::Bread || slot.id == ItemId::Apple) {
                if (stats->health < stats->maxHealth) { stats->heal(def.value); used = true;
                    Logger::infof("Used {} - HP: {:.0f}/{:.0f}", def.name, stats->health, stats->maxHealth); }
            } else if (slot.id == ItemId::StaminaPotion) {
                if (stats->stamina < stats->maxStamina) { stats->stamina = (std::min)(stats->maxStamina, stats->stamina + def.value); used = true;
                    Logger::infof("Used {} - STA: {:.0f}/{:.0f}", def.name, stats->stamina, stats->maxStamina); }
            } else if (slot.id == ItemId::ManaPotion) {
                if (stats->mana < stats->maxMana) { stats->mana = (std::min)(stats->maxMana, stats->mana + def.value); used = true;
                    Logger::infof("Used {} - MP: {:.0f}/{:.0f}", def.name, stats->mana, stats->maxMana); }
            }
            if (used) { slot.count--; if (slot.count == 0) slot.clear(); }
        }
    }
    
    void dropSelectedItem() {
        if (m_world.playerEntity == NULL_ENTITY) return;
        auto* inv = m_world.inventories.tryGet(m_world.playerEntity);
        if (!inv) return;
        auto& slot = inv->selectedItem();
        if (slot.isEmpty()) return;
        const auto& playerT = m_world.transforms.get(m_world.playerEntity);
        float yaw = glm::radians(playerT.rotation.y);
        glm::vec3 dropPos = playerT.position + glm::vec3(sin(yaw), 0, cos(yaw)) * 2.0f;
        dropPos.y = m_chunks.getHeightAt(dropPos.x, dropPos.z) + 0.5f;
        Entity e = m_world.createEntity(dropPos, {0,0,0}, {0.4f, 0.4f, 0.4f});
        Pickup pickup; pickup.itemId = slot.id; pickup.amount = slot.count;
        m_world.pickups.add(e, pickup);
        Renderable r; r.meshId = static_cast<uint32_t>(MeshId::Cube);
        r.indexStart = m_meshes[0].indexStart; r.indexCount = m_meshes[0].indexCount; r.vertexOffset = m_meshes[0].vertexOffset;
        m_world.renderables.add(e, r);
        const auto& def = ItemDatabase::get(slot.id);
        Logger::infof("Dropped: {} x{}", def.name, slot.count);
        slot.clear();
    }
    
    void showInventory() {
        if (m_world.playerEntity == NULL_ENTITY) return;
        auto* inv = m_world.inventories.tryGet(m_world.playerEntity);
        if (!inv) return;
        Logger::info("=== INVENTORY ===");
        auto items = inv->getNonEmptySlots();
        if (items.empty()) { Logger::info("  (empty)"); }
        else { for (const auto& [idx, stack] : items) {
            const auto& def = ItemDatabase::get(stack->id);
            std::string sel = (idx == inv->selectedSlot()) ? " <--" : "";
            Logger::infof("  [{}] {} x{}{}", idx, def.name, stack->count, sel);
        } }
        Logger::infof("Slots: {}/{} | Selected: {}", inv->usedSlots(), Inventory::MAX_SLOTS, inv->selectedSlot());
    }
    void updateAnimations(float dt) {
        static bool loggedOnce = false;
        for (auto& inst : m_modelInstances) {
            Model* model = m_modelManager.getModel(inst.modelId);
            if (model && model->hasSkeleton && !model->animations.empty()) {
                inst.updateAnimation(dt, *model);
                if (!loggedOnce && !inst.jointMatrices.empty()) {
                    Logger::info("Animation active: " + std::to_string(inst.jointMatrices.size()) + " joints, time=" + std::to_string(inst.animState.currentTime) + " playing=" + std::string(inst.animState.playing ? "true" : "false") + " clip=" + std::to_string(inst.animState.clipIndex));
                    loggedOnce = true;
                }
            }
        }
    }


    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            m_timer.tick();
            float dt = m_timer.clampedDeltaTime();
            m_totalPlayTime += dt;
            
            processInput(dt);
            
            auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity);
            updatePlayerInput(m_world, dt, m_mouseCaptured,
                Input::instance().mouseDeltaX(), Input::instance().mouseDeltaY(), cam);
            updateMovement(m_world, dt);
            
            // Get terrain height for player and update ground collision
            if (m_world.playerEntity != NULL_ENTITY) {
                auto& pt = m_world.transforms.get(m_world.playerEntity);
                float groundHeight = m_chunks.getHeightAt(pt.position.x, pt.position.z);
                auto* vel = m_world.velocities.tryGet(m_world.playerEntity);
                auto* ctrl = m_world.playerControllers.tryGet(m_world.playerEntity);
                
                // Ground collision with terrain
                if (pt.position.y <= groundHeight) {
                    pt.position.y = groundHeight;
                    if (vel) vel->linear.y = 0.0f;
                    if (ctrl) ctrl->isGrounded = true;
                } else if (pt.position.y > groundHeight + 0.1f) {
                    // In the air
                    if (ctrl) ctrl->isGrounded = false;
                }
            }
            
            updateCamera(m_world, dt, m_mouseCaptured,
                Input::instance().mouseDeltaX(), Input::instance().mouseDeltaY(), m_scrollDelta);
            
            m_scrollDelta = 0.0f;
            
            collectPickups();
            
            // Update combat cooldowns
            if (m_world.playerEntity != NULL_ENTITY) {
                if (auto* combat = m_world.combats.tryGet(m_world.playerEntity)) {
                    combat->update(dt);
                }
            }
            
            // Update enemy AI
            updateEnemyAI(dt);
            updateNPCs(dt);
            m_dialogue.update(dt);
            updateHUD(dt);
            
            Input::instance().update();
            
            if (m_world.playerEntity != NULL_ENTITY) {
                const auto& pt = m_world.transforms.get(m_world.playerEntity);
                m_regions.update(pt.position, dt);
                
                RegionVisuals target = m_regions.getCurrentVisuals();
                float visualLerp = 1.0f - exp(-2.0f * dt);
                m_currentVisuals.fogColor = glm::mix(m_currentVisuals.fogColor, target.fogColor, visualLerp);
                m_currentVisuals.skyColor = glm::mix(m_currentVisuals.skyColor, target.skyColor, visualLerp);
                
                m_chunks.update(pt.position);
                if (m_chunks.isDirty()) {
                    vkDeviceWaitIdle(m_context.device());
                    rebuildTerrain();
                }
            }
            updateAnimations(dt);
            drawFrame();
            
            // Update player stats
            if (m_world.playerEntity != NULL_ENTITY) {
                if (auto* playerStats = m_world.stats.tryGet(m_world.playerEntity)) {
                    playerStats->update(dt);
                }
            }
            
            m_logTimer += dt;
            if (m_logTimer >= 3.0f) {
                if (m_world.playerEntity != NULL_ENTITY) {
                    const auto& pt = m_world.transforms.get(m_world.playerEntity);
                    const auto& rd = m_regions.getCurrentRegionData();
                    const auto* playerStats = m_world.stats.tryGet(m_world.playerEntity);
                    
                    if (rd.state != m_lastLoggedState) {
                        Logger::infof("*** REGION: {} -> {} ***", regionStateName(m_lastLoggedState), regionStateName(rd.state));
                        m_lastLoggedState = rd.state;
                    }
                    
                    if (playerStats) {
                        if (playerStats->isDead) {
                            Logger::info("*** YOU ARE DEAD - Press R to respawn ***");
                        } else {
                            Logger::infof("HP: {:.0f}/{:.0f} | STA: {:.0f}/{:.0f} | MP: {:.0f}/{:.0f}{}",
                                playerStats->health, playerStats->maxHealth,
                                playerStats->stamina, playerStats->maxStamina,
                                playerStats->mana, playerStats->maxMana,
                                playerStats->isExhausted ? " [EXHAUSTED]" : "");
                        }
                    }
                    
                    Logger::infof("FPS: {:.0f} | Pos: ({:.0f},{:.1f},{:.0f})",
                        m_timer.fps(), pt.position.x, pt.position.y, pt.position.z);
                }
                m_logTimer = 0.0f;
            }
        }
        vkDeviceWaitIdle(m_context.device());
    }

    void processInput(float dt) {
        auto& input = Input::instance();
        if (input.isKeyPressed(GLFW_KEY_ESCAPE)) { glfwSetWindowShouldClose(m_window, true); return; }
        if (input.isKeyPressed(GLFW_KEY_TAB)) {
            m_mouseCaptured = !m_mouseCaptured;
            glfwSetInputMode(m_window, GLFW_CURSOR, m_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
        // Combat - left click to attack
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            playerAttack();
        }
        
        // Inventory controls
        if (input.isKeyPressed(GLFW_KEY_I)) showInventory();
        if (input.isKeyPressed(GLFW_KEY_E)) useSelectedItem();
        if (input.isKeyPressed(GLFW_KEY_Q)) dropSelectedItem();
        if (input.isKeyPressed(GLFW_KEY_F)) tryInteractWithNPC();
        if (input.isKeyPressed(GLFW_KEY_1)) { if (auto* inv = m_world.inventories.tryGet(m_world.playerEntity)) inv->selectSlot(0); }
        if (input.isKeyPressed(GLFW_KEY_2)) { if (auto* inv = m_world.inventories.tryGet(m_world.playerEntity)) inv->selectSlot(1); }
        if (input.isKeyPressed(GLFW_KEY_3)) { if (auto* inv = m_world.inventories.tryGet(m_world.playerEntity)) inv->selectSlot(2); }
        if (input.isKeyPressed(GLFW_KEY_4)) { if (auto* inv = m_world.inventories.tryGet(m_world.playerEntity)) inv->selectSlot(3); }
        if (input.isKeyPressed(GLFW_KEY_5)) { if (auto* inv = m_world.inventories.tryGet(m_world.playerEntity)) inv->selectSlot(4); }
        
        if (input.isKeyPressed(GLFW_KEY_F5)) saveGame();
        if (input.isKeyPressed(GLFW_KEY_F9)) loadGame();
        
        // Debug: F1 = take damage, F2 = heal
        if (input.isKeyPressed(GLFW_KEY_F1)) {
            if (auto* s = m_world.stats.tryGet(m_world.playerEntity)) {
                s->takeDamage(25.0f);
                Logger::infof("Took 25 damage! HP: {:.0f}/{:.0f}", s->health, s->maxHealth);
            }
        }
        if (input.isKeyPressed(GLFW_KEY_F2)) {
            if (auto* s = m_world.stats.tryGet(m_world.playerEntity)) {
                s->heal(25.0f);
                Logger::infof("Healed 25! HP: {:.0f}/{:.0f}", s->health, s->maxHealth);
            }
        }
        // Respawn when dead
        if (input.isKeyPressed(GLFW_KEY_R)) {
            if (auto* s = m_world.stats.tryGet(m_world.playerEntity)) {
                if (s->isDead) {
                    s->respawn();
                    auto& pt = m_world.transforms.get(m_world.playerEntity);
                    pt.position = glm::vec3(0, m_chunks.getHeightAt(0, 0) + 1.0f, 0);
                    Logger::info("*** RESPAWNED ***");
                }
            }
        }
    }

    void drawFrame() {
        vkWaitForFences(m_context.device(), 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex;
        if (!m_swapchain.acquireNextImage(imageIndex, m_imageAvailable[m_currentFrame])) {
            recreateSwapchain(); return;
        }
        vkResetFences(m_context.device(), 1, &m_inFlight[m_currentFrame]);
        
        updateCameraUBO();
        recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);
        
        VkSemaphore waitSems[] = {m_imageAvailable[m_currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSems[] = {m_renderFinished[m_currentFrame]};
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitSems; si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = 1; si.pCommandBuffers = &m_commandBuffers[m_currentFrame];
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = signalSems;
        vkQueueSubmit(m_context.graphicsQueue(), 1, &si, m_inFlight[m_currentFrame]);
        
        if (!m_swapchain.present(imageIndex, m_renderFinished[m_currentFrame]) || m_framebufferResized) {
            m_framebufferResized = false;
            recreateSwapchain();
        }
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void updateCameraUBO() {
        auto ext = m_swapchain.extent();
        CameraUBO ubo{};
        ubo.view = getCameraViewMatrix(m_world);
        ubo.proj = glm::perspective(glm::radians(60.0f), float(ext.width)/float(ext.height), 0.1f, 500.0f);
        ubo.proj[1][1] *= -1;
        ubo.viewProj = ubo.proj * ubo.view;
        ubo.cameraPos = getCameraPosition(m_world);
        ubo.time = m_timer.totalTime();
        ubo.sunDirection = m_sunDirection;
        ubo.sunIntensity = m_sunIntensity;
        ubo.sunColor = m_sunColor;
        ubo.ambientIntensity = m_ambientIntensity;
        ubo.skyColorTop = m_skyColorTop;
        ubo.skyColorBottom = m_skyColorBottom;
        m_descriptors.updateCameraUBO(m_currentFrame, ubo);
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &bi);
        
        auto ext = m_swapchain.extent();
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        
        VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpi.renderPass = m_swapchain.renderPass();
        rpi.framebuffer = m_swapchain.framebuffer(imageIndex);
        rpi.renderArea = {{0,0}, ext};
        rpi.clearValueCount = 2; rpi.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        
        VkViewport vp{0,0,float(ext.width),float(ext.height),0,1};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0}, ext};
        vkCmdSetScissor(cmd, 0, 1, &sc);
        
        // Draw sky
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline.pipeline());
        VkDescriptorSet ds = m_descriptors.descriptorSet(m_currentFrame);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline.pipelineLayout(), 0, 1, &ds, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        
        // Draw lit geometry
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_litPipeline.pipeline());
        PushConstants push{};
        
        // Terrain
        if (m_terrainIndexCount > 0) {
            m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_groundMaterial);
            VkBuffer tb[] = {m_terrainVB.buffer()}; VkDeviceSize to[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, tb, to);
            vkCmdBindIndexBuffer(cmd, m_terrainIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
            push.model = glm::mat4(1.0f);
            vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, m_terrainIndexCount, 1, 0, 0, 0);
        }
        
        // Static meshes
        VkBuffer sb[] = {m_staticVB.buffer()}; VkDeviceSize so[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, sb, so);
        vkCmdBindIndexBuffer(cmd, m_staticIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
        
        // Landmarks
        m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_stoneMaterial);
        m_world.landmarkTags.each([&](Entity e, const LandmarkTag&) {
            const auto* t = m_world.transforms.tryGet(e);
            const auto* r = m_world.renderables.tryGet(e);
            if (!t || !r || !r->visible) return;
            push.model = t->getMatrix();
            vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, r->indexCount, 1, r->indexStart, r->vertexOffset, 0);
        });
        
        // Player
        m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_playerMaterial);
        if (m_world.playerEntity != NULL_ENTITY) {
            const auto* t = m_world.transforms.tryGet(m_world.playerEntity);
            const auto* r = m_world.renderables.tryGet(m_world.playerEntity);
            if (t && r && r->visible) {
                push.model = t->getMatrix();
                vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdDrawIndexed(cmd, r->indexCount, 1, r->indexStart, r->vertexOffset, 0);
            }
        }
        

        // Enemies - red humanoids
        m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_enemyMaterial);
        {
            VkBuffer evb[] = {m_staticVB.buffer()};
            VkDeviceSize eoff[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, evb, eoff);
            vkCmdBindIndexBuffer(cmd, m_staticIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
        }
        for (Entity e : m_enemyEntities) {
            const auto* t = m_world.transforms.tryGet(e);
            const auto* health = m_world.healths.tryGet(e);
            if (t && health && !health->isDead) {
                push.model = t->getMatrix();
                vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdDrawIndexed(cmd, m_meshes[1].indexCount, 1, m_meshes[1].indexStart, m_meshes[1].vertexOffset, 0);
            }
        }

        // NPCs - blue friendly humanoids
        m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_npcMaterial);
        for (Entity e : m_npcEntities) {
            const auto* t = m_world.transforms.tryGet(e);
            if (t) {
                push.model = t->getMatrix();
                vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdDrawIndexed(cmd, m_meshes[1].indexCount, 1, m_meshes[1].indexStart, m_meshes[1].vertexOffset, 0);
            }
        }
          // Render glTF model instances
          static bool loggedSkinned = false;
          for (auto& inst : m_modelInstances) {
              Model* model = m_modelManager.getModel(inst.modelId);
              if (!model || model->meshes.empty()) continue;
              
              // Bind model's vertex and index buffers
              VkBuffer modelBuffers[] = {model->vertexBuffer};
              VkDeviceSize modelOffsets[] = {0};
              vkCmdBindVertexBuffers(cmd, 0, 1, modelBuffers, modelOffsets);
              vkCmdBindIndexBuffer(cmd, model->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
              
              if (model->hasSkeleton && !inst.jointMatrices.empty()) {
                  if (!loggedSkinned) {
                      Logger::info("SKINNED RENDER: " + std::to_string(inst.jointMatrices.size()) + " joints");
                    if (!inst.jointMatrices.empty()) {
                        const auto& m = inst.jointMatrices[0];
                        Logger::info("Joint0: diag=[" + std::to_string(m[0][0]) + "," + std::to_string(m[1][1]) + "," + std::to_string(m[2][2]) + "] trans=[" + std::to_string(m[3][0]) + "," + std::to_string(m[3][1]) + "," + std::to_string(m[3][2]) + "]");
                    }
                      loggedSkinned = true;
                  }
                  // Use skinned pipeline for animated models
                  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skinnedPipeline.pipeline());
                  
                  // Upload joint matrices for this instance
                  m_descriptors.updateJointMatrices(m_currentFrame, inst.jointMatrices.data(), 
                      static_cast<uint32_t>(inst.jointMatrices.size()));
                  
                  // Bind skinned descriptor set
                  m_descriptors.bindSkinnedDescriptor(cmd, m_skinnedPipeline.pipelineLayout(), m_currentFrame);
                  
                  // Draw skinned meshes
                  for (const auto& mesh : model->meshes) {
                      push.model = inst.transform;
                      vkCmdPushConstants(cmd, m_skinnedPipeline.pipelineLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                      vkCmdDrawIndexed(cmd, mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                  }
                  
                  // Switch back to lit pipeline for next static model
                  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_litPipeline.pipeline());
              } else {
                  // Use standard lit pipeline for static models
                  m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_stoneMaterial);
                  
                  for (const auto& mesh : model->meshes) {
                      push.model = inst.transform;
                      vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                      vkCmdDrawIndexed(cmd, mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                  }
              }
          }
          // Re-bind static buffers for UI or subsequent draws
          VkBuffer staticBuffers[] = {m_staticVB.buffer()};
          VkDeviceSize staticOffsets[] = {0};
          vkCmdBindVertexBuffers(cmd, 0, 1, staticBuffers, staticOffsets);
          vkCmdBindIndexBuffer(cmd, m_staticIB.buffer(), 0, VK_INDEX_TYPE_UINT32);



        // Render UI overlay
        renderUI(cmd, m_swapchain.extent().width, m_swapchain.extent().height);
        
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    void recreateSwapchain() {
        int w=0, h=0;
        glfwGetFramebufferSize(m_window, &w, &h);
        while (w==0||h==0) { glfwGetFramebufferSize(m_window,&w,&h); glfwWaitEvents(); }
        vkDeviceWaitIdle(m_context.device());
        m_swapchain.recreate();
    }

    void cleanup() {
        m_modelManager.cleanup();
        m_groundTexture.destroy(); m_stoneTexture.destroy(); m_playerTexture.destroy();
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_context.device(), m_imageAvailable[i], nullptr);
            vkDestroySemaphore(m_context.device(), m_renderFinished[i], nullptr);
            vkDestroyFence(m_context.device(), m_inFlight[i], nullptr);
        }
        m_terrainIB.destroy(); m_terrainVB.destroy();
        m_staticIB.destroy(); m_staticVB.destroy();
        m_litPipeline.destroy(); m_skinnedPipeline.destroy(); m_skyPipeline.destroy();
        m_descriptors.destroy(); m_swapchain.destroy(); m_context.destroy();
        glfwDestroyWindow(m_window); glfwTerminate();
    }
};

int main() {
    try { Application app; app.run(); }
    catch (const std::exception& e) { Logger::fatal(e.what()); return 1; }
    return 0;
}






















































































