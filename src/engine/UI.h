#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace myth {

// UI vertex for 2D rendering
struct UIVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(UIVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }
    
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attrs(3);
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = offsetof(UIVertex, position);
        
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = offsetof(UIVertex, uv);
        
        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[2].offset = offsetof(UIVertex, color);
        return attrs;
    }
};

// Simple UI quad
struct UIQuad {
    glm::vec2 position;  // Screen position (pixels from top-left)
    glm::vec2 size;      // Size in pixels
    glm::vec4 color;     // RGBA color
    float cornerRadius = 0.0f;
};

// UI bar (health, stamina, etc.)
struct UIBar {
    glm::vec2 position;
    glm::vec2 size;
    float fillPercent;   // 0.0 to 1.0
    glm::vec4 fillColor;
    glm::vec4 bgColor;
    glm::vec4 borderColor;
    float borderWidth = 2.0f;
};

// UI text (for future bitmap font rendering)
struct UIText {
    std::string text;
    glm::vec2 position;
    glm::vec4 color;
    float scale = 1.0f;
};

// HUD state
struct HUDState {
    // Player stats
    float healthPercent = 1.0f;
    float staminaPercent = 1.0f;
    float manaPercent = 1.0f;
    bool isExhausted = false;
    bool isDead = false;
    
    // Interaction
    bool showInteractPrompt = false;
    std::string interactText = "Press F to interact";
    std::string nearbyNPCName;
    
    // Dialogue
    bool showDialogue = false;
    std::string dialogueSpeaker;
    std::string dialogueText;
    float dialogueTimer = 0.0f;
    
    // Combat feedback
    bool showDamageFlash = false;
    float damageFlashTimer = 0.0f;
    
    // Crosshair
    bool showCrosshair = true;
};

// UI renderer class
class UIRenderer {
public:
    void init(VkDevice device, VkPhysicalDevice physicalDevice, 
              VkRenderPass renderPass, VkDescriptorSetLayout descriptorLayout,
              uint32_t width, uint32_t height) {
        m_device = device;
        m_screenWidth = width;
        m_screenHeight = height;
        
        createPipeline(renderPass, descriptorLayout);
        createBuffers(physicalDevice);
    }
    
