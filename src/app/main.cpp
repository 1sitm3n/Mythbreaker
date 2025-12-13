#include "engine/Logger.h"
#include "engine/Timer.h"
#include "engine/Input.h"
#include "engine/vulkan/VulkanContext.h"
#include "engine/vulkan/VulkanSwapchain.h"
#include "engine/vulkan/VulkanPipeline.h"
#include "engine/vulkan/VulkanBuffer.h"
#include "engine/vulkan/VulkanDescriptors.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <functional>

using namespace myth;
using namespace myth::vk;

// Hash function for chunk coordinates
struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& other) const { return x == other.x && z == other.z; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 16);
    }
};

// Simple pseudo-random based on coordinates
float chunkRandom(int x, int z, int seed = 0) {
    int n = x + z * 57 + seed * 131;
    n = (n << 13) ^ n;
    return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
}

// Chunk data
struct Chunk {
    ChunkCoord coord;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float baseHeight = 0.0f;
    glm::vec3 color;
    bool needsUpload = true;
    
    void generate(float chunkSize) {
        // Procedural height and color based on coordinates
        baseHeight = chunkRandom(coord.x, coord.z, 0) * 0.3f;
        
        // Color varies by chunk for visibility
        float r = 0.12f + chunkRandom(coord.x, coord.z, 1) * 0.08f;
        float g = 0.10f + chunkRandom(coord.x, coord.z, 2) * 0.06f;
        float b = 0.08f + chunkRandom(coord.x, coord.z, 3) * 0.04f;
        color = glm::vec3(r, g, b);
        
        // Create chunk mesh (simple quad with slight height variation at corners)
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

// Chunk manager
class ChunkManager {
public:
    float chunkSize = 10.0f;
    int loadRadius = 5;  // Chunks to load in each direction
    
    void update(const glm::vec3& playerPos) {
        // Calculate which chunk the player is in
        int playerChunkX = static_cast<int>(floor(playerPos.x / chunkSize));
        int playerChunkZ = static_cast<int>(floor(playerPos.z / chunkSize));
        
        // Mark chunks for loading
        for (int x = playerChunkX - loadRadius; x <= playerChunkX + loadRadius; x++) {
            for (int z = playerChunkZ - loadRadius; z <= playerChunkZ + loadRadius; z++) {
                ChunkCoord coord{x, z};
                if (m_chunks.find(coord) == m_chunks.end()) {
                    loadChunk(coord);
                }
            }
        }
        
        // Unload distant chunks
        std::vector<ChunkCoord> toUnload;
        for (auto& [coord, chunk] : m_chunks) {
            int dx = abs(coord.x - playerChunkX);
            int dz = abs(coord.z - playerChunkZ);
            if (dx > loadRadius + 1 || dz > loadRadius + 1) {
                toUnload.push_back(coord);
            }
        }
        
        for (const auto& coord : toUnload) {
            unloadChunk(coord);
        }
    }
    
    void loadChunk(const ChunkCoord& coord) {
        Chunk chunk;
        chunk.coord = coord;
        chunk.generate(chunkSize);
        m_chunks[coord] = std::move(chunk);
        m_meshDirty = true;
        Logger::infof("Chunk loaded: ({}, {})", coord.x, coord.z);
    }
    
    void unloadChunk(const ChunkCoord& coord) {
        m_chunks.erase(coord);
        m_meshDirty = true;
        Logger::infof("Chunk unloaded: ({}, {})", coord.x, coord.z);
    }
    
    bool isMeshDirty() const { return m_meshDirty; }
    void clearDirty() { m_meshDirty = false; }
    
    void buildMesh(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();
        
        for (auto& [coord, chunk] : m_chunks) {
            uint32_t baseVertex = static_cast<uint32_t>(outVertices.size());
            outVertices.insert(outVertices.end(), chunk.vertices.begin(), chunk.vertices.end());
            for (uint32_t idx : chunk.indices) {
                outIndices.push_back(baseVertex + idx);
            }
        }
    }
    
    size_t chunkCount() const { return m_chunks.size(); }
    
private:
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_chunks;
    bool m_meshDirty = false;
};

// Cube vertices
std::vector<Vertex> createCube(float size) {
    float s = size / 2.0f;
    return {
        {{-s, -s,  s}, {0.8f, 0.2f, 0.2f}, {0, 0}},
        {{ s, -s,  s}, {0.8f, 0.2f, 0.2f}, {1, 0}},
        {{ s,  s,  s}, {0.8f, 0.2f, 0.2f}, {1, 1}},
        {{-s,  s,  s}, {0.8f, 0.2f, 0.2f}, {0, 1}},
        {{ s, -s, -s}, {0.2f, 0.8f, 0.2f}, {0, 0}},
        {{-s, -s, -s}, {0.2f, 0.8f, 0.2f}, {1, 0}},
        {{-s,  s, -s}, {0.2f, 0.8f, 0.2f}, {1, 1}},
        {{ s,  s, -s}, {0.2f, 0.8f, 0.2f}, {0, 1}},
        {{-s,  s,  s}, {0.2f, 0.2f, 0.8f}, {0, 0}},
        {{ s,  s,  s}, {0.2f, 0.2f, 0.8f}, {1, 0}},
        {{ s,  s, -s}, {0.2f, 0.2f, 0.8f}, {1, 1}},
        {{-s,  s, -s}, {0.2f, 0.2f, 0.8f}, {0, 1}},
        {{-s, -s, -s}, {0.8f, 0.8f, 0.2f}, {0, 0}},
        {{ s, -s, -s}, {0.8f, 0.8f, 0.2f}, {1, 0}},
        {{ s, -s,  s}, {0.8f, 0.8f, 0.2f}, {1, 1}},
        {{-s, -s,  s}, {0.8f, 0.8f, 0.2f}, {0, 1}},
        {{ s, -s,  s}, {0.2f, 0.8f, 0.8f}, {0, 0}},
        {{ s, -s, -s}, {0.2f, 0.8f, 0.8f}, {1, 0}},
        {{ s,  s, -s}, {0.2f, 0.8f, 0.8f}, {1, 1}},
        {{ s,  s,  s}, {0.2f, 0.8f, 0.8f}, {0, 1}},
        {{-s, -s, -s}, {0.8f, 0.2f, 0.8f}, {0, 0}},
        {{-s, -s,  s}, {0.8f, 0.2f, 0.8f}, {1, 0}},
        {{-s,  s,  s}, {0.8f, 0.2f, 0.8f}, {1, 1}},
        {{-s,  s, -s}, {0.8f, 0.2f, 0.8f}, {0, 1}}
    };
}

std::vector<Vertex> createPlayerMesh(float width, float height) {
    float w = width / 2.0f;
    float h = height;
    glm::vec3 bodyColor = {0.9f, 0.7f, 0.3f};
    glm::vec3 headColor = {0.95f, 0.8f, 0.6f};
    return {
        {{-w, 0,  w}, bodyColor, {0, 0}}, {{ w, 0,  w}, bodyColor, {1, 0}},
        {{ w, h,  w}, headColor, {1, 1}}, {{-w, h,  w}, headColor, {0, 1}},
        {{ w, 0, -w}, bodyColor, {0, 0}}, {{-w, 0, -w}, bodyColor, {1, 0}},
        {{-w, h, -w}, headColor, {1, 1}}, {{ w, h, -w}, headColor, {0, 1}},
        {{-w, h,  w}, headColor, {0, 0}}, {{ w, h,  w}, headColor, {1, 0}},
        {{ w, h, -w}, headColor, {1, 1}}, {{-w, h, -w}, headColor, {0, 1}},
        {{-w, 0, -w}, bodyColor, {0, 0}}, {{ w, 0, -w}, bodyColor, {1, 0}},
        {{ w, 0,  w}, bodyColor, {1, 1}}, {{-w, 0,  w}, bodyColor, {0, 1}},
        {{ w, 0,  w}, bodyColor, {0, 0}}, {{ w, 0, -w}, bodyColor, {1, 0}},
        {{ w, h, -w}, headColor, {1, 1}}, {{ w, h,  w}, headColor, {0, 1}},
        {{-w, 0, -w}, bodyColor, {0, 0}}, {{-w, 0,  w}, bodyColor, {1, 0}},
        {{-w, h,  w}, headColor, {1, 1}}, {{-w, h, -w}, headColor, {0, 1}}
    };
}

std::vector<uint32_t> createBoxIndices(uint32_t baseVertex) {
    std::vector<uint32_t> idx;
    for (int face = 0; face < 6; face++) {
        uint32_t b = baseVertex + face * 4;
        idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3});
    }
    return idx;
}

