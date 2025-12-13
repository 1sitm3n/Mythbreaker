#include "engine/Logger.h"
#include "engine/Timer.h"
#include "engine/Input.h"
#include "engine/RegionState.h"
#include "engine/SaveLoad.h"
#include "engine/ecs/World.h"
#include "engine/ecs/Systems.h"
#include "engine/vulkan/VulkanContext.h"
#include "engine/vulkan/VulkanSwapchain.h"
#include "engine/vulkan/VulkanPipeline.h"
#include "engine/vulkan/VulkanBuffer.h"
#include "engine/vulkan/VulkanDescriptors.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

using namespace myth;
using namespace myth::vk;
using namespace myth::ecs;

// Chunk system
struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& other) const { return x == other.x && z == other.z; }
};
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 16);
    }
};

float chunkRandom(int x, int z, int seed = 0) {
    int n = x + z * 57 + seed * 131;
    n = (n << 13) ^ n;
    return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
}

struct Chunk {
    ChunkCoord coord;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    void generate(float chunkSize, float colorMult = 1.0f) {
        float baseHeight = chunkRandom(coord.x, coord.z, 0) * 0.3f;
        glm::vec3 color(
            0.12f + chunkRandom(coord.x, coord.z, 1) * 0.08f,
            0.10f + chunkRandom(coord.x, coord.z, 2) * 0.06f,
            0.08f + chunkRandom(coord.x, coord.z, 3) * 0.04f
        );
        color *= colorMult;
        
        float halfSize = chunkSize / 2.0f;
        float worldX = coord.x * chunkSize;
        float worldZ = coord.z * chunkSize;
        
        float h00 = baseHeight + chunkRandom(coord.x, coord.z, 10) * 0.1f;
        float h10 = baseHeight + chunkRandom(coord.x + 1, coord.z, 10) * 0.1f;
        float h01 = baseHeight + chunkRandom(coord.x, coord.z + 1, 10) * 0.1f;
        float h11 = baseHeight + chunkRandom(coord.x + 1, coord.z + 1, 10) * 0.1f;
        
        vertices = {
            {{worldX - halfSize, h00, worldZ - halfSize}, color, {0, 0}},
            {{worldX + halfSize, h10, worldZ - halfSize}, color, {1, 0}},
            {{worldX + halfSize, h11, worldZ + halfSize}, color, {1, 1}},
            {{worldX - halfSize, h01, worldZ + halfSize}, color, {0, 1}}
        };
        indices = {0, 1, 2, 0, 2, 3};
    }
};

class ChunkManager {
public:
    float chunkSize = 10.0f;
    int loadRadius = 5;
    
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
    size_t count() const { return m_chunks.size(); }
    
    void buildMesh(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        verts.clear(); inds.clear();
        for (auto& [coord, chunk] : m_chunks) {
            uint32_t base = static_cast<uint32_t>(verts.size());
            verts.insert(verts.end(), chunk.vertices.begin(), chunk.vertices.end());
            for (uint32_t idx : chunk.indices) inds.push_back(base + idx);
        }
    }
    
private:
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_chunks;
    bool m_dirty = false;
};