    void cleanup() {
        if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_vertexBuffer) vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        if (m_vertexMemory) vkFreeMemory(m_device, m_vertexMemory, nullptr);
    }
    
    void resize(uint32_t width, uint32_t height) {
        m_screenWidth = width;
        m_screenHeight = height;
    }
    
    void beginFrame() {
        m_vertices.clear();
    }
    
    void drawQuad(const UIQuad& quad) {
        // Convert pixel coordinates to NDC (-1 to 1)
        float x1 = (quad.position.x / m_screenWidth) * 2.0f - 1.0f;
        float y1 = (quad.position.y / m_screenHeight) * 2.0f - 1.0f;
        float x2 = ((quad.position.x + quad.size.x) / m_screenWidth) * 2.0f - 1.0f;
        float y2 = ((quad.position.y + quad.size.y) / m_screenHeight) * 2.0f - 1.0f;
        
        // Two triangles for quad
        m_vertices.push_back({{x1, y1}, {0, 0}, quad.color});
        m_vertices.push_back({{x2, y1}, {1, 0}, quad.color});
        m_vertices.push_back({{x2, y2}, {1, 1}, quad.color});
        
        m_vertices.push_back({{x1, y1}, {0, 0}, quad.color});
        m_vertices.push_back({{x2, y2}, {1, 1}, quad.color});
        m_vertices.push_back({{x1, y2}, {0, 1}, quad.color});
    }
    
    void drawBar(const UIBar& bar) {
        // Background
        drawQuad({bar.position, bar.size, bar.bgColor});
        
        // Fill
        if (bar.fillPercent > 0.0f) {
            glm::vec2 fillSize = {bar.size.x * bar.fillPercent, bar.size.y};
            drawQuad({bar.position, fillSize, bar.fillColor});
        }
        
        // Border (4 quads)
        float bw = bar.borderWidth;
        // Top
        drawQuad({{bar.position.x - bw, bar.position.y - bw}, {bar.size.x + bw*2, bw}, bar.borderColor});
        // Bottom
        drawQuad({{bar.position.x - bw, bar.position.y + bar.size.y}, {bar.size.x + bw*2, bw}, bar.borderColor});
        // Left
        drawQuad({{bar.position.x - bw, bar.position.y}, {bw, bar.size.y}, bar.borderColor});
        // Right
        drawQuad({{bar.position.x + bar.size.x, bar.position.y}, {bw, bar.size.y}, bar.borderColor});
    }
    
    void drawCrosshair(glm::vec4 color = {1, 1, 1, 0.7f}) {
        float cx = m_screenWidth / 2.0f;
        float cy = m_screenHeight / 2.0f;
        float size = 10.0f;
        float thickness = 2.0f;
        
        // Horizontal line
        drawQuad({{cx - size, cy - thickness/2}, {size * 2, thickness}, color});
        // Vertical line
        drawQuad({{cx - thickness/2, cy - size}, {thickness, size * 2}, color});
    }
    
    void drawDialogueBox(const std::string& speaker, const std::string& text) {
        // Box at bottom of screen
        float boxHeight = 120.0f;
        float boxMargin = 20.0f;
        float boxWidth = m_screenWidth - boxMargin * 2;
        
        // Semi-transparent background
        drawQuad({{boxMargin, m_screenHeight - boxHeight - boxMargin}, 
                  {boxWidth, boxHeight}, 
                  {0.0f, 0.0f, 0.0f, 0.75f}});
        
        // Border
        float bw = 3.0f;
        glm::vec4 borderColor = {0.6f, 0.5f, 0.3f, 1.0f};
        drawQuad({{boxMargin, m_screenHeight - boxHeight - boxMargin - bw}, {boxWidth, bw}, borderColor});
        drawQuad({{boxMargin, m_screenHeight - boxMargin}, {boxWidth, bw}, borderColor});
        drawQuad({{boxMargin - bw, m_screenHeight - boxHeight - boxMargin}, {bw, boxHeight}, borderColor});
        drawQuad({{boxMargin + boxWidth, m_screenHeight - boxHeight - boxMargin}, {bw, boxHeight}, borderColor});
        
        // Speaker name indicator (colored bar)
        drawQuad({{boxMargin + 10, m_screenHeight - boxHeight - boxMargin + 10}, 
                  {150, 25}, 
                  {0.3f, 0.5f, 0.7f, 0.9f}});
    }
    
    void drawInteractPrompt() {
        // Small prompt above crosshair
        float promptWidth = 180.0f;
        float promptHeight = 30.0f;
        float cx = (m_screenWidth - promptWidth) / 2.0f;
        float cy = m_screenHeight / 2.0f - 60.0f;
        
        // Background
        drawQuad({{cx, cy}, {promptWidth, promptHeight}, {0, 0, 0, 0.6f}});
        
        // Border
        drawQuad({{cx, cy - 2}, {promptWidth, 2}, {0.5f, 0.7f, 0.9f, 1.0f}});
        drawQuad({{cx, cy + promptHeight}, {promptWidth, 2}, {0.5f, 0.7f, 0.9f, 1.0f}});
    }
    
    void drawDamageFlash(float intensity) {
        // Red vignette overlay
        glm::vec4 flashColor = {0.8f, 0.1f, 0.1f, intensity * 0.4f};
        
        // Edges of screen
        float edgeSize = 100.0f;
        // Top
        drawQuad({{0, 0}, {(float)m_screenWidth, edgeSize}, flashColor});
        // Bottom
        drawQuad({{0, m_screenHeight - edgeSize}, {(float)m_screenWidth, edgeSize}, flashColor});
        // Left
        drawQuad({{0, 0}, {edgeSize, (float)m_screenHeight}, flashColor});
        // Right
        drawQuad({{m_screenWidth - edgeSize, 0}, {edgeSize, (float)m_screenHeight}, flashColor});
    }
    
    void drawDeathOverlay() {
        // Dark overlay
        drawQuad({{0, 0}, {(float)m_screenWidth, (float)m_screenHeight}, {0, 0, 0, 0.7f}});
        
        // "YOU DIED" box
        float boxW = 300, boxH = 80;
        float cx = (m_screenWidth - boxW) / 2;
        float cy = (m_screenHeight - boxH) / 2;
        drawQuad({{cx, cy}, {boxW, boxH}, {0.5f, 0.1f, 0.1f, 0.9f}});
        drawQuad({{cx, cy - 3}, {boxW, 3}, {0.8f, 0.2f, 0.2f, 1.0f}});
        drawQuad({{cx, cy + boxH}, {boxW, 3}, {0.8f, 0.2f, 0.2f, 1.0f}});
    }
    
    void endFrame(VkCommandBuffer cmd) {
        if (m_vertices.empty()) return;
        
        // Upload vertices
        void* data;
        vkMapMemory(m_device, m_vertexMemory, 0, m_vertices.size() * sizeof(UIVertex), 0, &data);
        memcpy(data, m_vertices.data(), m_vertices.size() * sizeof(UIVertex));
        vkUnmapMemory(m_device, m_vertexMemory);
        
        // Bind pipeline and draw
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        
        VkBuffer buffers[] = {m_vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        
        vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
    }
    
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }
    
private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    
    std::vector<UIVertex> m_vertices;
    uint32_t m_screenWidth = 1280;
    uint32_t m_screenHeight = 720;
    
    static constexpr size_t MAX_VERTICES = 10000;
    
    uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }
    
    void createBuffers(VkPhysicalDevice physicalDevice) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = MAX_VERTICES * sizeof(UIVertex);
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_vertexBuffer);
        
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &memReq);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexMemory);
        vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexMemory, 0);
    }
    
    void createPipeline(VkRenderPass renderPass, VkDescriptorSetLayout descriptorLayout) {
        // Pipeline layout (no descriptors needed for simple colored quads)
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pushConstantRangeCount = 0;
        
        vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout);
        
        // We need the shaders to be loaded - for now mark as incomplete
        // The actual pipeline creation will happen in main.cpp where we have shader loading
        m_pipeline = VK_NULL_HANDLE;
    }
};

} // namespace myth