struct Player {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float targetYaw = 0.0f;
    float moveSpeed = 10.0f;
    float turnSmoothSpeed = 10.0f;
    float height = 1.8f;
    float width = 0.6f;
    float jumpForce = 8.0f;
    float gravity = 20.0f;
    bool isGrounded = true;
};

struct ThirdPersonCamera {
    float yaw = 0.0f;
    float pitch = 25.0f;
    float distance = 8.0f;
    float heightOffset = 2.0f;
    float mouseSensitivity = 0.15f;
    float minPitch = -30.0f;
    float maxPitch = 60.0f;
    float smoothSpeed = 10.0f;
    glm::vec3 currentPosition;
    
    void processMouseInput(double deltaX, double deltaY) {
        yaw -= static_cast<float>(deltaX) * mouseSensitivity;
        pitch += static_cast<float>(deltaY) * mouseSensitivity;
        pitch = glm::clamp(pitch, minPitch, maxPitch);
        if (yaw < 0.0f) yaw += 360.0f;
        if (yaw > 360.0f) yaw -= 360.0f;
    }
    
    void adjustDistance(float delta) {
        distance = glm::clamp(distance - delta, 3.0f, 20.0f);
    }
    
    void update(const Player& player, float dt) {
        float horizontalDist = distance * cos(glm::radians(pitch));
        float verticalDist = distance * sin(glm::radians(pitch));
        
        glm::vec3 targetPos;
        targetPos.x = player.position.x - horizontalDist * sin(glm::radians(yaw));
        targetPos.z = player.position.z - horizontalDist * cos(glm::radians(yaw));
        targetPos.y = player.position.y + heightOffset + verticalDist;
        
        float t = 1.0f - exp(-smoothSpeed * dt);
        currentPosition = glm::mix(currentPosition, targetPos, t);
    }
    
