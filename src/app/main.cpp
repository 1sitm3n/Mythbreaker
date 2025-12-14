#include <random>
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

float chunkRandom(int x, int z, int seed = 0) {
    int n = x + z * 57 + seed * 131; n = (n << 13) ^ n;
    return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
}

struct Chunk {
    ChunkCoord coord; std::vector<Vertex> vertices; std::vector<uint32_t> indices;
    void generate(float chunkSize) {
        glm::vec3 color(1.0f);
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        float halfSize = chunkSize / 2.0f, worldX = coord.x * chunkSize, worldZ = coord.z * chunkSize;
        float h00 = chunkRandom(coord.x, coord.z, 10) * 0.15f;
        float h10 = chunkRandom(coord.x + 1, coord.z, 10) * 0.15f;
        float h01 = chunkRandom(coord.x, coord.z + 1, 10) * 0.15f;
        float h11 = chunkRandom(coord.x + 1, coord.z + 1, 10) * 0.15f;
        float uvScale = 2.0f;
        vertices = {
            {{worldX - halfSize, h00, worldZ - halfSize}, color, {0, 0}, normal},
            {{worldX + halfSize, h10, worldZ - halfSize}, color, {uvScale, 0}, normal},
            {{worldX + halfSize, h11, worldZ + halfSize}, color, {uvScale, uvScale}, normal},
            {{worldX - halfSize, h01, worldZ + halfSize}, color, {0, uvScale}, normal}
        };
        indices = {0, 2, 1, 0, 3, 2};
    }
};

class ChunkManager {
public:
    float chunkSize = 10.0f; int loadRadius = 5;
    void update(const glm::vec3& playerPos) {
        int px = static_cast<int>(floor(playerPos.x / chunkSize)), pz = static_cast<int>(floor(playerPos.z / chunkSize));
        for (int x = px - loadRadius; x <= px + loadRadius; x++) {
            for (int z = pz - loadRadius; z <= pz + loadRadius; z++) {
                ChunkCoord coord{x, z};
                if (m_chunks.find(coord) == m_chunks.end()) { Chunk chunk; chunk.coord = coord; chunk.generate(chunkSize); m_chunks[coord] = std::move(chunk); m_dirty = true; }
            }
        }
        std::vector<ChunkCoord> toUnload;
        for (auto& [coord, chunk] : m_chunks) { if (abs(coord.x - px) > loadRadius + 1 || abs(coord.z - pz) > loadRadius + 1) toUnload.push_back(coord); }
        for (const auto& coord : toUnload) { m_chunks.erase(coord); m_dirty = true; }
    }
    void forceRebuild() { m_dirty = true; } bool isDirty() const { return m_dirty; } void clearDirty() { m_dirty = false; }
    void buildMesh(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        verts.clear(); inds.clear();
        for (auto& [coord, chunk] : m_chunks) { uint32_t base = static_cast<uint32_t>(verts.size()); verts.insert(verts.end(), chunk.vertices.begin(), chunk.vertices.end()); for (uint32_t idx : chunk.indices) inds.push_back(base + idx); }
    }
private:
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_chunks; bool m_dirty = false;
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

std::vector<uint32_t> createBoxIndices(uint32_t base) { std::vector<uint32_t> idx; for (int f = 0; f < 6; f++) { uint32_t b = base + f * 4; idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3}); } return idx; }

struct MeshInfo { uint32_t indexStart, indexCount; int32_t vertexOffset; };

class Application {
public:
    void run() { initWindow(); initVulkan(); mainLoop(); cleanup(); }
private:
    GLFWwindow* m_window = nullptr; VulkanContext m_context; VulkanSwapchain m_swapchain; DescriptorManager m_descriptors;
    VulkanPipeline m_skyPipeline;
    VulkanPipeline m_litPipeline;
    VulkanBuffer m_terrainVB, m_terrainIB; uint32_t m_terrainIndexCount = 0; VulkanBuffer m_staticVB, m_staticIB; std::vector<MeshInfo> m_meshes;
    VulkanTexture m_groundTexture, m_stoneTexture, m_playerTexture; uint32_t m_groundMaterial = 0, m_stoneMaterial = 0, m_playerMaterial = 0;
    std::vector<VkCommandBuffer> m_commandBuffers; std::vector<VkSemaphore> m_imageAvailable, m_renderFinished; std::vector<VkFence> m_inFlight;
    uint32_t m_currentFrame = 0; bool m_framebufferResized = false;
    World m_world; ChunkManager m_chunks; RegionStateMachine m_regions;
    bool m_mouseCaptured = true; float m_scrollDelta = 0.0f; Timer m_timer; float m_logTimer = 0.0f, m_totalPlayTime = 0.0f;
    RegionVisuals m_currentVisuals; RegionState m_lastLoggedState = RegionState::Stable;
    
