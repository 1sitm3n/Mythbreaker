#include "Editor.h"
#include "engine/platform/Package.h"
#include <imgui_internal.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace myth::editor {
namespace {
using namespace engine;
constexpr ImVec4 Accent{0.96f, 0.68f, 0.30f, 1.0f};
constexpr ImVec4 Muted{0.49f, 0.55f, 0.63f, 1.0f};
constexpr ImVec4 Good{0.40f, 0.78f, 0.60f, 1.0f};
constexpr ImVec4 Bad{0.96f, 0.43f, 0.42f, 1.0f};

bool inputString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
    if (value.capacity() < 256) value.reserve(256);
    auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            auto* string = static_cast<std::string*>(data->UserData);
            string->resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = string->data();
        }
        return 0;
    };
    return ImGui::InputText(label, value.data(), value.capacity() + 1,
                            flags | ImGuiInputTextFlags_CallbackResize, callback, &value);
}

bool inputAssetReference(const char* label, std::string& value) {
    struct Draft { std::string value; bool active = false; };
    static std::unordered_map<ImGuiID, Draft> drafts;
    if (drafts.size() > 4096 && !ImGui::IsAnyItemActive()) drafts.clear();
    auto& draft = drafts[ImGui::GetID(label)];
    if (!draft.active) draft.value = value;
    const bool entered = inputString(label, draft.value, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool active = ImGui::IsItemActive();
    const bool commit = entered || (draft.active && !active);
    draft.active = active;
    if (commit && value != draft.value) { value = draft.value; return true; }
    return false;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool matches(const std::string& text, const std::string& filter) {
    return filter.empty() || lowercase(text).find(lowercase(filter)) != std::string::npos;
}

bool isModel(const std::string& value) {
    const auto extension = lowercase(std::filesystem::path(value).extension().string());
    return extension == ".gltf" || extension == ".glb" || extension == ".obj";
}

bool isTexture(const std::string& value) {
    const auto extension = lowercase(std::filesystem::path(value).extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
}

const char* entityKind(const SceneEntity& entity) {
    if (entity.camera) return "CAMERA";
    if (entity.light) return "LIGHT";
    if (entity.mesh) return "MESH";
    if (entity.audio) return "AUDIO";
    return "ENTITY";
}

void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}

bool vectorControl(const char* label, glm::vec3& value, float speed, float minimum = 0, float maximum = 0) {
    ImGui::SetNextItemWidth(-1);
    ImGui::TextColored(Muted, "%s", label);
    const std::string id = std::string("##") + label;
    return ImGui::DragFloat3(id.c_str(), glm::value_ptr(value), speed, minimum, maximum, "%.3f");
}

bool componentHeader(const char* title, bool& remove) {
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.18f, 0.22f, 1));
    const bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::PopStyleColor();
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove component")) remove = true;
        ImGui::EndPopup();
    }
    return open;
}

void defaultScriptProperties(ScriptComponent& script) {
    if (script.type == "Spin") script.properties = {{"degreesPerSecond", {0.0, 30.0, 0.0}}};
    else if (script.type == "Oscillate") script.properties = {{"axis", {0.0, 1.0, 0.0}}, {"amplitude", 1.0}, {"frequency", 1.0}, {"phase", 0.0}};
    else if (script.type == "PlayerController") script.properties = {{"speed", 5.0}, {"jumpSpeed", 6.0}, {"lookSensitivity", 0.12}};
    else script.properties = nlohmann::json::object();
}

// Ray parameter is preserved under an affine inverse, so intersections from
// differently scaled entities can be compared without normalizing local rays.
float rayBox(const glm::vec3& origin, const glm::vec3& direction,
             const glm::vec3& minimum, const glm::vec3& maximum) {
    float nearT = 0.0f, farT = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-7f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return -1;
        } else {
            float a = (minimum[axis] - origin[axis]) / direction[axis];
            float b = (maximum[axis] - origin[axis]) / direction[axis];
            if (a > b) std::swap(a, b);
            nearT = std::max(nearT, a);
            farT = std::min(farT, b);
            if (nearT > farT) return -1;
        }
    }
    return nearT;
}
} // namespace

Editor::Editor(GLFWwindow* window) : m_window(window), m_runtime(engine::Scene::createDefault()) {}
Editor::~Editor() { releaseNavigation(); }

void Editor::initialize(const std::filesystem::path& projectPath, const std::filesystem::path& executableDirectory) {
    m_executableDirectory = executableDirectory;
    applyStyle();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (!projectPath.empty()) openProject(projectPath);
    else resetHistory();
    log("Mythbreaker Editor is ready. Create or open a project to begin.");
}

void Editor::applyStyle() {
    auto& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(9, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 5);
    style.WindowRounding = 4;
    style.FrameRounding = 4;
    style.PopupRounding = 5;
    style.ScrollbarRounding = 5;
    style.GrabRounding = 3;
    style.TabRounding = 4;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.DockingSeparatorSize = 4;
    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.085f, 0.098f, 0.12f, 1);
    colors[ImGuiCol_ChildBg] = ImVec4(0.085f, 0.098f, 0.12f, 1);
    colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.13f, 0.16f, 1);
    colors[ImGuiCol_Border] = ImVec4(0.21f, 0.24f, 0.29f, 0.7f);
    colors[ImGuiCol_Text] = ImVec4(0.87f, 0.90f, 0.94f, 1);
    colors[ImGuiCol_TextDisabled] = Muted;
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.19f, 1);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.28f, 1);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.26f, 0.32f, 1);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 1);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.17f, 1);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.065f, 0.075f, 0.095f, 1);
    colors[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.25f, 1);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.30f, 0.34f, 1);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.39f, 0.31f, 0.21f, 1);
    colors[ImGuiCol_Header] = ImVec4(0.23f, 0.20f, 0.15f, 1);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.27f, 0.19f, 1);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.31f, 0.20f, 1);
    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.15f, 1);
    colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.26f, 0.19f, 1);
    colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.20f, 0.16f, 1);
    colors[ImGuiCol_TabSelectedOverline] = Accent;
    colors[ImGuiCol_CheckMark] = Accent;
    colors[ImGuiCol_SliderGrab] = Accent;
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.80f, 0.49f, 1);
    colors[ImGuiCol_DockingPreview] = ImVec4(Accent.x, Accent.y, Accent.z, 0.30f);
    colors[ImGuiCol_SeparatorHovered] = Accent;
    colors[ImGuiCol_SeparatorActive] = Accent;
    colors[ImGuiCol_NavCursor] = Accent;
}

std::filesystem::path Editor::projectRoot() const {
    return m_project ? m_project->root : std::filesystem::current_path();
}

engine::RenderCamera Editor::camera() const {
    engine::RenderCamera result;
    if (m_gameView) {
        for (const auto& entity : scene().entities()) {
            if (!entity.camera || !entity.camera->primary || !scene().isEnabled(entity.id)) continue;
            const auto world = scene().worldMatrix(entity.id);
            result.position = glm::vec3(world[3]);
            const auto forward = glm::normalize(-glm::vec3(world[2]));
            const auto up = glm::normalize(glm::vec3(world[1]));
            result.view = glm::lookAtRH(result.position, result.position + forward, up);
            result.projection = glm::perspectiveRH_ZO(glm::radians(entity.camera->fov), m_aspect,
                                                     entity.camera->nearClip, entity.camera->farClip);
            result.projection[1][1] *= -1;
            return result;
        }
    }
    result.position = m_editorCamera.position;
    result.view = m_editorCamera.view();
    result.projection = m_editorCamera.projection(m_aspect);
    return result;
}

void Editor::log(const std::string& message, bool error) {
    m_log.push_back({message, error});
    if (m_log.size() > 400) m_log.erase(m_log.begin(), m_log.begin() + 100);
    m_status = message;
    m_newLog = true;
}