    glm::vec3 getForward() const {
        return glm::normalize(glm::vec3(sin(glm::radians(yaw)), 0.0f, cos(glm::radians(yaw))));
    }
    
    glm::vec3 getRight() const {
        return glm::normalize(glm::cross(getForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    
    glm::mat4 getViewMatrix(const Player& player) const {
        glm::vec3 lookTarget = player.position + glm::vec3(0.0f, player.height * 0.6f, 0.0f);
        return glm::lookAt(currentPosition, lookTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

struct SceneObject {
    glm::vec3 position;
    glm::vec3 scale;
    float rotationY;
};

class Application {
public:
    void run() { initWindow(); initVulkan(); mainLoop(); cleanup(); }

private:
    GLFWwindow* m_window = nullptr;
    VulkanContext m_context;
    VulkanSwapchain m_swapchain;
    DescriptorManager m_descriptors;
    VulkanPipeline m_pipeline;
    
    // Terrain buffer (rebuilt when chunks change)
    VulkanBuffer m_terrainVertexBuffer;
    VulkanBuffer m_terrainIndexBuffer;
    uint32_t m_terrainIndexCount = 0;
    
    // Static geometry buffer
    VulkanBuffer m_staticVertexBuffer;
    VulkanBuffer m_staticIndexBuffer;
    uint32_t m_cubeIndexStart = 0, m_cubeIndexCount = 0, m_cubeVertexOffset = 0;
    uint32_t m_playerIndexStart = 0, m_playerIndexCount = 0, m_playerVertexOffset = 0;
    
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlight;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;
    
    Player m_player;
    ThirdPersonCamera m_camera;
    ChunkManager m_chunkManager;
    bool m_mouseCaptured = true;
    
    Timer m_timer;
    float m_logTimer = 0.0f;
    
    std::vector<SceneObject> m_landmarks;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        m_window = glfwCreateWindow(1280, 720, "Mythbreaker - Chunked World", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) {
            reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_framebufferResized = true;
        });
        glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double yoffset) {
            auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(w));
            app->m_camera.adjustDistance(static_cast<float>(yoffset) * 0.5f);
        });
        
        Input::instance().init(m_window);
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    void initVulkan() {
        Logger::info("=== MYTHBREAKER ENGINE ===");
        Logger::info("Version 0.1.0 - Milestone 5: Chunked World");
        m_context.init(m_window);
        m_swapchain.init(&m_context, m_window);
        m_descriptors.init(&m_context);
        m_pipeline.init(&m_context, &m_swapchain, &m_descriptors, "shaders/basic.vert.spv", "shaders/basic.frag.spv");
        
        m_camera.currentPosition = glm::vec3(0.0f, 5.0f, 10.0f);
        
        createStaticGeometry();
        createLandmarks();
        createSyncObjects();
        
        // Initial chunk load
        m_chunkManager.update(m_player.position);
        rebuildTerrainBuffer();
        
        Logger::info("Vulkan initialization complete");
        Logger::info("Controls: WASD move, Mouse look, Space jump, Scroll zoom, Tab mouse, ESC quit");
        Logger::infof("Chunk size: {}, Load radius: {}", m_chunkManager.chunkSize, m_chunkManager.loadRadius);
    }

    void createStaticGeometry() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        // Cube template
        auto cube = createCube(1.0f);
        m_cubeVertexOffset = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), cube.begin(), cube.end());
        auto cubeIdx = createBoxIndices(0);
        m_cubeIndexStart = static_cast<uint32_t>(indices.size());
        indices.insert(indices.end(), cubeIdx.begin(), cubeIdx.end());
        m_cubeIndexCount = static_cast<uint32_t>(cubeIdx.size());
        
