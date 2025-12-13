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

using namespace myth;
using namespace myth::vk;

// Ground plane
std::vector<Vertex> createGroundPlane(float size, float y) {
    float h = size / 2.0f;
    glm::vec3 color = {0.15f, 0.12f, 0.1f};
    return {
        {{-h, y, -h}, color, {0, 0}},
        {{ h, y, -h}, color, {1, 0}},
        {{ h, y,  h}, color, {1, 1}},
        {{-h, y,  h}, color, {0, 1}}
    };
}

// Cube vertices
std::vector<Vertex> createCube(float size) {
    float s = size / 2.0f;
    return {
        // Front (red)
        {{-s, -s,  s}, {0.8f, 0.2f, 0.2f}, {0, 0}},
        {{ s, -s,  s}, {0.8f, 0.2f, 0.2f}, {1, 0}},
        {{ s,  s,  s}, {0.8f, 0.2f, 0.2f}, {1, 1}},
        {{-s,  s,  s}, {0.8f, 0.2f, 0.2f}, {0, 1}},
        // Back (green)
        {{ s, -s, -s}, {0.2f, 0.8f, 0.2f}, {0, 0}},
        {{-s, -s, -s}, {0.2f, 0.8f, 0.2f}, {1, 0}},
        {{-s,  s, -s}, {0.2f, 0.8f, 0.2f}, {1, 1}},
        {{ s,  s, -s}, {0.2f, 0.8f, 0.2f}, {0, 1}},
        // Top (blue)
        {{-s,  s,  s}, {0.2f, 0.2f, 0.8f}, {0, 0}},
        {{ s,  s,  s}, {0.2f, 0.2f, 0.8f}, {1, 0}},
        {{ s,  s, -s}, {0.2f, 0.2f, 0.8f}, {1, 1}},
        {{-s,  s, -s}, {0.2f, 0.2f, 0.8f}, {0, 1}},
        // Bottom (yellow)
        {{-s, -s, -s}, {0.8f, 0.8f, 0.2f}, {0, 0}},
        {{ s, -s, -s}, {0.8f, 0.8f, 0.2f}, {1, 0}},
        {{ s, -s,  s}, {0.8f, 0.8f, 0.2f}, {1, 1}},
        {{-s, -s,  s}, {0.8f, 0.8f, 0.2f}, {0, 1}},
        // Right (cyan)
        {{ s, -s,  s}, {0.2f, 0.8f, 0.8f}, {0, 0}},
        {{ s, -s, -s}, {0.2f, 0.8f, 0.8f}, {1, 0}},
        {{ s,  s, -s}, {0.2f, 0.8f, 0.8f}, {1, 1}},
        {{ s,  s,  s}, {0.2f, 0.8f, 0.8f}, {0, 1}},
        // Left (magenta)
        {{-s, -s, -s}, {0.8f, 0.2f, 0.8f}, {0, 0}},
        {{-s, -s,  s}, {0.8f, 0.2f, 0.8f}, {1, 0}},
        {{-s,  s,  s}, {0.8f, 0.2f, 0.8f}, {1, 1}},
        {{-s,  s, -s}, {0.8f, 0.2f, 0.8f}, {0, 1}}
    };
}

