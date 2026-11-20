#pragma once

#include "engine/runtime/Scene.h"
#include <imgui.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace myth::engine {

struct RenderCamera {
    glm::mat4 view{1.0f};
    // Vulkan projection: depth 0..1 and Y flipped. The editor/runtime constructs it.
    glm::mat4 projection{1.0f};
    glm::vec3 position{0.0f, 2.0f, 6.0f};
};

struct RendererStats {
    std::uint32_t drawCalls = 0;
    std::uint32_t visibleObjects = 0;
    std::uint32_t culledObjects = 0;
    std::uint32_t triangles = 0;
    std::uint32_t loadedMeshes = 0;
    std::uint32_t loadedTextures = 0;
    std::uint32_t animatedInstances = 0;
};

struct AnimationClipInfo {
    std::string name;
    float duration = 0.0f;
};

// Owns the Vulkan context, swapchain, offscreen scene target, GPU asset cache,
// and Dear ImGui platform/renderer backends. The caller owns the window and
// ImGui context. Call shutdown before destroying either of those objects.
//
// Frame order: beginFrame -> beginGuiFrame -> ImGui::NewFrame -> editor UI ->
// ImGui::Render -> render. Resize requests take effect at the NEXT beginFrame,
// so a texture referenced by this frame's UI is never invalidated mid-frame.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void initialize(GLFWwindow* window, const std::filesystem::path& shaderDirectory);
    void shutdown() noexcept;
    bool beginFrame(); // Returns false while minimized / recreating the swapchain.
    void beginGuiFrame(); // Backend NewFrame calls; caller invokes ImGui::NewFrame.
    void render(const Scene& scene, const RenderCamera& camera,
                const std::filesystem::path& projectRoot, EntityId selected,
                ImDrawData* drawData);

    void setViewportSize(std::uint32_t width, std::uint32_t height);
    ImTextureID viewportTexture() const;
    std::uint32_t viewportWidth() const;
    std::uint32_t viewportHeight() const;
    const RendererStats& stats() const;
    const std::string& lastAssetError() const;
    bool meshBounds(const std::string& source, const std::filesystem::path& projectRoot,
                    glm::vec3& minimum, glm::vec3& maximum, EntityId instance = InvalidEntity) const;
    std::vector<AnimationClipInfo> animationClips(const std::string& source,
                                                const std::filesystem::path& projectRoot) const;
    void clearAssetCache(); // Also reloads externally changed model/texture files.
    void waitIdle();
    // Saves the latest offscreen viewport as a portable RGB PPM, useful in CI.
    void captureScreenshot(const std::filesystem::path& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace myth::engine