        // Player mesh
        auto playerMesh = createPlayerMesh(m_player.width, m_player.height);
        m_playerVertexOffset = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), playerMesh.begin(), playerMesh.end());
        auto playerIdx = createBoxIndices(0);
        m_playerIndexStart = static_cast<uint32_t>(indices.size());
        indices.insert(indices.end(), playerIdx.begin(), playerIdx.end());
        m_playerIndexCount = static_cast<uint32_t>(playerIdx.size());
        
        VulkanBuffer::createWithStaging(&m_context, m_staticVertexBuffer, vertices.data(), 
            sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_staticIndexBuffer, indices.data(), 
            sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    void createLandmarks() {
        // Landmarks at regular intervals to show world scale
        for (int x = -50; x <= 50; x += 25) {
            for (int z = -50; z <= 50; z += 25) {
                if (x == 0 && z == 0) continue; // Skip origin
                float height = 1.0f + chunkRandom(x, z, 99) * 1.5f;
                m_landmarks.push_back({{static_cast<float>(x), height/2, static_cast<float>(z)}, 
                                       {1.5f, height, 1.5f}, 
                                       chunkRandom(x, z, 100) * 360.0f});
            }
        }
        Logger::infof("Created {} landmarks", m_landmarks.size());
    }

    void rebuildTerrainBuffer() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        m_chunkManager.buildMesh(vertices, indices);
        m_terrainIndexCount = static_cast<uint32_t>(indices.size());
        
        if (m_terrainIndexCount == 0) return;
        
        // Destroy old buffers
        m_terrainVertexBuffer.destroy();
        m_terrainIndexBuffer.destroy();
        
        // Create new buffers
        VulkanBuffer::createWithStaging(&m_context, m_terrainVertexBuffer, vertices.data(), 
            sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_terrainIndexBuffer, indices.data(), 
            sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        
        m_chunkManager.clearDirty();
    }

    void createSyncObjects() {
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_context.commandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        vkAllocateCommandBuffers(m_context.device(), &allocInfo, m_commandBuffers.data());
        
        m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinished.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);
        
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(m_context.device(), &semInfo, nullptr, &m_imageAvailable[i]);
            vkCreateSemaphore(m_context.device(), &semInfo, nullptr, &m_renderFinished[i]);
            vkCreateFence(m_context.device(), &fenceInfo, nullptr, &m_inFlight[i]);
        }
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            m_timer.tick();
            
            float dt = m_timer.clampedDeltaTime();
            processInput(dt);
            updatePlayer(dt);
            m_camera.update(m_player, dt);
            
            // Update chunks based on player position
            m_chunkManager.update(m_player.position);
            if (m_chunkManager.isMeshDirty()) {
                vkDeviceWaitIdle(m_context.device());
                rebuildTerrainBuffer();
            }
            
            Input::instance().update();
            drawFrame();
            
            m_logTimer += dt;
            if (m_logTimer >= 3.0f) {
                int chunkX = static_cast<int>(floor(m_player.position.x / m_chunkManager.chunkSize));
                int chunkZ = static_cast<int>(floor(m_player.position.z / m_chunkManager.chunkSize));
                Logger::infof("FPS: {:.1f} | Pos: ({:.1f}, {:.1f}, {:.1f}) | Chunk: ({}, {}) | Loaded: {}", 
                    m_timer.fps(), m_player.position.x, m_player.position.y, m_player.position.z,
                    chunkX, chunkZ, m_chunkManager.chunkCount());
                m_logTimer = 0.0f;
            }
        }
        vkDeviceWaitIdle(m_context.device());
    }

    void processInput(float dt) {
        auto& input = Input::instance();
        
        if (input.isKeyPressed(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(m_window, true);
            return;
        }
        
        if (input.isKeyPressed(GLFW_KEY_TAB)) {
            m_mouseCaptured = !m_mouseCaptured;
            glfwSetInputMode(m_window, GLFW_CURSOR, m_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
        
        if (m_mouseCaptured) {
            m_camera.processMouseInput(input.mouseDeltaX(), input.mouseDeltaY());
        }
        
        glm::vec3 moveDir(0.0f);
        glm::vec3 camForward = m_camera.getForward();
        glm::vec3 camRight = m_camera.getRight();
        
        if (input.isKeyDown(GLFW_KEY_W)) moveDir += camForward;
        if (input.isKeyDown(GLFW_KEY_S)) moveDir -= camForward;
        if (input.isKeyDown(GLFW_KEY_A)) moveDir -= camRight;
        if (input.isKeyDown(GLFW_KEY_D)) moveDir += camRight;
        
        // Sprint
        float speed = m_player.moveSpeed;
        if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT)) speed *= 2.0f;
        
        if (glm::length(moveDir) > 0.01f) {
            moveDir = glm::normalize(moveDir);
            m_player.velocity.x = moveDir.x * speed;
            m_player.velocity.z = moveDir.z * speed;
            m_player.targetYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
        } else {
            m_player.velocity.x *= 0.85f;
            m_player.velocity.z *= 0.85f;
        }
        
        if (input.isKeyPressed(GLFW_KEY_SPACE) && m_player.isGrounded) {
            m_player.velocity.y = m_player.jumpForce;
            m_player.isGrounded = false;
        }
    }

    void updatePlayer(float dt) {
        // Smooth rotation
        float yawDiff = m_player.targetYaw - m_player.yaw;
        if (yawDiff > 180.0f) yawDiff -= 360.0f;
        if (yawDiff < -180.0f) yawDiff += 360.0f;
        m_player.yaw += yawDiff * m_player.turnSmoothSpeed * dt;
        if (m_player.yaw < 0.0f) m_player.yaw += 360.0f;
        if (m_player.yaw > 360.0f) m_player.yaw -= 360.0f;
        
        // Gravity
        if (!m_player.isGrounded) {
            m_player.velocity.y -= m_player.gravity * dt;
        }
        
        // Apply velocity
        m_player.position += m_player.velocity * dt;
        
        // Ground collision (sample terrain height at player position - simplified)
        float groundHeight = 0.0f; // Could sample chunk height here
        if (m_player.position.y <= groundHeight) {
            m_player.position.y = groundHeight;
            m_player.velocity.y = 0.0f;
            m_player.isGrounded = true;
        }
    }

    void drawFrame() {
        vkWaitForFences(m_context.device(), 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
        
        uint32_t imageIndex;
        if (!m_swapchain.acquireNextImage(imageIndex, m_imageAvailable[m_currentFrame])) {
            recreateSwapchain();
            return;
        }
        
        vkResetFences(m_context.device(), 1, &m_inFlight[m_currentFrame]);
        updateCamera();
        recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);
        
        VkSemaphore waitSemaphores[] = {m_imageAvailable[m_currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {m_renderFinished[m_currentFrame]};
        
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        
        vkQueueSubmit(m_context.graphicsQueue(), 1, &submitInfo, m_inFlight[m_currentFrame]);
        
        if (!m_swapchain.present(imageIndex, m_renderFinished[m_currentFrame]) || m_framebufferResized) {
            m_framebufferResized = false;
            recreateSwapchain();
        }
        
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void updateCamera() {
        auto extent = m_swapchain.extent();
        CameraUBO ubo{};
        ubo.view = m_camera.getViewMatrix(m_player);
        ubo.proj = glm::perspective(glm::radians(60.0f), float(extent.width) / float(extent.height), 0.1f, 500.0f);
        ubo.proj[1][1] *= -1;
        ubo.viewProj = ubo.proj * ubo.view;
        ubo.cameraPos = m_camera.currentPosition;
        ubo.time = m_timer.totalTime();
        m_descriptors.updateCameraUBO(m_currentFrame, ubo);
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
        vkResetCommandBuffer(cmd, 0);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);
        
        auto extent = m_swapchain.extent();
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = m_swapchain.renderPass();
        rpInfo.framebuffer = m_swapchain.framebuffer(imageIndex);
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = extent;
        
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.02f, 0.02f, 0.05f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpInfo.pClearValues = clearValues.data();
        
        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline());
        
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        
        VkDescriptorSet descSet = m_descriptors.descriptorSet(m_currentFrame);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipelineLayout(), 0, 1, &descSet, 0, nullptr);
        
        PushConstants push{};
        
        // Draw terrain chunks
        if (m_terrainIndexCount > 0) {
            VkBuffer terrainBuffers[] = {m_terrainVertexBuffer.buffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, terrainBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, m_terrainIndexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);
            
            push.model = glm::mat4(1.0f);
            vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
            vkCmdDrawIndexed(cmd, m_terrainIndexCount, 1, 0, 0, 0);
        }
        
        // Draw static geometry (landmarks + player)
        VkBuffer staticBuffers[] = {m_staticVertexBuffer.buffer()};
        VkDeviceSize staticOffsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, staticBuffers, staticOffsets);
        vkCmdBindIndexBuffer(cmd, m_staticIndexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);
        
        // Draw landmarks
        for (const auto& obj : m_landmarks) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position);
            model = glm::rotate(model, glm::radians(obj.rotationY), glm::vec3(0, 1, 0));
            model = glm::scale(model, obj.scale);
            push.model = model;
            vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
            vkCmdDrawIndexed(cmd, m_cubeIndexCount, 1, m_cubeIndexStart, m_cubeVertexOffset, 0);
        }
        
        // Draw player
        glm::mat4 playerModel = glm::translate(glm::mat4(1.0f), m_player.position);
        playerModel = glm::rotate(playerModel, glm::radians(m_player.yaw), glm::vec3(0, 1, 0));
        push.model = playerModel;
        vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
        vkCmdDrawIndexed(cmd, m_playerIndexCount, 1, m_playerIndexStart, m_playerVertexOffset, 0);
        
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    void recreateSwapchain() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        while (w == 0 || h == 0) { glfwGetFramebufferSize(m_window, &w, &h); glfwWaitEvents(); }
        vkDeviceWaitIdle(m_context.device());
        m_swapchain.recreate();
    }

    void cleanup() {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_context.device(), m_imageAvailable[i], nullptr);
            vkDestroySemaphore(m_context.device(), m_renderFinished[i], nullptr);
            vkDestroyFence(m_context.device(), m_inFlight[i], nullptr);
        }
        m_terrainIndexBuffer.destroy();
        m_terrainVertexBuffer.destroy();
        m_staticIndexBuffer.destroy();
        m_staticVertexBuffer.destroy();
        m_pipeline.destroy();
        m_descriptors.destroy();
        m_swapchain.destroy();
        m_context.destroy();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
};

int main() {
    try {
        Application app;
        app.run();
    } catch (const std::exception& e) {
        Logger::fatal(e.what());
        return 1;
    }
    return 0;
}