    glm::vec3 m_sunDirection = glm::normalize(glm::vec3(0.5f, -0.8f, 0.3f));
    float m_sunIntensity = 1.2f;
    glm::vec3 m_sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
    float m_ambientIntensity = 0.3f;
    glm::vec3 m_skyColorTop = glm::vec3(0.4f, 0.6f, 0.9f);
    glm::vec3 m_skyColorBottom = glm::vec3(0.7f, 0.8f, 0.95f);

    void initWindow() {
        glfwInit(); glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(1280, 720, "Mythbreaker - Lit World", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) { reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_framebufferResized = true; });
        glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double y) { reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_scrollDelta = static_cast<float>(y); });
        Input::instance().init(m_window); glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    void initVulkan() {
        Logger::info("=== MYTHBREAKER ENGINE ==="); Logger::info("Version 0.3.0 - Milestone 10: Skybox & Lighting");
        m_context.init(m_window); m_swapchain.init(&m_context, m_window); m_descriptors.init(&m_context);
        m_skyPipeline.initSky(&m_context, &m_swapchain, &m_descriptors, "shaders/sky.vert.spv", "shaders/sky.frag.spv");
        m_litPipeline.init(&m_context, &m_swapchain, &m_descriptors, "shaders/lit.vert.spv", "shaders/lit.frag.spv");
        m_currentVisuals = RegionVisuals::forState(RegionState::Stable);
        createTextures(); createMeshes(); createEntities(); createSyncObjects();
        m_chunks.update(glm::vec3(0)); rebuildTerrain();
        Logger::info("Engine initialized with lighting"); Logger::info("F5 = Save | F9 = Load");
    }

    void createTextures() {
        std::mt19937 rng(42);
        std::vector<uint8_t> groundPixels(256 * 256 * 4);
        for (int i = 0; i < 256 * 256; i++) { float n = (rng() % 100) / 100.0f; uint8_t b = static_cast<uint8_t>(60 + n * 40); groundPixels[i*4+0] = b; groundPixels[i*4+1] = static_cast<uint8_t>(b*0.7f); groundPixels[i*4+2] = static_cast<uint8_t>(b*0.4f); groundPixels[i*4+3] = 255; }
        m_groundTexture.loadFromMemory(&m_context, groundPixels.data(), 256, 256); m_groundMaterial = m_descriptors.createMaterial(m_groundTexture);
        
        std::vector<uint8_t> stonePixels(128 * 128 * 4);
        for (int i = 0; i < 128 * 128; i++) { float n = (rng() % 100) / 100.0f; uint8_t b = static_cast<uint8_t>(100 + n * 80); stonePixels[i*4+0] = b; stonePixels[i*4+1] = static_cast<uint8_t>(b*0.95f); stonePixels[i*4+2] = static_cast<uint8_t>(b*0.9f); stonePixels[i*4+3] = 255; }
        m_stoneTexture.loadFromMemory(&m_context, stonePixels.data(), 128, 128); m_stoneMaterial = m_descriptors.createMaterial(m_stoneTexture);
        
        std::vector<uint8_t> playerPixels(64 * 64 * 4);
        for (int i = 0; i < 64 * 64; i++) { float n = (rng() % 20) / 100.0f; playerPixels[i*4+0] = static_cast<uint8_t>(220 + n * 20); playerPixels[i*4+1] = static_cast<uint8_t>(180 + n * 20); playerPixels[i*4+2] = static_cast<uint8_t>(140 + n * 20); playerPixels[i*4+3] = 255; }
        m_playerTexture.loadFromMemory(&m_context, playerPixels.data(), 64, 64); m_playerMaterial = m_descriptors.createMaterial(m_playerTexture);
    }

    void createMeshes() {
        std::vector<Vertex> verts; std::vector<uint32_t> inds; m_meshes.resize(2);
        auto cube = createCube(1.0f); m_meshes[0].vertexOffset = static_cast<int32_t>(verts.size()); verts.insert(verts.end(), cube.begin(), cube.end());
        auto cubeIdx = createBoxIndices(0); m_meshes[0].indexStart = static_cast<uint32_t>(inds.size()); inds.insert(inds.end(), cubeIdx.begin(), cubeIdx.end()); m_meshes[0].indexCount = static_cast<uint32_t>(cubeIdx.size());
        auto player = createPlayerMesh(0.6f, 1.8f); m_meshes[1].vertexOffset = static_cast<int32_t>(verts.size()); verts.insert(verts.end(), player.begin(), player.end());
        auto playerIdx = createBoxIndices(0); m_meshes[1].indexStart = static_cast<uint32_t>(inds.size()); inds.insert(inds.end(), playerIdx.begin(), playerIdx.end()); m_meshes[1].indexCount = static_cast<uint32_t>(playerIdx.size());
        VulkanBuffer::createWithStaging(&m_context, m_staticVB, verts.data(), sizeof(Vertex)*verts.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_staticIB, inds.data(), sizeof(uint32_t)*inds.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    void createEntities() {
        Entity player = m_world.createPlayer(glm::vec3(0, 0, 0)); auto& r = m_world.renderables.get(player);
        r.indexStart = m_meshes[1].indexStart; r.indexCount = m_meshes[1].indexCount; r.vertexOffset = m_meshes[1].vertexOffset;
        m_world.createCamera(player);
        for (int x = -50; x <= 50; x += 25) { for (int z = -50; z <= 50; z += 25) { if (x == 0 && z == 0) continue;
            float h = 1.0f + chunkRandom(x, z, 99) * 1.5f;
            Entity e = m_world.createLandmark(glm::vec3(static_cast<float>(x), h/2, static_cast<float>(z)), glm::vec3(1.5f, h, 1.5f), chunkRandom(x, z, 100) * 360.0f);
            auto& lr = m_world.renderables.get(e); lr.indexStart = m_meshes[0].indexStart; lr.indexCount = m_meshes[0].indexCount; lr.vertexOffset = m_meshes[0].vertexOffset;
        } }
    }

    void rebuildTerrain() {
        std::vector<Vertex> v; std::vector<uint32_t> i; m_chunks.buildMesh(v, i); m_terrainIndexCount = static_cast<uint32_t>(i.size());
        if (m_terrainIndexCount == 0) return; m_terrainVB.destroy(); m_terrainIB.destroy();
        VulkanBuffer::createWithStaging(&m_context, m_terrainVB, v.data(), sizeof(Vertex)*v.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_terrainIB, i.data(), sizeof(uint32_t)*i.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_chunks.clearDirty();
    }

    void createSyncObjects() {
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = m_context.commandPool(); ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        vkAllocateCommandBuffers(m_context.device(), &ai, m_commandBuffers.data());
        m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT); m_renderFinished.resize(MAX_FRAMES_IN_FLIGHT); m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) { vkCreateSemaphore(m_context.device(), &si, nullptr, &m_imageAvailable[i]); vkCreateSemaphore(m_context.device(), &si, nullptr, &m_renderFinished[i]); vkCreateFence(m_context.device(), &fi, nullptr, &m_inFlight[i]); }
    }

    void saveGame() { SaveData data; data.playTime = m_totalPlayTime; if (m_world.playerEntity != NULL_ENTITY) { const auto& t = m_world.transforms.get(m_world.playerEntity); data.playerPosition = t.position; data.playerYaw = t.rotation.y; } if (m_world.cameraEntity != NULL_ENTITY) { const auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity); if (cam) { data.cameraYaw = cam->yaw; data.cameraPitch = cam->pitch; data.cameraDistance = cam->distance; } } auto rc = m_regions.currentRegion(); const auto& rd = m_regions.getCurrentRegionData(); data.regions.push_back({rc.x, rc.z, static_cast<int>(rd.state), rd.realityPressure}); if (SaveManager::save(data)) Logger::info("*** SAVED ***"); }
    void loadGame() { SaveData data; if (!SaveManager::load(data)) { Logger::error("Load failed!"); return; } m_totalPlayTime = data.playTime; if (m_world.playerEntity != NULL_ENTITY) { auto& t = m_world.transforms.get(m_world.playerEntity); t.position = data.playerPosition; t.rotation.y = data.playerYaw; if (auto* c = m_world.playerControllers.tryGet(m_world.playerEntity)) c->targetYaw = data.playerYaw; if (auto* v = m_world.velocities.tryGet(m_world.playerEntity)) v->linear = glm::vec3(0); } if (m_world.cameraEntity != NULL_ENTITY) { if (auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity)) { cam->yaw = data.cameraYaw; cam->pitch = data.cameraPitch; cam->distance = data.cameraDistance; } } for (const auto& rs : data.regions) { auto& region = m_regions.getOrCreateRegion({rs.x, rs.z}); region.state = static_cast<RegionState>(rs.state); region.realityPressure = rs.pressure; } m_currentVisuals = m_regions.getCurrentVisuals(); m_lastLoggedState = m_regions.getCurrentRegionData().state; if (m_world.playerEntity != NULL_ENTITY) { m_chunks.update(m_world.transforms.get(m_world.playerEntity).position); m_chunks.forceRebuild(); vkDeviceWaitIdle(m_context.device()); rebuildTerrain(); } Logger::info("*** LOADED ***"); }

    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents(); m_timer.tick(); float dt = m_timer.clampedDeltaTime(); m_totalPlayTime += dt;
            processInput(dt);
            auto* cam = m_world.cameraControllers.tryGet(m_world.cameraEntity);
            updatePlayerInput(m_world, dt, m_mouseCaptured, Input::instance().mouseDeltaX(), Input::instance().mouseDeltaY(), cam);
            updateMovement(m_world, dt);
            updateCamera(m_world, dt, m_mouseCaptured, Input::instance().mouseDeltaX(), Input::instance().mouseDeltaY(), m_scrollDelta);
            m_scrollDelta = 0.0f; Input::instance().update();
            if (m_world.playerEntity != NULL_ENTITY) {
                const auto& pt = m_world.transforms.get(m_world.playerEntity); m_regions.update(pt.position, dt);
                RegionVisuals target = m_regions.getCurrentVisuals(); float visualLerp = 1.0f - exp(-2.0f * dt);
                m_currentVisuals.fogColor = glm::mix(m_currentVisuals.fogColor, target.fogColor, visualLerp);
                m_currentVisuals.skyColor = glm::mix(m_currentVisuals.skyColor, target.skyColor, visualLerp);
                m_chunks.update(pt.position); if (m_chunks.isDirty()) { vkDeviceWaitIdle(m_context.device()); rebuildTerrain(); }
            }
            drawFrame();
            m_logTimer += dt; if (m_logTimer >= 3.0f) { if (m_world.playerEntity != NULL_ENTITY) { const auto& pt = m_world.transforms.get(m_world.playerEntity); const auto& rd = m_regions.getCurrentRegionData(); if (rd.state != m_lastLoggedState) { Logger::infof("*** REGION: {} -> {} ***", regionStateName(m_lastLoggedState), regionStateName(rd.state)); m_lastLoggedState = rd.state; } Logger::infof("FPS: {:.0f} | Pos: ({:.0f},{:.0f}) | {}: {:.0f}%", m_timer.fps(), pt.position.x, pt.position.z, regionStateName(rd.state), rd.realityPressure * 100.0f); } m_logTimer = 0.0f; }
        }
        vkDeviceWaitIdle(m_context.device());
    }

    void processInput(float dt) { auto& input = Input::instance(); if (input.isKeyPressed(GLFW_KEY_ESCAPE)) { glfwSetWindowShouldClose(m_window, true); return; } if (input.isKeyPressed(GLFW_KEY_TAB)) { m_mouseCaptured = !m_mouseCaptured; glfwSetInputMode(m_window, GLFW_CURSOR, m_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL); } if (input.isKeyPressed(GLFW_KEY_F5)) saveGame(); if (input.isKeyPressed(GLFW_KEY_F9)) loadGame(); }

    void drawFrame() {
        vkWaitForFences(m_context.device(), 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex; if (!m_swapchain.acquireNextImage(imageIndex, m_imageAvailable[m_currentFrame])) { recreateSwapchain(); return; }
        vkResetFences(m_context.device(), 1, &m_inFlight[m_currentFrame]);
        updateCameraUBO(); recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);
        VkSemaphore waitSems[] = {m_imageAvailable[m_currentFrame]}; VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}; VkSemaphore signalSems[] = {m_renderFinished[m_currentFrame]};
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitSems; si.pWaitDstStageMask = waitStages; si.commandBufferCount = 1; si.pCommandBuffers = &m_commandBuffers[m_currentFrame]; si.signalSemaphoreCount = 1; si.pSignalSemaphores = signalSems;
        vkQueueSubmit(m_context.graphicsQueue(), 1, &si, m_inFlight[m_currentFrame]);
        if (!m_swapchain.present(imageIndex, m_renderFinished[m_currentFrame]) || m_framebufferResized) { m_framebufferResized = false; recreateSwapchain(); }
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void updateCameraUBO() {
        auto ext = m_swapchain.extent(); CameraUBO ubo{};
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
        vkResetCommandBuffer(cmd, 0); VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cmd, &bi);
        auto ext = m_swapchain.extent(); std::array<VkClearValue, 2> clears{}; clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rpi.renderPass = m_swapchain.renderPass(); rpi.framebuffer = m_swapchain.framebuffer(imageIndex); rpi.renderArea = {{0,0}, ext}; rpi.clearValueCount = 2; rpi.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        
        VkViewport vp{0,0,float(ext.width),float(ext.height),0,1}; vkCmdSetViewport(cmd, 0, 1, &vp); VkRect2D sc{{0,0}, ext}; vkCmdSetScissor(cmd, 0, 1, &sc);
        
        // Draw sky first
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
            VkBuffer tb[] = {m_terrainVB.buffer()}; VkDeviceSize to[] = {0}; vkCmdBindVertexBuffers(cmd, 0, 1, tb, to); vkCmdBindIndexBuffer(cmd, m_terrainIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
            push.model = glm::mat4(1.0f); vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, m_terrainIndexCount, 1, 0, 0, 0);
        }
        
        VkBuffer sb[] = {m_staticVB.buffer()}; VkDeviceSize so[] = {0}; vkCmdBindVertexBuffers(cmd, 0, 1, sb, so); vkCmdBindIndexBuffer(cmd, m_staticIB.buffer(), 0, VK_INDEX_TYPE_UINT32);
        
        // Landmarks
        m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_stoneMaterial);
        m_world.landmarkTags.each([&](Entity e, const LandmarkTag&) { const auto* t = m_world.transforms.tryGet(e); const auto* r = m_world.renderables.tryGet(e); if (!t || !r || !r->visible) return; push.model = t->getMatrix(); vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push); vkCmdDrawIndexed(cmd, r->indexCount, 1, r->indexStart, r->vertexOffset, 0); });
        
        // Player
        m_descriptors.bindMaterial(cmd, m_litPipeline.pipelineLayout(), m_currentFrame, m_playerMaterial);
        if (m_world.playerEntity != NULL_ENTITY) { const auto* t = m_world.transforms.tryGet(m_world.playerEntity); const auto* r = m_world.renderables.tryGet(m_world.playerEntity); if (t && r && r->visible) { push.model = t->getMatrix(); vkCmdPushConstants(cmd, m_litPipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push); vkCmdDrawIndexed(cmd, r->indexCount, 1, r->indexStart, r->vertexOffset, 0); } }
        
        vkCmdEndRenderPass(cmd); vkEndCommandBuffer(cmd);
    }

    void recreateSwapchain() { int w=0, h=0; glfwGetFramebufferSize(m_window, &w, &h); while (w==0||h==0) { glfwGetFramebufferSize(m_window,&w,&h); glfwWaitEvents(); } vkDeviceWaitIdle(m_context.device()); m_swapchain.recreate(); }

    void cleanup() { m_groundTexture.destroy(); m_stoneTexture.destroy(); m_playerTexture.destroy(); for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) { vkDestroySemaphore(m_context.device(), m_imageAvailable[i], nullptr); vkDestroySemaphore(m_context.device(), m_renderFinished[i], nullptr); vkDestroyFence(m_context.device(), m_inFlight[i], nullptr); } m_terrainIB.destroy(); m_terrainVB.destroy(); m_staticIB.destroy(); m_staticVB.destroy(); m_litPipeline.destroy(); m_skyPipeline.destroy(); m_descriptors.destroy(); m_swapchain.destroy(); m_context.destroy(); glfwDestroyWindow(m_window); glfwTerminate(); }
};

int main() { try { Application app; app.run(); } catch (const std::exception& e) { Logger::fatal(e.what()); return 1; } return 0; }