// Player capsule (simplified as a tall box with distinct colors)
std::vector<Vertex> createPlayerMesh(float width, float height) {
    float w = width / 2.0f;
    float h = height;
    glm::vec3 bodyColor = {0.9f, 0.7f, 0.3f};  // Golden
    glm::vec3 headColor = {0.95f, 0.8f, 0.6f}; // Lighter for top
    return {
        // Front
        {{-w, 0,  w}, bodyColor, {0, 0}},
        {{ w, 0,  w}, bodyColor, {1, 0}},
        {{ w, h,  w}, headColor, {1, 1}},
        {{-w, h,  w}, headColor, {0, 1}},
        // Back
        {{ w, 0, -w}, bodyColor, {0, 0}},
        {{-w, 0, -w}, bodyColor, {1, 0}},
        {{-w, h, -w}, headColor, {1, 1}},
        {{ w, h, -w}, headColor, {0, 1}},
        // Top
        {{-w, h,  w}, headColor, {0, 0}},
        {{ w, h,  w}, headColor, {1, 0}},
        {{ w, h, -w}, headColor, {1, 1}},
        {{-w, h, -w}, headColor, {0, 1}},
        // Bottom
        {{-w, 0, -w}, bodyColor, {0, 0}},
        {{ w, 0, -w}, bodyColor, {1, 0}},
        {{ w, 0,  w}, bodyColor, {1, 1}},
        {{-w, 0,  w}, bodyColor, {0, 1}},
        // Right
        {{ w, 0,  w}, bodyColor, {0, 0}},
        {{ w, 0, -w}, bodyColor, {1, 0}},
        {{ w, h, -w}, headColor, {1, 1}},
        {{ w, h,  w}, headColor, {0, 1}},
        // Left
        {{-w, 0, -w}, bodyColor, {0, 0}},
        {{-w, 0,  w}, bodyColor, {1, 0}},
        {{-w, h,  w}, headColor, {1, 1}},
        {{-w, h, -w}, headColor, {0, 1}}
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

std::vector<uint32_t> createQuadIndices(uint32_t baseVertex) {
    return {baseVertex, baseVertex+1, baseVertex+2, baseVertex, baseVertex+2, baseVertex+3};
}

// Player controller
struct Player {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;           // Direction player is facing
    float moveSpeed = 8.0f;
    float turnSpeed = 180.0f;
    float height = 1.8f;
    float width = 0.6f;
    
    glm::vec3 getForward() const {
        return glm::vec3(sin(glm::radians(yaw)), 0.0f, cos(glm::radians(yaw)));
    }
    
    glm::vec3 getRight() const {
        return glm::vec3(cos(glm::radians(yaw)), 0.0f, -sin(glm::radians(yaw)));
    }
};

// Third-person camera
struct ThirdPersonCamera {
    float distance = 6.0f;       // Distance behind player
    float heightOffset = 2.5f;   // Height above player
    float pitch = 15.0f;         // Looking down angle
    float yawOffset = 0.0f;      // Orbit offset from player's yaw
    float smoothSpeed = 8.0f;    // Camera lag smoothing
    
    glm::vec3 currentPosition = {0.0f, 3.0f, 6.0f};
    glm::vec3 targetPosition = {0.0f, 3.0f, 6.0f};
    
    void update(const Player& player, float dt) {
        // Calculate ideal camera position
        float totalYaw = player.yaw + yawOffset;
        float horizontalDist = distance * cos(glm::radians(pitch));
        float verticalDist = distance * sin(glm::radians(pitch));
        
        targetPosition.x = player.position.x - horizontalDist * sin(glm::radians(totalYaw));
        targetPosition.z = player.position.z - horizontalDist * cos(glm::radians(totalYaw));
        targetPosition.y = player.position.y + heightOffset + verticalDist;
        
        // Smooth follow
        float t = 1.0f - exp(-smoothSpeed * dt);
        currentPosition = glm::mix(currentPosition, targetPosition, t);
    }
    
    glm::mat4 getViewMatrix(const Player& player) const {
        glm::vec3 lookTarget = player.position + glm::vec3(0.0f, player.height * 0.7f, 0.0f);
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
    void run() { 
        initWindow(); 
        initVulkan(); 
        mainLoop(); 
        cleanup(); 
    }

private:
    GLFWwindow* m_window = nullptr;
    VulkanContext m_context;
    VulkanSwapchain m_swapchain;
    DescriptorManager m_descriptors;
    VulkanPipeline m_pipeline;
    VulkanBuffer m_vertexBuffer;
    VulkanBuffer m_indexBuffer;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlight;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;
    
    // Player and camera
    Player m_player;
    ThirdPersonCamera m_camera;
    
    Timer m_timer;
    float m_logTimer = 0.0f;
    
    // Scene geometry
    std::vector<SceneObject> m_objects;
    uint32_t m_groundIndexStart = 0, m_groundIndexCount = 0;
    uint32_t m_cubeIndexStart = 0, m_cubeIndexCount = 0, m_cubeVertexOffset = 0;
    uint32_t m_playerIndexStart = 0, m_playerIndexCount = 0, m_playerVertexOffset = 0;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        m_window = glfwCreateWindow(1280, 720, "Mythbreaker - Third Person", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) {
            reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_framebufferResized = true;
        });
        Input::instance().init(m_window);
    }

    void initVulkan() {
        Logger::info("=== MYTHBREAKER ENGINE ===");
        Logger::info("Version 0.1.0 - Milestone 4: Third-Person Controller");
        m_context.init(m_window);
        m_swapchain.init(&m_context, m_window);
        m_descriptors.init(&m_context);
        m_pipeline.init(&m_context, &m_swapchain, &m_descriptors, "shaders/basic.vert.spv", "shaders/basic.frag.spv");
        
        createScene();
        createSyncObjects();
        
        Logger::info("Vulkan initialization complete");
        Logger::info("Controls: WASD move player, Q/E orbit camera, R/F adjust pitch, ESC quit");
    }

    void createScene() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        // Ground plane
        auto ground = createGroundPlane(100.0f, 0.0f);
        uint32_t groundBase = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), ground.begin(), ground.end());
        auto groundIdx = createQuadIndices(groundBase);
        m_groundIndexStart = static_cast<uint32_t>(indices.size());
        indices.insert(indices.end(), groundIdx.begin(), groundIdx.end());
        m_groundIndexCount = static_cast<uint32_t>(groundIdx.size());
        
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
        
        // Scene objects - scattered around the world
        m_objects = {
            // Near spawn
            {{5.0f, 0.5f, 5.0f}, {1.0f, 1.0f, 1.0f}, 0.0f},
            {{-5.0f, 0.5f, 5.0f}, {1.0f, 1.0f, 1.0f}, 45.0f},
            {{5.0f, 0.5f, -5.0f}, {1.0f, 1.0f, 1.0f}, -45.0f},
            {{-5.0f, 0.5f, -5.0f}, {1.0f, 1.0f, 1.0f}, 30.0f},
            
            // Mid-range
            {{10.0f, 0.75f, 0.0f}, {1.5f, 1.5f, 1.5f}, 15.0f},
            {{-10.0f, 0.75f, 0.0f}, {1.5f, 1.5f, 1.5f}, -15.0f},
            {{0.0f, 0.75f, 10.0f}, {1.5f, 1.5f, 1.5f}, 60.0f},
            {{0.0f, 0.75f, -10.0f}, {1.5f, 1.5f, 1.5f}, -60.0f},
            
            // Far landmarks
            {{20.0f, 1.0f, 20.0f}, {2.0f, 2.0f, 2.0f}, 45.0f},
            {{-20.0f, 1.0f, 20.0f}, {2.0f, 2.0f, 2.0f}, -45.0f},
            {{20.0f, 1.0f, -20.0f}, {2.0f, 2.0f, 2.0f}, 22.5f},
            {{-20.0f, 1.0f, -20.0f}, {2.0f, 2.0f, 2.0f}, -22.5f},
            
            // Stacked tower
            {{15.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0.0f},
            {{15.0f, 1.6f, 0.0f}, {0.8f, 0.8f, 0.8f}, 45.0f},
            {{15.0f, 2.5f, 0.0f}, {0.6f, 0.6f, 0.6f}, 22.5f},
            {{15.0f, 3.2f, 0.0f}, {0.4f, 0.4f, 0.4f}, 67.5f},
        };
        
        VulkanBuffer::createWithStaging(&m_context, m_vertexBuffer, vertices.data(), 
            sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_indexBuffer, indices.data(), 
            sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        
        Logger::infof("Scene: {} vertices, {} indices, {} objects + player", 
            vertices.size(), indices.size(), m_objects.size());
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
            Input::instance().update();
            
            float dt = m_timer.clampedDeltaTime();
            processInput(dt);
            updatePlayer(dt);
            m_camera.update(m_player, dt);
            
            drawFrame();
            
            m_logTimer += dt;
            if (m_logTimer >= 2.0f) {
                Logger::infof("FPS: {:.1f} | Player: ({:.1f}, {:.1f}, {:.1f}) | Yaw: {:.0f}", 
                    m_timer.fps(), m_player.position.x, m_player.position.y, m_player.position.z, m_player.yaw);
                m_logTimer = 0.0f;
            }
        }
        vkDeviceWaitIdle(m_context.device());
    }

    void processInput(float dt) {
        auto& input = Input::instance();
        
        if (input.isKeyDown(GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(m_window, true);
        
        // Player movement (relative to player facing)
        glm::vec3 moveDir(0.0f);
        if (input.isKeyDown(GLFW_KEY_W)) moveDir += m_player.getForward();
        if (input.isKeyDown(GLFW_KEY_S)) moveDir -= m_player.getForward();
        if (input.isKeyDown(GLFW_KEY_A)) moveDir -= m_player.getRight();
        if (input.isKeyDown(GLFW_KEY_D)) moveDir += m_player.getRight();
        
        if (glm::length(moveDir) > 0.01f) {
            moveDir = glm::normalize(moveDir);
            m_player.velocity.x = moveDir.x * m_player.moveSpeed;
            m_player.velocity.z = moveDir.z * m_player.moveSpeed;
        } else {
            // Friction
            m_player.velocity.x *= 0.9f;
            m_player.velocity.z *= 0.9f;
        }
        
        // Player rotation (turn left/right)
        if (input.isKeyDown(GLFW_KEY_LEFT)) m_player.yaw += m_player.turnSpeed * dt;
        if (input.isKeyDown(GLFW_KEY_RIGHT)) m_player.yaw -= m_player.turnSpeed * dt;
        
        // Camera orbit
        float orbitSpeed = 90.0f * dt;
        if (input.isKeyDown(GLFW_KEY_Q)) m_camera.yawOffset += orbitSpeed;
        if (input.isKeyDown(GLFW_KEY_E)) m_camera.yawOffset -= orbitSpeed;
        
        // Camera pitch
        float pitchSpeed = 45.0f * dt;
        if (input.isKeyDown(GLFW_KEY_R)) m_camera.pitch = glm::min(m_camera.pitch + pitchSpeed, 60.0f);
        if (input.isKeyDown(GLFW_KEY_F)) m_camera.pitch = glm::max(m_camera.pitch - pitchSpeed, -10.0f);
        
        // Camera distance
        if (input.isKeyDown(GLFW_KEY_Z)) m_camera.distance = glm::max(m_camera.distance - 5.0f * dt, 2.0f);
        if (input.isKeyDown(GLFW_KEY_X)) m_camera.distance = glm::min(m_camera.distance + 5.0f * dt, 20.0f);
    }

    void updatePlayer(float dt) {
        // Apply velocity
        m_player.position += m_player.velocity * dt;
        
        // Ground collision (simple - keep on y=0 plane)
        m_player.position.y = 0.0f;
        
        // Keep yaw in reasonable range
        if (m_player.yaw > 360.0f) m_player.yaw -= 360.0f;
        if (m_player.yaw < 0.0f) m_player.yaw += 360.0f;
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
        ubo.proj = glm::perspective(glm::radians(60.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f);
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
        
        VkBuffer vertexBuffers[] = {m_vertexBuffer.buffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, m_indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);
        
        PushConstants push{};
        
        // Draw ground
        push.model = glm::mat4(1.0f);
        vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
        vkCmdDrawIndexed(cmd, m_groundIndexCount, 1, m_groundIndexStart, 0, 0);
        
        // Draw scene cubes
        for (const auto& obj : m_objects) {
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
        m_indexBuffer.destroy();
        m_vertexBuffer.destroy();
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