// Mesh helpers
std::vector<Vertex> createCube(float size) {
    float s = size / 2.0f;
    return {
        {{-s,-s, s},{0.8f,0.2f,0.2f},{0,0}},{{ s,-s, s},{0.8f,0.2f,0.2f},{1,0}},
        {{ s, s, s},{0.8f,0.2f,0.2f},{1,1}},{{-s, s, s},{0.8f,0.2f,0.2f},{0,1}},
        {{ s,-s,-s},{0.2f,0.8f,0.2f},{0,0}},{{-s,-s,-s},{0.2f,0.8f,0.2f},{1,0}},
        {{-s, s,-s},{0.2f,0.8f,0.2f},{1,1}},{{ s, s,-s},{0.2f,0.8f,0.2f},{0,1}},
        {{-s, s, s},{0.2f,0.2f,0.8f},{0,0}},{{ s, s, s},{0.2f,0.2f,0.8f},{1,0}},
        {{ s, s,-s},{0.2f,0.2f,0.8f},{1,1}},{{-s, s,-s},{0.2f,0.2f,0.8f},{0,1}},
        {{-s,-s,-s},{0.8f,0.8f,0.2f},{0,0}},{{ s,-s,-s},{0.8f,0.8f,0.2f},{1,0}},
        {{ s,-s, s},{0.8f,0.8f,0.2f},{1,1}},{{-s,-s, s},{0.8f,0.8f,0.2f},{0,1}},
        {{ s,-s, s},{0.2f,0.8f,0.8f},{0,0}},{{ s,-s,-s},{0.2f,0.8f,0.8f},{1,0}},
        {{ s, s,-s},{0.2f,0.8f,0.8f},{1,1}},{{ s, s, s},{0.2f,0.8f,0.8f},{0,1}},
        {{-s,-s,-s},{0.8f,0.2f,0.8f},{0,0}},{{-s,-s, s},{0.8f,0.2f,0.8f},{1,0}},
        {{-s, s, s},{0.8f,0.2f,0.8f},{1,1}},{{-s, s,-s},{0.8f,0.2f,0.8f},{0,1}}
    };
}

std::vector<Vertex> createPlayerMesh(float w, float h) {
    float hw = w/2.0f;
    glm::vec3 body{0.9f,0.7f,0.3f}, head{0.95f,0.8f,0.6f};
    return {
        {{-hw,0, hw},body,{0,0}},{{ hw,0, hw},body,{1,0}},{{ hw,h, hw},head,{1,1}},{{-hw,h, hw},head,{0,1}},
        {{ hw,0,-hw},body,{0,0}},{{-hw,0,-hw},body,{1,0}},{{-hw,h,-hw},head,{1,1}},{{ hw,h,-hw},head,{0,1}},
        {{-hw,h, hw},head,{0,0}},{{ hw,h, hw},head,{1,0}},{{ hw,h,-hw},head,{1,1}},{{-hw,h,-hw},head,{0,1}},
        {{-hw,0,-hw},body,{0,0}},{{ hw,0,-hw},body,{1,0}},{{ hw,0, hw},body,{1,1}},{{-hw,0, hw},body,{0,1}},
        {{ hw,0, hw},body,{0,0}},{{ hw,0,-hw},body,{1,0}},{{ hw,h,-hw},head,{1,1}},{{ hw,h, hw},head,{0,1}},
        {{-hw,0,-hw},body,{0,0}},{{-hw,0, hw},body,{1,0}},{{-hw,h, hw},head,{1,1}},{{-hw,h,-hw},head,{0,1}}
    };
}

std::vector<uint32_t> createBoxIndices(uint32_t base) {
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; f++) {
        uint32_t b = base + f * 4;
        idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3});
    }
    return idx;
}

struct MeshInfo { uint32_t indexStart, indexCount; int32_t vertexOffset; };

class Application {
public:
    void run() { initWindow(); initVulkan(); mainLoop(); cleanup(); }

private:
    GLFWwindow* m_window = nullptr;
    VulkanContext m_context;
    VulkanSwapchain m_swapchain;
    DescriptorManager m_descriptors;
    VulkanPipeline m_pipeline;
    
    VulkanBuffer m_terrainVB, m_terrainIB;
    uint32_t m_terrainIndexCount = 0;
    VulkanBuffer m_staticVB, m_staticIB;
    std::vector<MeshInfo> m_meshes;
    
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailable, m_renderFinished;
    std::vector<VkFence> m_inFlight;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;
    
    World m_world;
    ChunkManager m_chunks;
    RegionStateMachine m_regions;
    
    bool m_mouseCaptured = true;
    float m_scrollDelta = 0.0f;
    Timer m_timer;
    float m_logTimer = 0.0f;
    float m_totalPlayTime = 0.0f;
    
