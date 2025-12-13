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

// Ground plane (large, dark)
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

// Cube vertices with unique colors per face
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

std::vector<uint32_t> createCubeIndices(uint32_t baseVertex) {
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
    
    // Camera
    glm::vec3 m_cameraPos = {0.0f, 2.0f, 8.0f};
    float m_yaw = -90.0f;
    float m_pitch = -10.0f;
    
    Timer m_timer;
    float m_logTimer = 0.0f;
    
    // Scene
    std::vector<SceneObject> m_objects;
    uint32_t m_groundIndexStart = 0;
    uint32_t m_groundIndexCount = 0;
    uint32_t m_cubeIndexStart = 0;
    uint32_t m_cubeIndexCount = 0;
    uint32_t m_cubeVertexOffset = 0;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        m_window = glfwCreateWindow(1280, 720, "Mythbreaker - Milestone 3", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) {
            reinterpret_cast<Application*>(glfwGetWindowUserPointer(w))->m_framebufferResized = true;
        });
        Input::instance().init(m_window);
    }

    void initVulkan() {
        Logger::info("=== MYTHBREAKER ENGINE ===");
        Logger::info("Version 0.1.0 - Milestone 3: 3D Proof");
        m_context.init(m_window);
        m_swapchain.init(&m_context, m_window);
        m_descriptors.init(&m_context);
        m_pipeline.init(&m_context, &m_swapchain, &m_descriptors, "shaders/basic.vert.spv", "shaders/basic.frag.spv");
        
        createScene();
        createSyncObjects();
        
        Logger::info("Vulkan initialization complete");
        Logger::info("Controls: WASD move, Q/E rotate, Space/Shift up/down, ESC quit");
    }

    void createScene() {
        // Build vertex and index arrays
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        // Ground plane
        auto ground = createGroundPlane(50.0f, 0.0f);
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
        auto cubeIdx = createCubeIndices(0); // Will use vertex offset when drawing
        m_cubeIndexStart = static_cast<uint32_t>(indices.size());
        indices.insert(indices.end(), cubeIdx.begin(), cubeIdx.end());
        m_cubeIndexCount = static_cast<uint32_t>(cubeIdx.size());
        
        // Create scene objects - cubes at various positions to show depth
        m_objects = {
            // Near cubes (should move fast when camera moves)
            {{-2.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 1.0f}, 0.0f},
            {{ 2.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 1.0f}, 45.0f},
            
            // Mid-distance cubes
            {{-4.0f, 0.5f, -2.0f}, {1.2f, 1.2f, 1.2f}, 30.0f},
            {{ 0.0f, 1.0f, -3.0f}, {2.0f, 2.0f, 2.0f}, 15.0f},
            {{ 4.0f, 0.5f, -2.0f}, {1.2f, 1.2f, 1.2f}, -30.0f},
            
            // Far cubes (should move slowly when camera moves)
            {{-6.0f, 0.75f, -8.0f}, {1.5f, 1.5f, 1.5f}, 60.0f},
            {{ 0.0f, 0.5f, -10.0f}, {1.0f, 1.0f, 1.0f}, 0.0f},
            {{ 6.0f, 0.75f, -8.0f}, {1.5f, 1.5f, 1.5f}, -60.0f},
            
            // Very far cubes
            {{-10.0f, 1.0f, -15.0f}, {2.0f, 2.0f, 2.0f}, 45.0f},
            {{ 10.0f, 1.0f, -15.0f}, {2.0f, 2.0f, 2.0f}, -45.0f},
            
            // Stacked cubes to show vertical depth
            {{ 0.0f, 0.5f, 5.0f}, {1.0f, 1.0f, 1.0f}, 0.0f},
            {{ 0.0f, 1.6f, 5.0f}, {0.8f, 0.8f, 0.8f}, 45.0f},
            {{ 0.0f, 2.5f, 5.0f}, {0.6f, 0.6f, 0.6f}, 22.5f},
        };
        
        // Create GPU buffers
        VulkanBuffer::createWithStaging(&m_context, m_vertexBuffer, vertices.data(), 
            sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VulkanBuffer::createWithStaging(&m_context, m_indexBuffer, indices.data(), 
            sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        
        Logger::infof("Scene created: {} vertices, {} indices, {} objects", 
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
            processInput();
            drawFrame();
            
            m_logTimer += m_timer.deltaTime();
            if (m_logTimer >= 2.0f) {
                Logger::infof("FPS: {:.1f} | Pos: ({:.2f}, {:.2f}, {:.2f}) | Yaw: {:.1f}", 
                    m_timer.fps(), m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, m_yaw);
                m_logTimer = 0.0f;
            }
        }
        vkDeviceWaitIdle(m_context.device());
    }

    void processInput() {
        auto& input = Input::instance();
        float dt = m_timer.clampedDeltaTime();
        float speed = 5.0f * dt;
        float rotSpeed = 90.0f * dt;
        
        if (input.isKeyDown(GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(m_window, true);
        
        // Camera direction
        glm::vec3 forward;
        forward.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        forward.y = sin(glm::radians(m_pitch));
        forward.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        forward = glm::normalize(forward);
        
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        
        // Movement
        if (input.isKeyDown(GLFW_KEY_W)) m_cameraPos += forward * speed;
        if (input.isKeyDown(GLFW_KEY_S)) m_cameraPos -= forward * speed;
        if (input.isKeyDown(GLFW_KEY_A)) m_cameraPos -= right * speed;
        if (input.isKeyDown(GLFW_KEY_D)) m_cameraPos += right * speed;
        if (input.isKeyDown(GLFW_KEY_SPACE)) m_cameraPos.y += speed;
        if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT)) m_cameraPos.y -= speed;
        
        // Rotation
        if (input.isKeyDown(GLFW_KEY_Q)) m_yaw -= rotSpeed;
        if (input.isKeyDown(GLFW_KEY_E)) m_yaw += rotSpeed;
        if (input.isKeyDown(GLFW_KEY_R)) m_pitch += rotSpeed * 0.5f;
        if (input.isKeyDown(GLFW_KEY_F)) m_pitch -= rotSpeed * 0.5f;
        
        // Clamp pitch
        m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
    }

    void drawFrame() {
        vkWaitForFences(m_context.device(), 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
        
        uint32_t imageIndex;
        if (!m_swapchain.acquireNextImage(imageIndex, m_imageAvailable[m_currentFrame])) {
            recreateSwapchain();
            return;
        }
        
        vkResetFences(m_context.device(), 1, &m_inFlight[m_currentFrame]);
        
        // Update camera UBO
        updateCamera();
        
        // Record commands
        recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);
        
        // Submit
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
        
        // Present
        if (!m_swapchain.present(imageIndex, m_renderFinished[m_currentFrame]) || m_framebufferResized) {
            m_framebufferResized = false;
            recreateSwapchain();
        }
        
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void updateCamera() {
        glm::vec3 forward;
        forward.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        forward.y = sin(glm::radians(m_pitch));
        forward.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        forward = glm::normalize(forward);
        
        auto extent = m_swapchain.extent();
        CameraUBO ubo{};
        ubo.view = glm::lookAt(m_cameraPos, m_cameraPos + forward, glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.proj = glm::perspective(glm::radians(60.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f);
        ubo.proj[1][1] *= -1; // Flip Y for Vulkan
        ubo.viewProj = ubo.proj * ubo.view;
        ubo.cameraPos = m_cameraPos;
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
        
        // Set viewport and scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        
        // Bind descriptor set
        VkDescriptorSet descSet = m_descriptors.descriptorSet(m_currentFrame);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipelineLayout(), 0, 1, &descSet, 0, nullptr);
        
        // Bind vertex and index buffers
        VkBuffer vertexBuffers[] = {m_vertexBuffer.buffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, m_indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);
        
        // Draw ground plane
        PushConstants push{};
        push.model = glm::mat4(1.0f);
        vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
        vkCmdDrawIndexed(cmd, m_groundIndexCount, 1, m_groundIndexStart, 0, 0);
        
        // Draw cubes
        for (const auto& obj : m_objects) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, obj.position);
            model = glm::rotate(model, glm::radians(obj.rotationY), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, obj.scale);
            
            push.model = model;
            vkCmdPushConstants(cmd, m_pipeline.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
            vkCmdDrawIndexed(cmd, m_cubeIndexCount, 1, m_cubeIndexStart, m_cubeVertexOffset, 0);
        }
        
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    void recreateSwapchain() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(m_window, &w, &h);
            glfwWaitEvents();
        }
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