void Editor::tryOperation(const std::function<void()>& operation) {
    try { operation(); }
    catch (const std::exception& exception) {
        m_error = exception.what();
        m_openErrorDialog = true;
        log(m_error, true);
    }
}

void Editor::draw(engine::SceneRenderer& renderer, float deltaTime) {
    m_renderer = &renderer;
    if (m_refreshRenderer) { renderer.clearAssetCache(); m_refreshRenderer = false; }
    detectImportedAnimations(renderer);
    processShortcuts();
    drawDockspace();
    drawHierarchy();
    drawInspector();
    drawAssets(renderer);
    drawConsole();
    drawViewport(renderer, deltaTime);
    drawDialogs(renderer);
    recordChanges();
    if (!scene().find(m_selected)) m_selected = engine::InvalidEntity;
    const auto& errors = m_runtime.errors();
    if (m_seenRuntimeErrors > errors.size()) m_seenRuntimeErrors = 0;
    while (m_seenRuntimeErrors < errors.size()) log(errors[m_seenRuntimeErrors++], true);
    if (renderer.lastAssetError() != m_lastRendererError) {
        m_lastRendererError = renderer.lastAssetError();
        if (!m_lastRendererError.empty()) log(m_lastRendererError, true);
    }
    const std::string title = std::string("Mythbreaker Editor | ") + (m_project ? m_project->config.name : "Untitled project") +
                              " | " + m_runtime.editorScene().name + (isDirty() ? " *" : "");
    if (m_window) glfwSetWindowTitle(m_window, title.c_str());
}

void Editor::drawDockspace() {
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const auto flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("Mythbreaker Workspace", nullptr, flags);
    ImGui::PopStyleVar(3);
    drawMenu();
    drawToolbar();
    const auto dockId = ImGui::GetID("MythbreakerDockspace");
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const ImVec2 dockSize(area.x, std::max(100.0f, area.y - 27.0f));
    if (m_resetLayout) {
        m_resetLayout = false;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockId, ImGui::GetCursorScreenPos());
        ImGui::DockBuilderSetNodeSize(dockId, dockSize);
        ImGuiID center = dockId;
        const ImGuiID inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        const ImGuiID hierarchy = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", hierarchy);
        ImGui::DockBuilderDockWindow("Inspector", inspector);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpace(dockId, dockSize);
    ImGui::SetCursorPosX(12);
    ImGui::TextColored(Muted, "%s", m_status.c_str());
    ImGui::End();
}

void Editor::drawMenu() {
    if (!ImGui::BeginMenuBar()) return;
    ImGui::TextColored(Accent, " MYTHBREAKER ");
    if (ImGui::BeginMenu("File")) {
        ImGui::BeginDisabled(m_runtime.isPlaying());
        if (ImGui::MenuItem("New project...", "Ctrl+Shift+N")) requestAction(Action::NewProject);
        if (ImGui::MenuItem("Open project...", "Ctrl+O")) requestAction(Action::OpenProject);
        ImGui::Separator();
        if (ImGui::MenuItem("New scene", "Ctrl+N")) requestAction(Action::NewScene);
        if (ImGui::MenuItem("Open scene...", nullptr, false, m_project.has_value())) requestAction(Action::OpenScene);
        if (ImGui::MenuItem("Save scene", "Ctrl+S")) saveScene();
        if (ImGui::MenuItem("Save scene as...", "Ctrl+Shift+S", false, m_project.has_value())) requestAction(Action::SaveAs);
        const bool isStartup = m_project && !m_sceneRelative.empty() && m_project->config.startScene == m_sceneRelative;
        if (ImGui::MenuItem("Use current scene at startup", nullptr, isStartup, m_project && !m_sceneRelative.empty()))
            tryOperation([&] { m_project->config.startScene = m_sceneRelative; m_project->save(); log("Startup scene: " + m_sceneRelative); });
        ImGui::Separator();
        if (ImGui::MenuItem("Import asset...", "Ctrl+I", false, m_project.has_value())) requestAction(Action::ImportAsset);
        if (ImGui::MenuItem("Export standalone...", nullptr, false, m_project.has_value())) requestAction(Action::ExportProject);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) requestClose();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        ImGui::BeginDisabled(m_runtime.isPlaying());
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty())) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty())) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_selected != 0)) duplicateSelection();
        if (ImGui::MenuItem("Delete", "Delete", false, m_selected != 0)) deleteSelection();
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        ImGui::BeginDisabled(m_runtime.isPlaying());
        for (const auto* kind : {"Cube", "Plane", "Sphere", "Empty", "Camera", "Directional light", "Point light", "Audio source"})
            if (ImGui::MenuItem(kind)) createEntity(kind);
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Focus selected", "F", false, m_selected != 0)) focusSelection();
        ImGui::MenuItem("Translate gizmo", nullptr, &m_showGizmo);
        if (ImGui::MenuItem("Reset camera")) m_editorCamera = EditorCamera{};
        if (ImGui::MenuItem("Reset workspace layout")) m_resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Navigation and shortcuts")) m_openControls = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void Editor::drawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13, 6));
    ImGui::SetCursorPosX(12);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(m_project ? m_project->config.name.c_str() : "Untitled project");
    ImGui::SameLine();
    ImGui::TextColored(Muted, " / %s%s", m_runtime.editorScene().name.c_str(), isDirty() ? " *" : "");
    const float center = std::max(ImGui::GetCursorPosX() + 25.0f, ImGui::GetWindowWidth() * 0.43f);
    ImGui::SameLine(center);
    ImGui::BeginDisabled(m_runtime.isPlaying());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.30f, 0.23f, 1));
    if (ImGui::Button("Play")) tryOperation([&] {
        recordChanges(); m_runtime.play(); m_seenRuntimeErrors = 0;
        m_gameView = std::any_of(scene().entities().begin(), scene().entities().end(), [&](const auto& entity) {
            return entity.camera && entity.camera->primary && scene().isEnabled(entity.id);
        });
        log(m_gameView ? "Play mode started. Stop restores the authored scene." : "Play mode started with the editor camera. Add a primary camera to author a game view.");
    });
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_runtime.isPlaying());
    if (ImGui::Button(m_runtime.isPaused() ? "Resume" : "Pause")) {
        if (m_runtime.isPaused()) m_runtime.resume(); else m_runtime.pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { m_runtime.stop(); m_gameView = false; releaseNavigation(); log("Stopped. Authored scene restored."); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_runtime.isPaused());
    if (ImGui::Button("Step")) tryOperation([&] { m_runtime.stepOnce(); });
    help("Advance one fixed simulation step while paused.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(m_runtime.isPlaying() ? Good : Muted, "%s", m_runtime.isPaused() ? "PAUSED" : (m_runtime.isPlaying() ? "PLAYING" : "EDITING"));
    ImGui::PopStyleVar();
    ImGui::Separator();
}

void Editor::drawHierarchy() {
    ImGui::Begin("Hierarchy");
    ImGui::TextColored(Muted, "%zu ENTITIES", scene().entities().size());
    ImGui::SetNextItemWidth(-1);
    inputString("##FindEntity", m_hierarchyFilter);
    help("Filter entities by name.");
    ImGui::BeginDisabled(m_runtime.isPlaying());
    if (ImGui::Button("+ Create", ImVec2(-1, 0))) ImGui::OpenPopup("CreateEntity");
    if (ImGui::BeginPopup("CreateEntity")) {
        for (const auto* kind : {"Cube", "Plane", "Sphere", "Empty", "Camera", "Directional light", "Point light", "Audio source"})
            if (ImGui::MenuItem(kind)) createEntity(kind);
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (m_hierarchyFilter.empty()) {
        for (const auto& entity : scene().entities()) if (entity.parent == 0) drawEntityNode(entity.id);
    } else {
        for (const auto& entity : scene().entities()) {
            if (!matches(entity.name, m_hierarchyFilter)) continue;
            const std::string label = entity.name + "##" + std::to_string(entity.id);
            if (ImGui::Selectable(label.c_str(), m_selected == entity.id)) m_selected = entity.id;
        }
    }
    if (!m_runtime.isPlaying()) {
        ImGui::Spacing();
        ImGui::Button("Drop here to unparent", ImVec2(-1, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("MYTH_ENTITY")) {
                const auto id = *static_cast<const engine::EntityId*>(payload->Data);
                m_queuedActions.push_back([this, id] { m_runtime.editorScene().reparent(id, 0, true); });
            }
            ImGui::EndDragDropTarget();
        }
    }
    for (auto& action : m_queuedActions) tryOperation(action);
    m_queuedActions.clear();
    ImGui::End();
}

void Editor::drawEntityNode(engine::EntityId id) {
    const auto* entity = scene().find(id);
    if (!entity) return;
    const bool hasChildren = std::any_of(scene().entities().begin(), scene().entities().end(), [id](const auto& e) { return e.parent == id; });
    auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_selected == id) flags |= ImGuiTreeNodeFlags_Selected;
    if (!entity->enabled) ImGui::PushStyleColor(ImGuiCol_Text, Muted);
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)), flags, "%s", entity->name.c_str());
    if (!entity->enabled) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_selected = id;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { m_selected = id; focusSelection(); }
    if (!m_runtime.isPlaying()) {
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("MYTH_ENTITY", &id, sizeof(id));
            ImGui::TextUnformatted(entity->name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("MYTH_ENTITY")) {
                const auto child = *static_cast<const engine::EntityId*>(payload->Data);
                m_queuedActions.push_back([this, child, id] {
                    if (!m_runtime.editorScene().reparent(child, id, true)) log("Cannot parent an entity to itself or one of its descendants.", true);
                });
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Create empty child")) m_queuedActions.push_back([this, id] { createEntity("Empty", id); });
            if (ImGui::MenuItem("Duplicate")) m_queuedActions.push_back([this, id] { m_selected = id; duplicateSelection(); });
            if (ImGui::MenuItem("Delete")) m_queuedActions.push_back([this, id] { m_selected = id; deleteSelection(); });
            ImGui::EndPopup();
        }
    }
    if (hasChildren && open) {
        for (const auto& child : scene().entities()) if (child.parent == id) drawEntityNode(child.id);
        ImGui::TreePop();
    }
}