    RegionVisuals m_currentVisuals;
    RegionState m_lastLoggedState = RegionState::Stable;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(1280, 720, "Mythbreaker - Complete", nullptr, nullptr);
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
        Logger::info("Version 0.1.0 - Milestone 8: Save/Load Complete");
        m_context.init(m_window);
        m_swapchain.init(&m_context, m_window);
        m_descriptors.init(&m_context);
        m_pipeline.init(&m_context, &m_swapchain, &m_descriptors, "shaders/basic.vert.spv", "shaders/basic.frag.spv");
        
        m_currentVisuals = RegionVisuals::forState(RegionState::Stable);
        
        createMeshes();
        createEntities();
        createSyncObjects();
        
        m_chunks.update(glm::vec3(0));
        rebuildTerrain();
        
        Logger::info("Engine initialized");
        Logger::info("Controls: WASD move, Mouse look, Space jump");
        Logger::info("F5 = Quick Save | F9 = Quick Load");
        
        // Auto-load if save exists
        if (SaveManager::saveExists()) {
            Logger::info("Save file found - press F9 to load");
        }
    }

    void createMeshes() {
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;
        m_meshes.resize(2);
        
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
        Entity player = m_world.createPlayer(glm::vec3(0, 0, 0));
        auto& r = m_world.renderables.get(player);
        r.indexStart = m_meshes[1].indexStart;
        r.indexCount = m_meshes[1].indexCount;
        r.vertexOffset = m_meshes[1].vertexOffset;
        
        m_world.createCamera(player);
        
        for (int x = -50; x <= 50; x += 25) {
            for (int z = -50; z <= 50; z += 25) {
                if (x == 0 && z == 0) continue;
                float h = 1.0f + chunkRandom(x, z, 99) * 1.5f;
                Entity e = m_world.createLandmark(
                    glm::vec3(static_cast<float>(x), h/2, static_cast<float>(z)),
                    glm::vec3(1.5f, h, 1.5f),
                    chunkRandom(x, z, 100) * 360.0f
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
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(m_context.device(), &si, nullptr, &m_imageAvailable[i]);
            vkCreateSemaphore(m_context.device(), &si, nullptr, &m_renderFinished[i]);
            vkCreateFence(m_context.device(), &fi, nullptr, &m_inFlight[i]);
        }
    }

    // === SAVE SYSTEM ===
    void saveGame() {
        SaveData data;
        data.playTime = m_totalPlayTime;
        
        // Save player state
        if (m_world.playerEntity != NULL_ENTITY) {
            const auto& t = m_world.transforms.get(m_world.playerEntity);
            data.playerPosition = t.position;
            data.playerYaw = t.rotation.y;
        }
        
        // Save camera state
        if (m_world.cameraEntity != NULL_ENTITY) {
            const auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity);
            if (cam) {
                data.cameraYaw = cam->yaw;
                data.cameraPitch = cam->pitch;
                data.cameraDistance = cam->distance;
            }
        }
        
        // Save region states - iterate tracked regions
        // We need to access RegionStateMachine's internal data
        // For now, save current region
        auto rc = m_regions.currentRegion();
        const auto& rd = m_regions.getCurrentRegionData();
        data.regions.push_back({rc.x, rc.z, static_cast<int>(rd.state), rd.realityPressure});
        
        if (SaveManager::save(data)) {
            Logger::info("*** GAME SAVED ***");
            Logger::infof("Position: ({:.1f}, {:.1f}, {:.1f})", 
                data.playerPosition.x, data.playerPosition.y, data.playerPosition.z);
        } else {
            Logger::error("Failed to save game!");
        }
    }
    
    void loadGame() {
        SaveData data;
        if (!SaveManager::load(data)) {
            Logger::error("Failed to load save file!");
            return;
        }
        
        m_totalPlayTime = data.playTime;
        
        // Restore player state
        if (m_world.playerEntity != NULL_ENTITY) {
            auto& t = m_world.transforms.get(m_world.playerEntity);
            t.position = data.playerPosition;
            t.rotation.y = data.playerYaw;
            
            auto* controller = m_world.playerControllers.tryGet(m_world.playerEntity);
            if (controller) {
                controller->targetYaw = data.playerYaw;
            }
            
            auto* vel = m_world.velocities.tryGet(m_world.playerEntity);
            if (vel) {
                vel->linear = glm::vec3(0);
            }
        }
        
        // Restore camera state
        if (m_world.cameraEntity != NULL_ENTITY) {
            auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity);
            if (cam) {
                cam->yaw = data.cameraYaw;
                cam->pitch = data.cameraPitch;
                cam->distance = data.cameraDistance;
                // Snap camera position
                if (m_world.playerEntity != NULL_ENTITY) {
                    const auto& pt = m_world.transforms.get(m_world.playerEntity);
                    float horizontalDist = cam->distance * cos(glm::radians(cam->pitch));
                    float verticalDist = cam->distance * sin(glm::radians(cam->pitch));
                    cam->currentPosition.x = pt.position.x - horizontalDist * sin(glm::radians(cam->yaw));
                    cam->currentPosition.z = pt.position.z - horizontalDist * cos(glm::radians(cam->yaw));
                    cam->currentPosition.y = pt.position.y + cam->heightOffset + verticalDist;
                }
            }
        }
        
        // Restore region states
        for (const auto& rs : data.regions) {
            RegionCoord coord{rs.x, rs.z};
            auto& region = m_regions.getOrCreateRegion(coord);
            region.state = static_cast<RegionState>(rs.state);
            region.realityPressure = rs.pressure;
        }
        
        // Update visuals immediately
        m_currentVisuals = m_regions.getCurrentVisuals();
        m_lastLoggedState = m_regions.getCurrentRegionData().state;
        
        // Force chunk rebuild at new position
        if (m_world.playerEntity != NULL_ENTITY) {
            const auto& pt = m_world.transforms.get(m_world.playerEntity);
            m_chunks.update(pt.position);
            m_chunks.forceRebuild();
            vkDeviceWaitIdle(m_context.device());
            rebuildTerrain();
        }
        
        Logger::info("*** GAME LOADED ***");
        Logger::infof("Position: ({:.1f}, {:.1f}, {:.1f}) | Play time: {:.0f}s", 
            data.playerPosition.x, data.playerPosition.y, data.playerPosition.z, data.playTime);
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
            updateCamera(m_world, dt, m_mouseCaptured, 
                Input::instance().mouseDeltaX(), Input::instance().mouseDeltaY(), m_scrollDelta);
            
            m_scrollDelta = 0.0f;
            Input::instance().update();
            
            if (m_world.playerEntity != NULL_ENTITY) {
                const auto& pt = m_world.transforms.get(m_world.playerEntity);
                m_regions.update(pt.position, dt);
                
                RegionVisuals target = m_regions.getCurrentVisuals();
                float visualLerp = 1.0f - exp(-2.0f * dt);
                m_currentVisuals.fogColor = glm::mix(m_currentVisuals.fogColor, target.fogColor, visualLerp);
                m_currentVisuals.skyColor = glm::mix(m_currentVisuals.skyColor, target.skyColor, visualLerp);
                m_currentVisuals.fogDensity = glm::mix(m_currentVisuals.fogDensity, target.fogDensity, visualLerp);
                m_currentVisuals.colorIntensity = glm::mix(m_currentVisuals.colorIntensity, target.colorIntensity, visualLerp);
                
                m_chunks.update(pt.position);
                if (m_chunks.isDirty()) { vkDeviceWaitIdle(m_context.device()); rebuildTerrain(); }
            }
            
            drawFrame();
            
            m_logTimer += dt;
            if (m_logTimer >= 3.0f) {
                if (m_world.playerEntity != NULL_ENTITY) {
                    const auto& pt = m_world.transforms.get(m_world.playerEntity);
                    const auto& rd = m_regions.getCurrentRegionData();
                    auto rc = m_regions.currentRegion();
                    
                    if (rd.state != m_lastLoggedState) {
                        Logger::infof("*** REGION STATE: {} -> {} ***", 
                            regionStateName(m_lastLoggedState), regionStateName(rd.state));
                        m_lastLoggedState = rd.state;
                    }
                    
                    Logger::infof("FPS: {:.0f} | Pos: ({:.0f},{:.0f}) | Region: ({},{}) | {}: {:.0f}% | Time: {:.0f}s", 
                        m_timer.fps(), pt.position.x, pt.position.z,
                        rc.x, rc.z, regionStateName(rd.state), rd.realityPressure * 100.0f,
                        m_totalPlayTime);
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
        
        // Save/Load
        if (input.isKeyPressed(GLFW_KEY_F5)) {
            saveGame();
        }
        if (input.isKeyPressed(GLFW_KEY_F9)) {
            loadGame();
        }
    }

    void drawFrame() {
        vkWaitForFences(m_context.device(), 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex;
        if (!m_swapchain.acquireNextImage(imageIndex, m_imageAvailable[m_currentFrame])) { recreateSwapchain(); return; }
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
            m_framebufferResized = false; recreateSwapchain();
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
        m_descriptors.updateCameraUBO(m_currentFrame, ubo);
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &bi);
        
        auto ext = m_swapchain.extent();
        std::array<VkClearValue, 2> clears{}; 
        clears[0].color = {{m_currentVisuals.skyColor.r, m_currentVisuals.skyColor.g, m_currentVisuals.skyColor.b, 1.0f}}; 
        clears[1].depthStencil = {1.0f, 0};
        
        VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpi.renderPass = m_swapchain.renderPass();
        rpi.framebuffer = m_swapchain.framebuffer(imageIndex);
        rpi.renderArea = {{0,0}, ext};
        rpi.clearValueCount = 2; rpi.pClearValues = clears.data();
        
        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline());
        
        VkViewport vp{0,0,float(ext.width),float(ext.height),0,1};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0}, ext};
        vkCmdSetScissor(cmd, 0, 1, &sc);
        
        VkDescriptorSet ds = m_descriptors.descriptorSet(m_currentFrame);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipelineLayout(), 0, 1, &ds, 0, nullptr);
        
        PushConstants push{};
        
        if (m_terrainIndexCount > 0) {
            VkBuffer tb[] = {m_terrainVB.buffer()}; VkDeviceSize to[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, tb, to);
            vkCmdBindIndexBuffer(cmd, m_terrainIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
            push.model = glm::mat4(1.0f);
            vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, m_terrainIndexCount, 1, 0, 0, 0);
        }
        
        VkBuffer sb[] = {m_staticVB.buffer()}; VkDeviceSize so[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, sb, so);
        vkCmdBindIndexBuffer(cmd, m_staticIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
        
        m_world.renderables.each([&](Entity e, const Renderable& r) {
            if (!r.visible) return;
            const auto* t = m_world.transforms.tryGet(e);
            if (!t) return;
            push.model = t->getMatrix();
            vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, r.indexCount, 1, r.indexStart, r.vertexOffset, 0);
        });
        
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
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_context.device(), m_imageAvailable[i], nullptr);
            vkDestroySemaphore(m_context.device(), m_renderFinished[i], nullptr);
            vkDestroyFence(m_context.device(), m_inFlight[i], nullptr);
        }
        m_terrainIB.destroy(); m_terrainVB.destroy();
        m_staticIB.destroy(); m_staticVB.destroy();
        m_pipeline.destroy(); m_descriptors.destroy();
        m_swapchain.destroy(); m_context.destroy();
        glfwDestroyWindow(m_window); glfwTerminate();
    }
};

int main() {
    try { Application app; app.run(); }
    catch (const std::exception& e) { Logger::fatal(e.what()); return 1; }
    return 0;
}
