#include "editor/Editor.h"
#include <imgui.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// This test runs the actual editor, runtime, serialization, and Dear ImGui frame
// code without opening an OS window. Rendering and two GLFW window operations
// are deliberately replaced: these checks do not validate GPU output.
extern "C" void glfwSetWindowTitle(GLFWwindow*, const char*) {}
extern "C" void glfwSetInputMode(GLFWwindow*, int, int) {}

namespace myth::engine {
struct SceneRenderer::Impl {};
SceneRenderer::SceneRenderer() : m_impl(std::make_unique<Impl>()) {}
SceneRenderer::~SceneRenderer() = default;
void SceneRenderer::setViewportSize(std::uint32_t, std::uint32_t) {}
ImTextureID SceneRenderer::viewportTexture() const { return 1; }
const RendererStats& SceneRenderer::stats() const { static RendererStats result; return result; }
const std::string& SceneRenderer::lastAssetError() const { static std::string result; return result; }
void SceneRenderer::clearAssetCache() {}
bool SceneRenderer::meshBounds(const std::string&, const std::filesystem::path&, glm::vec3& minimum, glm::vec3& maximum, EntityId) const {
    minimum = glm::vec3(-0.5f); maximum = glm::vec3(0.5f); return true;
}
std::vector<AnimationClipInfo> SceneRenderer::animationClips(const std::string&, const std::filesystem::path&) const { return {}; }
} // namespace myth::engine

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("mythbreaker-editor-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

struct ImGuiContextScope {
    ImGuiContextScope() { ImGui::CreateContext(); }
    ~ImGuiContextScope() { ImGui::DestroyContext(); }
};
} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        const auto project = myth::engine::Project::create(temporary.path / "Project", "Editor test project");
        ImGuiContextScope context;
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1440, 900);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        unsigned char* pixels = nullptr; int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(1);
        myth::engine::SceneRenderer renderer;
        myth::editor::Editor editor(nullptr);
        editor.initialize(project.root);
        auto frame = [&] {
            ImGui::NewFrame();
            editor.draw(renderer, 1.0f / 60.0f);
            ImGui::Render();
            require(ImGui::GetDrawData()->TotalVtxCount > 0, "Editor did not produce ImGui draw data");
        };
        auto shortcut = [&](ImGuiKey key) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(key, true);
            frame();
            io.AddKeyEvent(key, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
        };
        frame(); frame();
        require(!editor.isDirty(), "Freshly loaded project should be clean");
        require(editor.projectRoot() == project.root, "Editor did not retain the project root");

        // Select each default entity to exercise the real inspector for its
        // mesh, camera, light, body, and behaviour components.
        for (const auto& entity : editor.scene().entities()) {
            editor.selectEntity(entity.id);
            frame();
        }
        const auto firstId = editor.scene().entities().front().id;
        const auto originalName = editor.scene().find(firstId)->name;
        editor.selectEntity(firstId);
        editor.runtime().editorScene().find(firstId)->name = "Edited entity";
        frame();
        require(editor.isDirty(), "Scene edit was not marked dirty");
        shortcut(ImGuiKey_Z);
        require(editor.scene().find(firstId)->name == originalName, "Ctrl+Z did not restore authored state");
        shortcut(ImGuiKey_Y);
        require(editor.scene().find(firstId)->name == "Edited entity", "Ctrl+Y did not restore the undone edit");
        shortcut(ImGuiKey_S);
        require(!editor.isDirty(), "Saved scene remains dirty");
        require(project.loadStartScene().find(firstId)->name == "Edited entity", "Ctrl+S did not persist the actual scene file");

        auto& audioEntity = editor.runtime().editorScene().createEntity("Unassigned audio");
        audioEntity.audio.emplace();
        const auto audioId = audioEntity.id;
        editor.selectEntity(audioId);
        frame();
        require(editor.isDirty(), "Adding a component did not dirty the scene");
        shortcut(ImGuiKey_Z);
        require(!editor.scene().find(audioId), "Undo did not remove the newly created entity");

        auto& animated = editor.runtime().editorScene().createEntity("Animation inspector test");
        animated.mesh.emplace();
        auto& animation = animated.animation.emplace();
        animation.clipIndex = 1;
        animation.speed = 1.25f;
        animation.loop = false;
        animation.playing = true;
        animation.time = 0.5f;
        const auto animationId = animated.id;
        editor.selectEntity(animationId);
        frame();
        shortcut(ImGuiKey_S);
        const auto savedScene = project.loadStartScene();
        const auto& savedAnimation = *savedScene.find(animationId)->animation;
        require(savedAnimation.clipIndex == 1 && savedAnimation.speed == 1.25f && !savedAnimation.loop &&
                savedAnimation.playing && savedAnimation.time == 0.5f, "Animation authoring fields did not survive the actual save/load roundtrip");

        const auto authored = editor.runtime().editorScene().toJson();
        editor.runtime().play(); frame();
        require(editor.runtime().isPlaying(), "Runtime did not enter play mode");
        require(editor.scene().find(animationId)->animation->time > 0.5f, "Playing editor did not advance the per-instance animation clock");
        editor.runtime().pause();
        const auto time = editor.runtime().simulationTime();
        const auto pausedAnimationTime = editor.scene().find(animationId)->animation->time;
        frame();
        require(editor.runtime().isPaused() && editor.runtime().simulationTime() == time, "Paused editor advanced simulation");
        require(editor.scene().find(animationId)->animation->time == pausedAnimationTime, "Paused editor advanced animation time");
        editor.runtime().stop(); frame();
        require(editor.scene().toJson() == authored, "Stop did not restore the authored scene");

        // A user can temporarily clear a name while typing. Scene validation
        // should report the incomplete field without terminating the UI.
        editor.runtime().editorScene().find(firstId)->name.clear();
        editor.selectEntity(firstId);
        frame();
        require(editor.isDirty(), "Invalid unsaved authoring data should remain dirty");
        require(!editor.requestClose(), "Closing discarded unsaved edits without confirmation");
        frame();
        require(!editor.quitRequested(), "Unsaved-change prompt unexpectedly closed the editor");
        editor.runtime().editorScene().find(firstId)->name = "Edited entity";
        frame();
        std::cout << "PASS: editor frames, component inspectors, dirty state, undo/redo, disk save, animation roundtrip and playback, play/pause/stop, invalid-field recovery, close confirmation.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