void Editor::drawInspector() {
    ImGui::Begin("Inspector");
    const float labelSpace = ImGui::CalcTextSize("Ambient intensity").x + ImGui::GetStyle().ItemInnerSpacing.x + 8.0f;
    ImGui::PushItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - labelSpace));
    auto* entity = m_runtime.scene().find(m_selected);
    ImGui::BeginDisabled(m_runtime.isPlaying());
    if (!entity) {
        ImGui::TextColored(Accent, "SCENE");
        inputString("Name", m_runtime.editorScene().name);
        drawEnvironment();
        ImGui::Spacing();
        ImGui::TextWrapped("Select an entity in the hierarchy or viewport to edit its components.");
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::End();
        return;
    }
    ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity->id)));
    ImGui::TextColored(Accent, "%s", entityKind(*entity));
    ImGui::SameLine();
    ImGui::TextColored(Muted, "#%llu", static_cast<unsigned long long>(entity->id));
    ImGui::Checkbox("Enabled", &entity->enabled);
    ImGui::SetNextItemWidth(-1);
    inputString("##EntityName", entity->name);
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        vectorControl("Position", entity->transform.position, 0.05f);
        vectorControl("Rotation", entity->transform.rotation, 0.5f);
        vectorControl("Scale", entity->transform.scale, 0.01f, 0.001f, 10000.0f);
        if (ImGui::SmallButton("Reset transform")) entity->transform = engine::Transform{};
        if (entity->parent) {
            const auto* parent = scene().find(entity->parent);
            ImGui::TextColored(Muted, "Parent: %s", parent ? parent->name.c_str() : "None");
        }
    }
    if (entity->mesh) {
        bool remove = false;
        ImGui::PushID("Mesh");
        if (componentHeader("Mesh renderer", remove)) {
            auto& mesh = *entity->mesh;
            ImGui::Checkbox("Visible", &mesh.visible);
            ImGui::TextColored(Muted, "Mesh source");
            ImGui::SetNextItemWidth(-1);
            inputAssetReference("##MeshSource", mesh.source);
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("MYTH_ASSET")) {
                    const auto* asset = static_cast<const char*>(payload->Data);
                    if (isModel(asset)) mesh.source = asset;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginCombo("Primitive", "Choose built-in...")) {
                for (const auto* primitive : {"cube", "plane", "sphere"})
                    if (ImGui::Selectable(primitive)) mesh.source = std::string("builtin:") + primitive;
                ImGui::EndCombo();
            }
            ImGui::SeparatorText("Material");
            ImGui::ColorEdit4("Base color", glm::value_ptr(mesh.material.baseColor), ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("Metallic", &mesh.material.metallic, 0, 1, "%.2f");
            ImGui::SliderFloat("Roughness", &mesh.material.roughness, 0.04f, 1, "%.2f");
            ImGui::TextColored(Muted, "Albedo texture");
            ImGui::SetNextItemWidth(-1);
            inputAssetReference("##AlbedoTexture", mesh.material.albedoTexture);
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("MYTH_ASSET")) {
                    const auto* asset = static_cast<const char*>(payload->Data);
                    if (isTexture(asset)) mesh.material.albedoTexture = asset;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::SmallButton("Clear texture")) mesh.material.albedoTexture.clear();
        }
        if (remove) entity->mesh.reset();
        ImGui::PopID();
    }
    if (entity->camera) {
        bool remove = false;
        ImGui::PushID("Camera");
        if (componentHeader("Camera", remove)) {
            auto& value = *entity->camera;
            if (ImGui::Checkbox("Primary game camera", &value.primary) && value.primary)
                for (auto& other : m_runtime.editorScene().entities()) if (other.id != entity->id && other.camera) other.camera->primary = false;
            ImGui::SliderFloat("Field of view", &value.fov, 10, 140, "%.1f deg");
            ImGui::DragFloat("Near plane", &value.nearClip, 0.01f, 0.01f, value.farClip - 0.01f, "%.2f");
            ImGui::DragFloat("Far plane", &value.farClip, 1, value.nearClip + 0.01f, 100000, "%.1f");
            if (ImGui::Button("Align with editor view")) {
                const auto inverseParent = entity->parent ? glm::inverse(scene().worldMatrix(entity->parent)) : glm::mat4(1);
                entity->transform.position = glm::vec3(inverseParent * glm::vec4(m_editorCamera.position, 1));
                // World alignment is unambiguous for root cameras; parenting can
                // be adjusted in the transform inspector without silently losing it.
                if (!entity->parent) entity->transform.rotation = glm::vec3(m_editorCamera.pitch, -m_editorCamera.yaw - 90.0f, 0);
            }
            if (entity->parent) ImGui::TextWrapped("Camera position aligns in parent space; adjust its local rotation above.");
        }
        if (remove) entity->camera.reset();
        ImGui::PopID();
    }
    if (entity->animation) {
        bool remove = false;
        ImGui::PushID("Animation");
        if (componentHeader("Animation", remove)) {
            auto& animation = *entity->animation;
            const auto clips = m_renderer && entity->mesh
                ? m_renderer->animationClips(entity->mesh->source, projectRoot())
                : std::vector<engine::AnimationClipInfo>{};
            if (!clips.empty()) {
                const auto index = static_cast<std::size_t>(std::max(0, animation.clipIndex));
                const std::string current = index < clips.size() ? clips[index].name : "Unavailable clip";
                if (ImGui::BeginCombo("Clip", current.c_str())) {
                    for (std::size_t i = 0; i < clips.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(clips[i].name.c_str(), index == i) && index != i) {
                            animation.clipIndex = static_cast<int>(i);
                            animation.time = 0;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (index < clips.size()) ImGui::TextColored(Muted, "Duration: %.3f s", clips[index].duration);
                else ImGui::TextColored(Bad, "Select an available clip for this model.");
            } else {
                ImGui::DragInt("Clip index", &animation.clipIndex, 1, 0, 100000);
                if (!entity->mesh) ImGui::TextWrapped("Add a mesh renderer with an animated glTF model to use this component.");
                else {
                    glm::vec3 minimum, maximum;
                    const bool loaded = m_renderer && m_renderer->meshBounds(entity->mesh->source, projectRoot(), minimum, maximum);
                    ImGui::TextWrapped(loaded ? "This model has no supported animation clips." : "Clip names appear after the model loads.");
                }
            }
            ImGui::SliderFloat("Speed", &animation.speed, 0, 10, "%.2fx");
            ImGui::Checkbox("Loop", &animation.loop);
            ImGui::Checkbox("Playing", &animation.playing);
            ImGui::DragFloat("Time (seconds)", &animation.time, 0.01f, 0, 360000, "%.3f");
            help("In edit mode this is the authored start offset. Play mode advances an independent copy of the animation clock.");
            if (ImGui::SmallButton("Restart clip")) animation.time = 0;
            ImGui::TextWrapped("Each entity has its own clip, speed, and playback time. Stop restores the authored start offset.");
        }
        if (remove) entity->animation.reset();
        ImGui::PopID();
    }
    if (entity->light) {
        bool remove = false;
        ImGui::PushID("Light");
        if (componentHeader("Light", remove)) {
            auto& value = *entity->light;
            int type = value.type == engine::LightType::Directional ? 0 : 1;
            if (ImGui::Combo("Type", &type, "Directional\0Point\0")) value.type = type == 0 ? engine::LightType::Directional : engine::LightType::Point;
            ImGui::ColorEdit3("Color", glm::value_ptr(value.color), ImGuiColorEditFlags_NoInputs);
            ImGui::DragFloat("Intensity", &value.intensity, 0.05f, 0, 1000, "%.2f");
            if (value.type == engine::LightType::Point) ImGui::DragFloat("Range", &value.range, 0.1f, 0.1f, 1000, "%.1f");
            else ImGui::TextWrapped("Rotate this entity to aim the light.");
        }
        if (remove) entity->light.reset();
        ImGui::PopID();
    }
    if (entity->rigidBody) {
        bool remove = false;
        ImGui::PushID("Body");
        if (componentHeader("Box physics", remove)) {
            auto& body = *entity->rigidBody;
            ImGui::Checkbox("Dynamic", &body.dynamic);
            ImGui::Checkbox("Use gravity", &body.useGravity);
            ImGui::DragFloat("Mass", &body.mass, 0.05f, 0.001f, 100000, "%.2f");
            ImGui::SliderFloat("Restitution", &body.restitution, 0, 1, "%.2f");
            vectorControl("Collider half extents", body.halfExtents, 0.02f, 0.001f, 10000);
            vectorControl("Initial velocity", body.velocity, 0.05f);
            ImGui::TextColored(Muted, "World-aligned box collisions");
            help("Box colliders do not rotate with the visual mesh. This solver does not simulate angular dynamics.");
        }
        if (remove) entity->rigidBody.reset();
        ImGui::PopID();
    }
    if (entity->audio) {
        bool remove = false;
        ImGui::PushID("Audio");
        if (componentHeader("Audio source", remove)) {
            auto& audio = *entity->audio;
            ImGui::TextColored(Muted, "Audio asset");
            ImGui::SetNextItemWidth(-1);
            inputAssetReference("##AudioSource", audio.source);
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("MYTH_ASSET")) audio.source = static_cast<const char*>(payload->Data);
                ImGui::EndDragDropTarget();
            }
            ImGui::SliderFloat("Volume", &audio.volume, 0, 1, "%.2f");
            ImGui::Checkbox("Loop", &audio.loop);
            ImGui::Checkbox("Play on start", &audio.playOnStart);
            ImGui::Checkbox("Spatial audio", &audio.spatial);
        }
        if (remove) entity->audio.reset();
        ImGui::PopID();
    }
    if (entity->script) {
        bool remove = false;
        ImGui::PushID("Script");
        if (componentHeader("Native behaviour", remove)) {
            auto& script = *entity->script;
            if (ImGui::BeginCombo("Type", script.type.c_str())) {
                for (const auto& type : m_runtime.behaviours().types())
                    if (ImGui::Selectable(type.c_str(), type == script.type) && type != script.type) { script.type = type; defaultScriptProperties(script); }
                ImGui::EndCombo();
            }
            if (!m_runtime.behaviours().contains(script.type)) ImGui::TextColored(Bad, "Behaviour type is not registered.");
            if (script.properties.is_object()) {
                std::string removeProperty;
                for (auto it = script.properties.begin(); it != script.properties.end(); ++it) {
                    auto& property = it.value();
                    ImGui::PushID(it.key().c_str());
                    if (property.is_number()) {
                        float value = property.get<float>();
                        if (ImGui::DragFloat(it.key().c_str(), &value, 0.05f)) property = value;
                    } else if (property.is_boolean()) {
                        bool value = property.get<bool>();
                        if (ImGui::Checkbox(it.key().c_str(), &value)) property = value;
                    } else if (property.is_string()) {
                        auto value = property.get<std::string>();
                        if (inputString(it.key().c_str(), value)) property = value;
                    } else if (property.is_array() && property.size() == 3 && property[0].is_number() && property[1].is_number() && property[2].is_number()) {
                        glm::vec3 value(property[0].get<float>(), property[1].get<float>(), property[2].get<float>());
                        if (vectorControl(it.key().c_str(), value, 0.05f)) property = {value.x, value.y, value.z};
                    } else ImGui::TextWrapped("%s: %s", it.key().c_str(), property.dump().c_str());
                    if (ImGui::BeginPopupContextItem("PropertyMenu")) {
                        if (ImGui::MenuItem("Remove property")) removeProperty = it.key();
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (!removeProperty.empty()) script.properties.erase(removeProperty);
            }
            if (ImGui::SmallButton("Add property...")) ImGui::OpenPopup("AddProperty");
            if (ImGui::BeginPopup("AddProperty")) {
                inputString("Name", m_newPropertyName);
                ImGui::Combo("Value type", &m_newPropertyType, "Number\0Boolean\0Text\0Vector 3\0");
                ImGui::BeginDisabled(m_newPropertyName.empty() || script.properties.contains(m_newPropertyName));
                if (ImGui::Button("Add")) {
                    switch (m_newPropertyType) {
                        case 0: script.properties[m_newPropertyName] = 0.0; break;
                        case 1: script.properties[m_newPropertyName] = false; break;
                        case 2: script.properties[m_newPropertyName] = ""; break;
                        case 3: script.properties[m_newPropertyName] = {0.0, 0.0, 0.0}; break;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::TextWrapped("Register additional C++ behaviours in your application. Scene files retain their type and properties.");
        }
        if (remove) entity->script.reset();
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("+ Add component", ImVec2(-1, 0))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (!entity->mesh && ImGui::MenuItem("Mesh renderer")) entity->mesh.emplace();
        if (!entity->camera && ImGui::MenuItem("Camera")) { entity->camera.emplace(); entity->camera->primary = false; }
        if (!entity->animation && ImGui::MenuItem("Animation")) entity->animation.emplace();
        if (!entity->light && ImGui::MenuItem("Light")) entity->light.emplace();
        if (!entity->rigidBody && ImGui::MenuItem("Box physics")) entity->rigidBody.emplace();
        if (!entity->audio && ImGui::MenuItem("Audio source")) entity->audio.emplace();
        if (!entity->script && ImGui::MenuItem("Native behaviour")) { entity->script.emplace(); entity->script->type = "Spin"; defaultScriptProperties(*entity->script); }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::EndDisabled();
    if (m_runtime.isPlaying()) { ImGui::Spacing(); ImGui::TextColored(Accent, "Stop play mode to edit the scene."); }
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Scene environment")) {
        ImGui::BeginDisabled(m_runtime.isPlaying());
        drawEnvironment();
        ImGui::EndDisabled();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void Editor::drawEnvironment() {
    auto& environment = m_runtime.editorScene().environment;
    ImGui::ColorEdit3("Sky color", glm::value_ptr(environment.skyColor), ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit3("Ambient color", glm::value_ptr(environment.ambientColor), ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Ambient intensity", &environment.ambientIntensity, 0, 2, "%.2f");
    ImGui::SliderFloat("Exposure", &environment.exposure, 0.05f, 5, "%.2f");
    vectorControl("Gravity", environment.gravity, 0.1f);
}

void Editor::drawAssets(engine::SceneRenderer& renderer) {
    ImGui::Begin("Assets");
    if (!m_project) {
        ImGui::TextWrapped("Create or open a project to manage its assets.");
        if (ImGui::Button("Create project...")) requestAction(Action::NewProject);
        ImGui::SameLine();
        if (ImGui::Button("Open project...")) requestAction(Action::OpenProject);
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(m_runtime.isPlaying());
    if (ImGui::Button("Import...")) requestAction(Action::ImportAsset);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) tryOperation([&] { refreshAssets(); renderer.clearAssetCache(); log("Assets refreshed."); });
    ImGui::SameLine();
    ImGui::SetNextItemWidth(190);
    inputString("##AssetFilter", m_assetFilter);
    ImGui::SameLine();
    ImGui::TextColored(Muted, "%zu files", m_assets.size());
    ImGui::Separator();
    if (m_assets.empty()) ImGui::TextWrapped("Import a glTF, GLB, OBJ, texture, or audio file. Models and their referenced files are copied into this project.");
    if (ImGui::BeginTable("AssetTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();
        for (const auto& asset : m_assets) {
            const auto path = asset.generic_string();
            if (!matches(path, m_assetFilter)) continue;
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            ImGui::Selectable(path.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            if (!m_runtime.isPlaying() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && isModel(path)) instantiateAsset(path);
            if (!m_runtime.isPlaying() && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("MYTH_ASSET", path.c_str(), path.size() + 1);
                ImGui::TextUnformatted(path.c_str());
                ImGui::EndDragDropSource();
            }
            help("Drag models into the viewport, or textures/audio onto their component fields. Double-click a model to instantiate it.");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(isModel(path) ? Accent : Muted, "%s", asset.extension().string().c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void Editor::drawConsole() {
    ImGui::Begin("Console");
    if (ImGui::Button("Clear")) m_log.clear();
    ImGui::SameLine(); ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::Separator();
    ImGui::BeginChild("Messages", ImVec2(0, 0));
    for (const auto& item : m_log) {
        ImGui::PushStyleColor(ImGuiCol_Text, item.error ? Bad : ImVec4(0.72f, 0.78f, 0.85f, 1));
        ImGui::TextWrapped("%s", item.message.c_str());
        ImGui::PopStyleColor();
    }
    if (m_autoScroll && m_newLog) ImGui::SetScrollHereY(1);
    m_newLog = false;
    ImGui::EndChild();
    ImGui::End();
}

void Editor::drawViewport(engine::SceneRenderer& renderer, float dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::SetCursorPosX(10);
    if (ImGui::Checkbox("Game camera", &m_gameView) && m_gameView) {
        const bool availableCamera = std::any_of(scene().entities().begin(), scene().entities().end(), [&](const auto& entity) {
            return entity.camera && entity.camera->primary && scene().isEnabled(entity.id);
        });
        if (!availableCamera) { m_gameView = false; log("This scene has no enabled primary camera. Add one from Create > Camera.", true); }
    }
    ImGui::SameLine();
    const auto& stats = renderer.stats();
    ImGui::TextColored(Muted, "%u draws   %u visible   %u culled   %u triangles", stats.drawCalls, stats.visibleObjects, stats.culledObjects, stats.triangles);
    ImGui::Separator();
    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x); available.y = std::max(1.0f, available.y);
    m_aspect = available.x / available.y;
    renderer.setViewportSize(static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(renderer.viewportTexture(), available);
    m_viewportHovered = ImGui::IsItemHovered();
    if (!m_runtime.isPlaying() && ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("MYTH_ASSET")) {
            const auto* path = static_cast<const char*>(payload->Data);
            if (isModel(path)) instantiateAsset(path);
        }
        ImGui::EndDragDropTarget();
    }
    auto& io = ImGui::GetIO();
    engine::RuntimeInput runtimeInput;
    const bool active = m_viewportHovered || m_navigationActive;
    const bool useEditorNavigation = !m_gameView;
    if (active && !io.WantTextInput) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_viewportHovered) {
            m_navigationActive = true;
            if (m_window) glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        if (m_navigationActive && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            const float forward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
            const float right = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
            const float up = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
            if (useEditorNavigation) {
                if (io.KeyAlt && m_selected && scene().find(m_selected)) m_editorCamera.orbit(glm::vec3(scene().worldMatrix(m_selected)[3]), io.MouseDelta.x, io.MouseDelta.y);
                else { m_editorCamera.look(io.MouseDelta.x, io.MouseDelta.y); m_editorCamera.move(forward, right, up, dt, io.KeyShift); }
                if (io.MouseWheel != 0) m_editorCamera.speed = std::clamp(m_editorCamera.speed * std::pow(1.2f, io.MouseWheel), 0.1f, 1000.0f);
            } else {
                const auto view = camera();
                auto forwardVector = -glm::vec3(glm::inverse(view.view)[2]); forwardVector.y = 0;
                if (glm::length(forwardVector) > 0.001f) forwardVector = glm::normalize(forwardVector);
                const auto rightVector = glm::normalize(glm::cross(forwardVector, glm::vec3(0, 1, 0)));
                runtimeInput.movement = forwardVector * forward + rightVector * right;
                runtimeInput.lookDelta = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
                runtimeInput.jump = ImGui::IsKeyPressed(ImGuiKey_Space, false);
            }
        }
        if (useEditorNavigation && !m_navigationActive && io.MouseWheel != 0) m_editorCamera.position += m_editorCamera.forward() * io.MouseWheel * std::max(0.2f, m_editorCamera.speed * 0.25f);
        if (useEditorNavigation && ImGui::IsKeyPressed(ImGuiKey_F, false)) focusSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) releaseNavigation();
    }
    if (m_navigationActive && (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) releaseNavigation();
    if (!m_runtime.isPlaying() && m_showGizmo && !m_gameView) drawGizmo(camera(), origin, available);
    if (m_viewportHovered && !m_navigationActive && m_gizmoAxis < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
        pick(renderer, io.MousePos, origin, available);
    auto* draw = ImGui::GetWindowDrawList();
    const std::string overlay = m_gameView ? "GAME VIEW  |  Hold RMB to control" : "PERSPECTIVE  |  RMB + WASD to fly  |  F to focus";
    draw->AddRectFilled(ImVec2(origin.x + 10, origin.y + 10), ImVec2(origin.x + 395, origin.y + 35), IM_COL32(16, 20, 27, 205), 4);
    draw->AddText(ImVec2(origin.x + 20, origin.y + 17), IM_COL32(186, 198, 214, 255), overlay.c_str());
    if (m_runtime.isPlaying()) {
        draw->AddRect(origin, ImVec2(origin.x + available.x, origin.y + available.y), IM_COL32(100, 190, 140, 255), 0, 0, 2);
    }
    ImGui::End();
    tryOperation([&] { m_runtime.update(dt, runtimeInput); });
}

void Editor::drawGizmo(const engine::RenderCamera& view, const ImVec2& origin, const ImVec2& size) {
    auto* entity = m_runtime.editorScene().find(m_selected);
    if (!entity) { m_gizmoAxis = -1; return; }
    const auto world = scene().worldMatrix(m_selected);
    const auto position = glm::vec3(world[3]);
    const auto vp = view.projection * view.view;
    auto project = [&](glm::vec3 point, ImVec2& screen) {
        const auto clip = vp * glm::vec4(point, 1);
        if (clip.w <= 0.001f) return false;
        const auto ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < 0 || ndc.z > 1) return false;
        screen = ImVec2(origin.x + (ndc.x * 0.5f + 0.5f) * size.x, origin.y + (ndc.y * 0.5f + 0.5f) * size.y);
        return true;
    };
    ImVec2 center;
    if (!project(position, center)) return;
    const float length = std::max(0.2f, glm::length(view.position - position) * 0.12f);
    auto* draw = ImGui::GetWindowDrawList();
    const std::array<ImU32, 3> colors{IM_COL32(236, 97, 93, 255), IM_COL32(104, 208, 126, 255), IM_COL32(102, 163, 241, 255)};
    auto& io = ImGui::GetIO();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 direction(0); direction[axis] = 1;
        ImVec2 end;
        if (!project(position + direction * length, end)) continue;
        const glm::vec2 delta(end.x - center.x, end.y - center.y);
        const float pixels = glm::length(delta);
        if (pixels < 5) continue;
        const auto unit = delta / pixels;
        const glm::vec2 fromCenter(io.MousePos.x - center.x, io.MousePos.y - center.y);
        const float along = glm::dot(fromCenter, unit);
        const float distance = glm::length(fromCenter - unit * std::clamp(along, 8.0f, pixels));
        const bool hovered = m_viewportHovered && distance < 7 && along > 8 && along < pixels + 7;
        const ImU32 color = m_gizmoAxis == axis || hovered ? IM_COL32(255, 217, 137, 255) : colors[axis];
        draw->AddLine(center, end, color, 3);
        draw->AddCircleFilled(end, 5, color);
        const char label[] = {static_cast<char>('X' + axis), 0};
        draw->AddText(ImVec2(end.x + 7, end.y - 7), color, label);
        if (hovered && !m_navigationActive && ImGui::IsMouseClicked(0)) {
            m_gizmoAxis = axis;
            m_gizmoScreenDirection = ImVec2(unit.x, unit.y);
            m_gizmoPixelsPerUnit = pixels / length;
            m_gizmoWorldAxis = direction;
        }
    }
    draw->AddCircleFilled(center, 4, IM_COL32(235, 235, 235, 255));
    draw->PopClipRect();
    if (m_gizmoAxis >= 0) {
        if (!ImGui::IsMouseDown(0)) m_gizmoAxis = -1;
        else {
            const float pixels = io.MouseDelta.x * m_gizmoScreenDirection.x + io.MouseDelta.y * m_gizmoScreenDirection.y;
            glm::vec3 delta = m_gizmoWorldAxis * (pixels / std::max(0.1f, m_gizmoPixelsPerUnit));
            if (entity->parent) delta = glm::vec3(glm::inverse(scene().worldMatrix(entity->parent)) * glm::vec4(delta, 0));
            entity->transform.position += delta;
        }
    }
}

void Editor::pick(const engine::SceneRenderer& renderer, const ImVec2& mouse, const ImVec2& origin, const ImVec2& size) {
    const auto view = camera();
    const float x = 2 * (mouse.x - origin.x) / size.x - 1;
    const float y = 2 * (mouse.y - origin.y) / size.y - 1;
    const auto inverse = glm::inverse(view.projection * view.view);
    auto nearPoint = inverse * glm::vec4(x, y, 0, 1); nearPoint /= nearPoint.w;
    auto farPoint = inverse * glm::vec4(x, y, 1, 1); farPoint /= farPoint.w;
    const glm::vec3 rayOrigin(nearPoint), rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
    float nearest = std::numeric_limits<float>::infinity();
    engine::EntityId selected = 0;
    for (const auto& entity : scene().entities()) {
        if (!entity.mesh || !entity.mesh->visible || !scene().isEnabled(entity.id)) continue;
        glm::vec3 minimum, maximum;
        if (!renderer.meshBounds(entity.mesh->source, projectRoot(), minimum, maximum, entity.id)) continue;
        // Planes need a small thickness for stable picking at grazing angles.
        maximum = glm::max(maximum, minimum + glm::vec3(0.005f));
        const auto inverseWorld = glm::inverse(scene().worldMatrix(entity.id));
        const auto localOrigin = glm::vec3(inverseWorld * glm::vec4(rayOrigin, 1));
        const auto localDirection = glm::vec3(inverseWorld * glm::vec4(rayDirection, 0));
        const float distance = rayBox(localOrigin, localDirection, minimum, maximum);
        if (distance >= 0 && distance < nearest) { nearest = distance; selected = entity.id; }
    }
    m_selected = selected;
}

void Editor::releaseNavigation() {
    if (m_navigationActive && m_window) glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    m_navigationActive = false;
}

void Editor::processShortcuts() {
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsAnyItemActive()) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) requestClose();
    if (m_runtime.isPlaying()) return;
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) requestAction(Action::SaveAs); else saveScene(); }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) requestAction(io.KeyShift ? Action::NewProject : Action::NewScene);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) requestAction(Action::OpenProject);
        if (ImGui::IsKeyPressed(ImGuiKey_I, false) && m_project) requestAction(Action::ImportAsset);
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) redo(); else undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection();
}

void Editor::createEntity(const std::string& kind, engine::EntityId parent) {
    if (m_runtime.isPlaying()) return;
    auto& entity = m_runtime.editorScene().createEntity(kind, parent);
    m_selected = entity.id;
    if (kind == "Cube" || kind == "Plane" || kind == "Sphere") {
        entity.mesh.emplace();
        entity.mesh->source = "builtin:" + lowercase(kind);
        if (kind == "Plane") entity.transform.scale = glm::vec3(10, 1, 10);
        else entity.transform.position.y = 0.5f;
    } else if (kind == "Camera") {
        entity.camera.emplace();
        entity.camera->primary = std::none_of(scene().entities().begin(), scene().entities().end(), [&](const auto& other) { return other.id != entity.id && other.camera && other.camera->primary; });
        entity.transform.position = parent ? glm::vec3(0, 2, 5) : m_editorCamera.position;
        if (!parent) entity.transform.rotation = glm::vec3(m_editorCamera.pitch, -m_editorCamera.yaw - 90.0f, 0);
    } else if (kind == "Directional light" || kind == "Point light") {
        entity.light.emplace();
        entity.light->type = kind == "Point light" ? engine::LightType::Point : engine::LightType::Directional;
        entity.transform.position = glm::vec3(0, 4, 0);
        entity.transform.rotation = glm::vec3(-45, 25, 0);
        if (kind == "Point light") entity.light->intensity = 20;
    } else if (kind == "Audio source") entity.audio.emplace();
    log("Created " + kind + ".");
}

void Editor::instantiateAsset(const std::string& asset) {
    if (m_runtime.isPlaying() || !isModel(asset)) return;
    auto& entity = m_runtime.editorScene().createEntity(std::filesystem::path(asset).stem().string());
    entity.mesh.emplace();
    entity.mesh->source = asset;
    m_selected = entity.id;
    const auto extension = lowercase(std::filesystem::path(asset).extension().string());
    if (extension == ".gltf" || extension == ".glb") m_pendingAnimations.push_back({entity.id, asset});
    log("Added model: " + asset);
}

void Editor::detectImportedAnimations(const engine::SceneRenderer& renderer) {
    if (m_runtime.isPlaying()) return;
    for (auto it = m_pendingAnimations.begin(); it != m_pendingAnimations.end();) {
        auto* entity = m_runtime.editorScene().find(it->entity);
        if (!entity || !entity->mesh || entity->mesh->source != it->source || entity->animation) {
            it = m_pendingAnimations.erase(it);
            continue;
        }
        glm::vec3 minimum, maximum;
        if (!renderer.meshBounds(it->source, projectRoot(), minimum, maximum)) {
            // A disabled imported entity may not be loaded until it is enabled.
            // Keep its probe until metadata is available or the entity changes.
            ++it;
            continue;
        }
        const auto clips = renderer.animationClips(it->source, projectRoot());
        if (!clips.empty()) {
            entity->animation.emplace();
            log("Animation added to " + entity->name + ": " + std::to_string(clips.size()) + " clips available.");
        }
        it = m_pendingAnimations.erase(it);
    }
}

void Editor::focusSelection() {
    const auto* entity = scene().find(m_selected);
    if (!entity) return;
    m_gameView = false;
    const auto matrix = scene().worldMatrix(m_selected);
    float radius = std::max({glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])), 1.0f});
    glm::vec3 target(matrix[3]);
    glm::vec3 minimum, maximum;
    if (m_renderer && entity->mesh && m_renderer->meshBounds(entity->mesh->source, projectRoot(), minimum, maximum, entity->id)) {
        target = glm::vec3(matrix * glm::vec4((minimum + maximum) * 0.5f, 1));
        const auto extent = (maximum - minimum) * 0.5f;
        const auto transformed = glm::abs(glm::vec3(matrix[0])) * extent.x + glm::abs(glm::vec3(matrix[1])) * extent.y + glm::abs(glm::vec3(matrix[2])) * extent.z;
        radius = std::max(0.5f, glm::length(transformed));
    }
    m_editorCamera.focus(target, radius);
}

void Editor::deleteSelection() {
    if (m_runtime.isPlaying() || !m_selected) return;
    if (m_runtime.editorScene().destroyEntity(m_selected)) log("Deleted entity and its children.");
    m_selected = 0;
}

void Editor::duplicateSelection() {
    if (m_runtime.isPlaying() || !m_selected) return;
    const auto id = m_runtime.editorScene().duplicateEntity(m_selected);
    if (id) { m_selected = id; log("Duplicated entity and its children."); }
}

bool Editor::isDirty() const { return m_dirty; }

void Editor::resetHistory(bool saved) {
    m_undo.clear(); m_redo.clear();
    m_pendingAnimations.clear();
    m_lastSnapshot = m_runtime.editorScene().toJson();
    if (saved) m_savedSnapshot = m_lastSnapshot;
    m_dirty = !saved;
    m_selected = 0;
    m_seenRuntimeErrors = 0;
}

void Editor::recordChanges() {
    if (m_runtime.isPlaying()) return;
    // Text and drag widgets may temporarily hold incomplete values. A component
    // inspector must remain usable while a path/name/number is being entered.
    if (ImGui::IsAnyItemActive() || m_gizmoAxis >= 0) return;
    try {
        const auto snapshot = m_runtime.editorScene().toJson();
        m_validationError.clear();
        m_dirty = snapshot != m_savedSnapshot;
        if (snapshot != m_lastSnapshot) {
            m_undo.push_back(m_lastSnapshot);
            if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
            m_redo.clear();
            m_lastSnapshot = snapshot;
        }
    } catch (const std::exception& exception) {
        m_dirty = true;
        if (m_validationError != exception.what()) {
            m_validationError = exception.what();
            log("Complete the edited field before saving: " + m_validationError, true);
        }
    }
}

void Editor::undo() {
    if (m_undo.empty() || m_runtime.isPlaying()) return;
    tryOperation([&] {
        m_redo.push_back(m_runtime.editorScene().toJson());
        m_runtime.setScene(engine::Scene::fromJson(m_undo.back()));
        m_undo.pop_back();
        m_lastSnapshot = m_runtime.editorScene().toJson();
        m_dirty = m_lastSnapshot != m_savedSnapshot;
        log("Undo.");
    });
}

void Editor::redo() {
    if (m_redo.empty() || m_runtime.isPlaying()) return;
    tryOperation([&] {
        m_undo.push_back(m_runtime.editorScene().toJson());
        m_runtime.setScene(engine::Scene::fromJson(m_redo.back()));
        m_redo.pop_back();
        m_lastSnapshot = m_runtime.editorScene().toJson();
        m_dirty = m_lastSnapshot != m_savedSnapshot;
        log("Redo.");
    });
}

bool Editor::saveScene() {
    if (m_runtime.isPlaying()) return false;
    if (!m_project) {
        m_preserveSceneOnNewProject = true;
        m_pathAction = Action::NewProject;
        m_pathInput.clear();
        m_openPathDialog = true;
        return false;
    }
    if (m_sceneRelative.empty()) {
        m_pathAction = Action::SaveAs;
        m_pathInput = "Scenes/" + m_runtime.editorScene().name + ".scene.json";
        m_openPathDialog = true;
        return false;
    }
    bool success = false;
    tryOperation([&] {
        m_project->saveScene(m_runtime.editorScene(), m_sceneRelative);
        m_savedSnapshot = m_runtime.editorScene().toJson();
        m_dirty = false;
        log("Saved " + m_sceneRelative + ".");
        success = true;
    });
    return success;
}

void Editor::openProject(const std::filesystem::path& path) {
    auto project = engine::Project::load(path);
    auto newScene = project.loadStartScene();
    m_runtime.setScene(std::move(newScene));
    m_project = std::move(project);
    m_sceneRelative = m_project->config.startScene;
    m_gameView = false;
    m_refreshRenderer = true;
    resetHistory();
    refreshAssets();
    log("Opened project: " + m_project->config.name);
}

void Editor::refreshAssets() { m_assets = m_project ? m_project->listAssets() : std::vector<std::filesystem::path>{}; }

bool Editor::requestClose() {
    releaseNavigation();
    if (m_runtime.isPlaying()) m_runtime.stop();
    try { m_dirty = m_runtime.editorScene().toJson() != m_savedSnapshot; }
    catch (const std::exception&) { m_dirty = true; }
    requestAction(Action::Quit);
    return m_quit;
}

void Editor::requestAction(Action action) {
    if (action != Action::Quit && m_runtime.isPlaying()) return;
    const bool destructive = action == Action::NewScene || action == Action::NewProject || action == Action::OpenProject || action == Action::OpenScene || action == Action::Quit;
    try { m_dirty = m_runtime.editorScene().toJson() != m_savedSnapshot; }
    catch (const std::exception&) { m_dirty = true; }
    if (destructive && isDirty()) {
        m_pendingAction = action;
        m_openUnsavedDialog = true;
        return;
    }
    executeAction(action);
}

void Editor::executeAction(Action action) {
    if (action == Action::Quit) { m_quit = true; return; }
    if (action == Action::NewScene) {
        m_runtime.setScene(engine::Scene{});
        m_sceneRelative.clear();
        m_gameView = false;
        resetHistory(false);
        log("New scene created.");
        return;
    }
    if (action == Action::None) return;
    m_pathAction = action;
    m_openPathDialog = true;
    if (action == Action::SaveAs) m_pathInput = m_sceneRelative.empty() ? "Scenes/New.scene.json" : m_sceneRelative;
    else if (action == Action::OpenScene) m_pathInput = "Scenes/";
    else if (action == Action::OpenProject && m_project) m_pathInput = m_project->manifestPath.string();
    else m_pathInput.clear();
    if (action == Action::NewProject) m_preserveSceneOnNewProject = false;
}

void Editor::finishPathAction(engine::SceneRenderer& renderer) {
    if (m_pathInput.empty()) throw std::runtime_error("Choose a path first.");
    const auto action = m_pathAction;
    if (action == Action::NewProject) {
        auto project = engine::Project::create(m_pathInput, m_projectName);
        auto preserved = m_runtime.editorScene().clone();
        if (m_preserveSceneOnNewProject) project.saveScene(preserved);
        m_project = std::move(project);
        m_runtime.setScene(m_preserveSceneOnNewProject ? std::move(preserved) : m_project->loadStartScene());
        m_sceneRelative = m_project->config.startScene;
        m_preserveSceneOnNewProject = false;
        resetHistory(); refreshAssets();
        m_refreshRenderer = true;
        log("Created project: " + m_project->config.name);
    } else if (action == Action::OpenProject) openProject(m_pathInput);
    else if (action == Action::OpenScene || action == Action::SaveAs) {
        if (!m_project) throw std::runtime_error("Create or open a project first.");
        auto relative = std::filesystem::path(m_pathInput);
        if (relative.is_absolute()) relative = relative.lexically_relative(m_project->root);
        if (action == Action::OpenScene) {
            auto loaded = m_project->loadScene(relative);
            m_runtime.setScene(std::move(loaded));
            m_sceneRelative = relative.generic_string();
            resetHistory();
            log("Opened scene: " + m_sceneRelative);
        } else {
            m_project->saveScene(m_runtime.editorScene(), relative);
            m_sceneRelative = relative.generic_string();
            m_savedSnapshot = m_runtime.editorScene().toJson();
            m_dirty = false;
            log("Saved scene: " + m_sceneRelative);
        }
    } else if (action == Action::ImportAsset) {
        if (!m_project) throw std::runtime_error("Create or open a project first.");
        const auto asset = m_project->importAsset(m_pathInput);
        refreshAssets(); renderer.clearAssetCache();
        log("Imported asset: " + asset);
        if (m_importInstantiate && isModel(asset)) instantiateAsset(asset);
    } else if (action == Action::ExportProject) {
        if (!m_project) throw std::runtime_error("Create or open a project first.");
        if (isDirty() && !saveScene()) throw std::runtime_error("Save the scene before exporting.");
        engine::packageProject(*m_project, m_pathInput, m_executableDirectory);
        log("Exported standalone project to " + m_pathInput);
    }
    m_pathAction = Action::None;
}

void Editor::drawDialogs(engine::SceneRenderer& renderer) {
    if (m_openUnsavedDialog) { ImGui::OpenPopup("Unsaved changes"); m_openUnsavedDialog = false; }
    ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save your scene before continuing? Changes to '%s' have not been saved.", m_runtime.editorScene().name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(130, 0))) {
            if (saveScene()) { const auto action = m_pendingAction; m_pendingAction = Action::None; ImGui::CloseCurrentPopup(); executeAction(action); }
            else if (m_openPathDialog) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(130, 0))) { const auto action = m_pendingAction; m_pendingAction = Action::None; ImGui::CloseCurrentPopup(); executeAction(action); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(130, 0))) { m_pendingAction = Action::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (m_openPathDialog) { ImGui::OpenPopup("Project operation"); m_openPathDialog = false; }
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Project operation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* title = "Choose a path";
        const char* hint = "";
        switch (m_pathAction) {
            case Action::NewProject: title = "Create project"; hint = "Choose a new or empty folder. Your project will contain its own scenes and assets."; break;
            case Action::OpenProject: title = "Open project"; hint = "Enter a project folder or a Mythbreaker project manifest path."; break;
            case Action::OpenScene: title = "Open scene"; hint = "Enter a path inside this project, such as Scenes/Main.scene.json."; break;
            case Action::SaveAs: title = "Save scene as"; hint = "Enter a project-relative path, such as Scenes/Level01.scene.json."; break;
            case Action::ImportAsset: title = "Import asset"; hint = "Enter the path to a glTF, GLB, OBJ, texture, or audio file. Required model dependencies will be copied with it."; break;
            case Action::ExportProject: title = "Export standalone"; hint = "Choose a new or empty destination folder. The runtime, shaders, project, and assets will be packaged together."; break;
            default: break;
        }
        ImGui::TextColored(Accent, "%s", title);
        ImGui::TextWrapped("%s", hint);
        ImGui::Spacing();
        if (m_pathAction == Action::NewProject) { ImGui::SetNextItemWidth(-1); inputString("Project name", m_projectName); }
        ImGui::SetNextItemWidth(-1);
        inputString("##Path", m_pathInput);
        if (m_pathAction == Action::ImportAsset) ImGui::Checkbox("Place imported model in the scene", &m_importInstantiate);
        if (m_pathAction == Action::ExportProject && m_project) {
            ImGui::TextWrapped("Startup scene: %s", m_project->config.startScene.c_str());
            if (!m_sceneRelative.empty() && m_sceneRelative != m_project->config.startScene) {
                ImGui::TextColored(Accent, "The currently open scene is not the startup scene.");
                if (ImGui::Button("Use current scene at startup"))
                    tryOperation([&] { m_project->config.startScene = m_sceneRelative; m_project->save(); log("Startup scene: " + m_sceneRelative); });
            }
        }
        if (m_project && (m_pathAction == Action::OpenScene || m_pathAction == Action::SaveAs)) ImGui::TextWrapped("Project: %s", m_project->root.string().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Continue", ImVec2(140, 0))) {
            bool complete = false;
            tryOperation([&] { finishPathAction(renderer); complete = true; });
            if (complete) {
                ImGui::CloseCurrentPopup();
                if (m_pendingAction != Action::None) { const auto action = m_pendingAction; m_pendingAction = Action::None; executeAction(action); }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0))) {
            m_pathAction = Action::None; m_pendingAction = Action::None; m_preserveSceneOnNewProject = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (m_openErrorDialog) { ImGui::OpenPopup("Operation failed"); m_openErrorDialog = false; }
    ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Operation failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(Bad, "The operation could not be completed.");
        ImGui::TextWrapped("%s", m_error.c_str());
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (m_openControls) { ImGui::OpenPopup("Editor controls"); m_openControls = false; }
    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Editor controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(Accent, "NAVIGATION");
        ImGui::TextWrapped("Hold right mouse in the viewport to look. WASD moves, Q/E moves down/up, Shift accelerates. Scroll changes flight speed while looking, or moves the camera otherwise.");
        ImGui::TextWrapped("Select an entity and press F to focus it. Alt + right mouse orbits the selection. Drag the colored gizmo axes to move an entity.");
        ImGui::SeparatorText("Authoring");
        ImGui::TextWrapped("Drag an entity onto another in Hierarchy to parent it. Drag it onto the unparent target to move it back to the scene root. Right-click component headers to remove components.");
        ImGui::TextWrapped("Ctrl+S saves, Ctrl+Z undoes, Ctrl+Y redoes, Ctrl+D duplicates, Delete removes. Double-click a model in Assets or drag it into the viewport to instantiate it.");
        ImGui::SeparatorText("Play mode");
        ImGui::TextWrapped("Play simulates a copy of your scene. Pause and Step inspect the simulation. Stop restores the original authored scene. Game camera uses the primary camera component; turn it off to inspect play mode with the editor camera.");
        ImGui::TextWrapped("Hold RMB in Game view to send WASD, mouse-look, and Space input to a PlayerController behaviour. Physics uses world-aligned box colliders.");
        if (ImGui::Button("Done", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace myth::editor
