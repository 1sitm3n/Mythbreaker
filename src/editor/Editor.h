#pragma once

#include "EditorCamera.h"
#include "engine/runtime/Project.h"
#include "engine/runtime/Runtime.h"
#include "engine/rendering/SceneRenderer.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace myth::editor {

// Owns authoring state and UI only. Window, ImGui context, renderer, and audio
// lifetime belong to the host application. draw() advances the runtime once.
class Editor {
public:
    explicit Editor(GLFWwindow* window);
    ~Editor();
    void initialize(const std::filesystem::path& projectPath = {},
                    const std::filesystem::path& executableDirectory = {});
    void draw(engine::SceneRenderer& renderer, float deltaTime);

    const engine::Scene& scene() const { return m_runtime.scene(); }
    engine::RenderCamera camera() const;
    std::filesystem::path projectRoot() const;
    engine::EntityId selectedEntity() const { return m_selected; }
    void selectEntity(engine::EntityId id) { m_selected = scene().find(id) ? id : engine::InvalidEntity; }
    engine::Runtime& runtime() { return m_runtime; }
    const engine::Runtime& runtime() const { return m_runtime; }
    bool quitRequested() const { return m_quit; }
    bool requestClose(); // False means the UI must resolve unsaved changes first.
    bool isDirty() const;
    void log(const std::string& message, bool error = false);

private:
    enum class Action { None, NewScene, NewProject, OpenProject, OpenScene, SaveAs, ImportAsset, ExportProject, Quit };
    struct LogEntry { std::string message; bool error; };
    struct PendingAnimation { engine::EntityId entity; std::string source; };
    void applyStyle();
    void drawDockspace();
    void drawMenu();
    void drawToolbar();
    void drawHierarchy();
    void drawEntityNode(engine::EntityId id);
    void drawInspector();
    void drawEnvironment();
    void drawAssets(engine::SceneRenderer& renderer);
    void drawConsole();
    void drawViewport(engine::SceneRenderer& renderer, float dt);
    void drawGizmo(const engine::RenderCamera& camera, const ImVec2& origin, const ImVec2& size);
    void drawDialogs(engine::SceneRenderer& renderer);
    void processShortcuts();
    void requestAction(Action action);
    void executeAction(Action action);
    void finishPathAction(engine::SceneRenderer& renderer);
    bool saveScene();
    void openProject(const std::filesystem::path& path);
    void refreshAssets();
    void detectImportedAnimations(const engine::SceneRenderer& renderer);
    void createEntity(const std::string& kind, engine::EntityId parent = engine::InvalidEntity);
    void instantiateAsset(const std::string& asset);
    void focusSelection();
    void deleteSelection();
    void duplicateSelection();
    void pick(const engine::SceneRenderer& renderer, const ImVec2& mouse, const ImVec2& origin, const ImVec2& size);
    void resetHistory(bool saved = true);
    void recordChanges();
    void undo();
    void redo();
    void tryOperation(const std::function<void()>& operation);
    void releaseNavigation();

    GLFWwindow* m_window = nullptr;
    engine::SceneRenderer* m_renderer = nullptr; // Host-owned, valid during draw.
    engine::Runtime m_runtime;
    std::optional<engine::Project> m_project;
    std::filesystem::path m_executableDirectory;
    std::string m_sceneRelative;
    EditorCamera m_editorCamera;
    engine::EntityId m_selected = engine::InvalidEntity;
    std::vector<nlohmann::json> m_undo, m_redo;
    nlohmann::json m_lastSnapshot, m_savedSnapshot;
    std::vector<LogEntry> m_log;
    std::vector<PendingAnimation> m_pendingAnimations;
    std::vector<std::filesystem::path> m_assets;
    std::vector<std::function<void()>> m_queuedActions;
    std::string m_assetFilter;
    std::string m_hierarchyFilter;
    std::string m_pathInput;
    std::string m_projectName = "My New Game";
    std::string m_newPropertyName = "value";
    int m_newPropertyType = 0;
    std::string m_status = "Ready";
    std::string m_error;
    std::string m_lastRendererError;
    std::string m_validationError;
    Action m_pendingAction = Action::None;
    Action m_pathAction = Action::None;
    bool m_openUnsavedDialog = false;
    bool m_openPathDialog = false;
    bool m_openErrorDialog = false;
    bool m_openControls = false;
    bool m_quit = false;
    bool m_resetLayout = true;
    bool m_gameView = false;
    bool m_navigationActive = false;
    bool m_viewportHovered = false;
    bool m_importInstantiate = true;
    bool m_showConsole = true;
    bool m_autoScroll = true;
    bool m_newLog = false;
    bool m_showGizmo = true;
    bool m_dirty = false;
    bool m_refreshRenderer = false;
    bool m_preserveSceneOnNewProject = false;
    float m_aspect = 16.0f / 9.0f;
    int m_gizmoAxis = -1;
    ImVec2 m_gizmoScreenDirection{0, 0};
    float m_gizmoPixelsPerUnit = 1.0f;
    glm::vec3 m_gizmoWorldAxis{0.0f};
    std::size_t m_seenRuntimeErrors = 0;
};

} // namespace myth::editor
