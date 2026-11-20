#include "engine/runtime/Project.h"
#include "engine/runtime/Runtime.h"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace myth::engine;
namespace fs = std::filesystem;

namespace {
int assertions = 0;
void check(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const std::string& message, float epsilon = 0.0001f) {
    check(std::abs(actual - expected) <= epsilon, message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}
void fails(const std::function<void()>& function, const std::string& message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    check(threw, message);
}
struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() / ("mythbreaker-core-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { fs::create_directories(path); }
    ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary); stream << value;
    if (!stream) throw std::runtime_error("Test fixture write failed");
}

void hierarchy() {
    Scene scene;
    const auto parent = scene.createEntity("Parent").id;
    const auto child = scene.createEntity("Child", parent).id;
    scene.find(parent)->transform.position = {10, 0, 0};
    scene.find(child)->transform.position = {2, 3, 4};
    near(scene.worldMatrix(child)[3].x, 12, "Hierarchy composes transforms");
    check(!scene.reparent(parent, child), "Hierarchy cycle rejected");
    check(!scene.reparent(child, child), "Self-parent rejected");
    check(scene.reparent(child, 0), "Can reparent to scene root");
    near(scene.find(child)->transform.position.x, 12, "Reparent retains world position");
    check(scene.reparent(child, parent), "Can reparent under original parent");
    near(scene.find(child)->transform.position.x, 2, "Reparent computes local position");
    scene.find(parent)->enabled = false;
    check(!scene.isEnabled(child), "Disabled ancestors disable descendants");
    scene.find(parent)->enabled = true;
    const auto duplicated = scene.duplicateEntity(parent);
    check(duplicated != parent && duplicated != child && duplicated != 0, "Duplication assigns fresh root ID");
    check(scene.entities().size() == 4, "Duplication includes descendants");
    int copiedChildren = 0;
    for (const auto& entity : scene.entities()) if (entity.parent == duplicated) ++copiedChildren;
    check(copiedChildren == 1, "Duplication remaps child parent");
    check(scene.destroyEntity(parent), "Can destroy subtree");
    check(!scene.find(child) && !scene.find(parent), "Deletion removes descendants");
    check(scene.entities().size() == 2, "Deletion preserves unrelated subtree");
    const auto replacement = scene.createEntity("New").id;
    check(replacement > duplicated && replacement != parent && replacement != child, "Persistent IDs are never recycled");
    scene.validate();

    Scene transformed;
    const auto p = transformed.createEntity("Scaled parent").id;
    const auto c = transformed.createEntity("Object").id;
    transformed.find(p)->transform.position = {3, 4, 5};
    transformed.find(p)->transform.rotation = {20, 40, 10};
    transformed.find(p)->transform.scale = {2, 2, 2};
    transformed.find(c)->transform.position = {8, 9, 10};
    transformed.find(c)->transform.rotation = {10, 20, 30};
    const auto before = transformed.worldMatrix(c);
    check(transformed.reparent(c, p), "Uniformly scaled rotated reparent succeeds");
    const auto after = transformed.worldMatrix(c);
    for (int col = 0; col != 4; ++col) for (int row = 0; row != 4; ++row)
        near(after[col][row], before[col][row], "Reparent preserves full world transform", .001f);
}

void serialization() {
    TemporaryDirectory temporary;
    Scene scene = Scene::createDefault();
    const auto id = scene.entities().front().id;
    auto* entity = scene.find(id);
    entity->audio = AudioSourceComponent{"Assets/sound.wav", .4f, true, true, false};
    entity->animation = AnimationComponent{2, 1.5f, false, true, .25f};
    entity->mesh->material.albedoTexture = "Assets/checker.png";
    entity->script = ScriptComponent{"Spin", {{"degreesPerSecond", {0, 10, 0}}}};
    const auto deleted = scene.createEntity("Delete me").id;
    scene.destroyEntity(deleted);
    const auto path = temporary.path / "sub" / "scene.json";
    scene.save(path);
    auto loaded = Scene::load(path);
    check(loaded.toJson() == scene.toJson(), "Scene roundtrip retains every component and environment value");
    check(loaded.createEntity("After load").id > deleted, "Save retains high-water entity ID after deletion");
    scene.name = "Replacement";
    scene.save(path);
    check(Scene::load(path).name == "Replacement", "Atomic save replaces existing file");
    int count = 0;
    for (const auto& file : fs::directory_iterator(path.parent_path())) { (void)file; ++count; }
    check(count == 1, "Atomic save leaves no temporary files");
    const auto current = scene.toJson();
    write(temporary.path / "bad.json", "{ not json");
    fails([&] { scene = Scene::load(temporary.path / "bad.json"); }, "Malformed JSON rejects load");
    check(scene.toJson() == current, "Failed load preserves existing scene");
}

void malformedScenes() {
    auto base = Scene::createDefault().toJson();
    auto test = [&](const std::function<void(nlohmann::json&)>& mutate, const std::string& message) {
        auto json = base; mutate(json); fails([&] { Scene::fromJson(json); }, message);
    };
    test([](auto& j) { j["version"] = 99; }, "Unknown scene version rejected");
    test([](auto& j) { j["entities"][1]["id"] = j["entities"][0]["id"]; }, "Duplicate IDs rejected");
    test([](auto& j) { j["entities"][0]["id"] = -1; }, "Negative IDs rejected");
    test([](auto& j) { j["entities"][0]["parent"] = 9999; }, "Missing parent rejected");
    test([](auto& j) { j["entities"][0]["parent"] = j["entities"][1]["id"]; j["entities"][1]["parent"] = j["entities"][0]["id"]; }, "Cyclic scene rejected");
    test([](auto& j) { j["entities"][0]["transform"]["scale"] = {1, 0, 1}; }, "Zero transform scale rejected");
    test([](auto& j) { j["entities"][0]["transform"]["position"] = {1, 2}; }, "Truncated vector rejected");
    test([](auto& j) { j["entities"][0]["mesh"]["source"] = "../outside.glb"; }, "Escaping mesh reference rejected");
    test([](auto& j) { j["entities"][0]["mesh"]["source"] = "C:/outside.glb"; }, "Absolute Windows asset reference rejected on all hosts");
    test([](auto& j) { j["entities"][0]["mesh"]["source"] = "builtin:invalid"; }, "Unknown primitive rejected");
    test([](auto& j) { j["entities"][0]["mesh"]["material"]["roughness"] = -1; }, "Invalid material rejected");
    test([](auto& j) { j["entities"][0]["camera"] = {{"nearClip", 5}, {"farClip", 1}}; }, "Invalid camera rejected");
    test([](auto& j) { j["entities"][0]["script"] = {{"type", "Spin"}, {"properties", nlohmann::json::array()}}; }, "Invalid script properties rejected");
    test([](auto& j) { j["entities"][0]["audio"] = {{"source", "../audio.wav"}}; }, "Escaping audio reference rejected");
    test([](auto& j) { j["nextId"] = 1; }, "Entity ID watermark regression rejected");
    test([](auto& j) { j["entities"][0]["animation"] = {{"clipIndex", -1}}; }, "Negative animation clip rejected");
    test([](auto& j) { j["entities"][0]["animation"] = {{"speed", -1}}; }, "Negative playback rate rejected");
    Scene nonfinite;
    nonfinite.createEntity().transform.position.x = std::numeric_limits<float>::quiet_NaN();
    fails([&] { nonfinite.toJson(); }, "Non-finite transforms cannot be saved");
}

void projects() {
    TemporaryDirectory temporary;
    auto project = Project::create(temporary.path / "Source", "Example");
    check(fs::exists(project.manifestPath), "Project manifest created");
    check(project.loadStartScene().name == "Example", "Project creates playable default scene");
    const auto reopened = Project::load(project.root);
    check(reopened.config.name == "Example", "Project directory resolves its manifest");
    fails([&] { Project::create(project.root, "Overwrite"); }, "Project creation refuses non-empty directory");
    fails([&] { project.resolve("../outside.txt"); }, "Project rejects parent traversal");
    fails([&] { project.resolve("C:\\outside.txt"); }, "Project rejects Windows absolute paths on every platform");

    const auto external = temporary.path / "External";
    write(external / "model.bin", "geometry");
    write(external / "textures" / "albedo.png", "image");
    writeJsonAtomically(external / "model.gltf", {{"asset", {{"version", "2.0"}}},
        {"buffers", {{{"uri", "model.bin"}, {"byteLength", 8}}}},
        {"images", {{{"uri", "textures/albedo.png"}}}}});
    const auto asset = project.importAsset(external / "model.gltf");
    const auto imported = project.resolve(asset);
    check(fs::exists(imported), "Asset primary imported");
    check(fs::exists(imported.parent_path() / "model.bin"), "glTF external buffer imported");
    check(fs::exists(imported.parent_path() / "textures" / "albedo.png"), "glTF texture directory imported");
    check(project.importAsset(external / "model.gltf") != asset, "Repeated imports never overwrite existing assets");
    auto bad = readJsonFile(external / "model.gltf");
    bad["buffers"][0]["uri"] = "../outside.bin";
    writeJsonAtomically(external / "bad.gltf", bad);
    fails([&] { project.importAsset(external / "bad.gltf"); }, "Escaping glTF dependency rejected");
    bad["buffers"][0]["uri"] = "https://example.invalid/data.bin";
    writeJsonAtomically(external / "bad.gltf", bad);
    fails([&] { project.importAsset(external / "bad.gltf"); }, "Remote glTF dependency rejected");
    check(project.listAssets().size() == 6, "Asset listing includes imported bundles");
    write(external / "unsupported.ogg", "unsupported codec");
    fails([&] { project.importAsset(external / "unsupported.ogg"); }, "Import rejects audio codecs unavailable in the runtime");

    std::promise<void> beginImports;
    const auto importStart = beginImports.get_future().share();
    std::vector<std::future<std::string>> imports;
    for (int i = 0; i != 8; ++i) imports.push_back(std::async(std::launch::async, [&, importStart] {
        importStart.wait();
        return project.importAsset(external / "model.gltf");
    }));
    beginImports.set_value();
    std::set<std::string> simultaneousBundles;
    for (auto& import : imports) {
        const auto importedReference = import.get();
        check(simultaneousBundles.insert(importedReference).second, "Concurrent imports reserve distinct bundles");
        const auto bundle = project.resolve(importedReference).parent_path();
        check(fs::is_regular_file(bundle / "model.bin") && fs::is_regular_file(bundle / "textures" / "albedo.png"),
            "Concurrent import retains all bundle dependencies");
    }

    std::string glbJson = readJsonFile(external / "model.gltf").dump();
    while (glbJson.size() % 4 != 0) glbJson += ' ';
    std::string glb;
    const auto append32 = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift != 32; shift += 8) glb += static_cast<char>((value >> shift) & 0xff);
    };
    append32(0x46546C67); append32(2); append32(static_cast<std::uint32_t>(20 + glbJson.size()));
    append32(static_cast<std::uint32_t>(glbJson.size())); append32(0x4E4F534A); glb += glbJson;
    write(external / "container.glb", glb);
    const auto glbReference = project.importAsset(external / "container.glb");
    const auto glbBundle = project.resolve(glbReference).parent_path();
    check(fs::is_regular_file(glbBundle / "model.bin") && fs::is_regular_file(glbBundle / "textures" / "albedo.png"),
        "GLB imports preserve external buffers and textures referenced by the JSON chunk");
    auto glbScene = project.loadStartScene();
    glbScene.entities().front().mesh->source = glbReference;
    project.validateAssets(glbScene);
    fs::remove(glbBundle / "textures" / "albedo.png");
    fails([&] { project.validateAssets(glbScene); }, "GLB validation catches missing external texture");
    write(glbBundle / "textures" / "albedo.png", "image");
    glb[8] = 0; glb[9] = 0; glb[10] = 0; glb[11] = 0;
    write(external / "invalid.glb", glb);
    fails([&] { project.importAsset(external / "invalid.glb"); }, "GLB declared file length is validated before import");

    auto scene = project.loadStartScene();
    scene.entities().front().mesh->source = asset;
    project.saveScene(scene);
    project.validateAssets(scene);
    fs::remove(imported.parent_path() / "model.bin");
    fails([&] { project.validateAssets(scene); }, "Validation rejects missing glTF sidecars");
    const auto failedExport = temporary.path / "FailedExport";
    fs::create_directory(failedExport);
    fails([&] { project.exportTo(failedExport); }, "Export rejects incomplete model bundle");
    check(fs::is_directory(failedExport) && fs::is_empty(failedExport), "Failed export preserves existing empty destination");
    write(imported.parent_path() / "model.bin", "geometry");
    const auto exportedPath = temporary.path / "Exported";
    fs::create_directory(exportedPath);
    project.exportTo(exportedPath);
    const auto exported = Project::load(exportedPath);
    check(exported.loadStartScene().toJson() == scene.toJson(), "Export retains complete scene");
    check(fs::exists(exported.resolve(asset).parent_path() / "model.bin"), "Export retains external model dependency");
    fs::remove_all(external);
    check(fs::exists(exported.resolve(asset)), "Export is independent of original imported file location");
    fails([&] { project.exportTo(project.root / "Nested"); }, "Export refuses recursive destination");
    fails([&] { project.exportTo(exportedPath); }, "Export refuses to overwrite non-empty destination");
    check(fs::exists(exported.manifestPath), "Failed export preserves existing destination");
    for (const auto& entry : fs::directory_iterator(temporary.path))
        check(entry.path().filename().string().find(".export-") == std::string::npos, "Export leaves no staging directory");

    const auto secret = temporary.path / "Secret";
    fs::create_directory(secret);
    std::error_code linkError;
    fs::create_directory_symlink(secret, project.root / "Assets" / "escape", linkError);
    if (!linkError) fails([&] { project.resolve("Assets/escape/anything.txt"); }, "Symlink traversal outside project rejected");
}

Scene physicsScene(EntityId& dynamicId) {
    Scene scene;
    auto& ground = scene.createEntity("Ground");
    ground.transform.position.y = -0.5f;
    ground.transform.scale = {20, 1, 20};
    ground.rigidBody = RigidBodyComponent{};
    ground.rigidBody->dynamic = false;
    ground.rigidBody->restitution = 0;
    auto& box = scene.createEntity("Box");
    dynamicId = box.id;
    box.transform.position.y = 4;
    box.rigidBody = RigidBodyComponent{};
    box.rigidBody->restitution = 0;
    return scene;
}

void runtimeControls() {
    Scene scene;
    const auto id = scene.createEntity("Spinner").id;
    scene.find(id)->script = ScriptComponent{"Spin", {{"degreesPerSecond", {0, 30, 0}}}};
    scene.find(id)->animation = AnimationComponent{0, 2.0f, true, true, .25f};
    Runtime runtime(scene);
    check(runtime.state() == RuntimeState::Edit, "Runtime starts in edit state");
    runtime.update(1);
    near(runtime.scene().find(id)->transform.rotation.y, 0, "Edit scene does not simulate");
    runtime.play();
    for (int i = 0; i != 60; ++i) runtime.update(Runtime::FixedStep);
    near(runtime.scene().find(id)->transform.rotation.y, 30, "Spin advances at fixed-step rate", .001f);
    near(runtime.scene().find(id)->animation->time, 2.25f, "Animation advances by fixed delta and speed", .001f);
    near(runtime.editorScene().find(id)->transform.rotation.y, 0, "Play never mutates editor scene");
    runtime.pause();
    const auto pausedSteps = runtime.simulationSteps();
    runtime.update(.2f);
    check(runtime.simulationSteps() == pausedSteps, "Pause freezes simulation");
    near(runtime.scene().find(id)->animation->time, 2.25f, "Pause freezes animation", .001f);
    runtime.stepOnce();
    check(runtime.simulationSteps() == pausedSteps + 1, "Single step advances one fixed update");
    check(runtime.isPaused(), "Single step remains paused");
    runtime.scene().destroyEntity(id);
    runtime.stop();
    check(runtime.scene().find(id) != nullptr, "Stop restores entities deleted during play");
    near(runtime.scene().find(id)->transform.rotation.y, 0, "Stop restores authoring transform");
    near(runtime.scene().find(id)->animation->time, .25f, "Stop restores animation authoring offset");
    runtime.play(); runtime.update(5);
    check(runtime.simulationSteps() == 15, "Long frame clamps simulation to 15 fixed steps");
    runtime.stop(); runtime.play();
    runtime.update(-1); runtime.update(std::numeric_limits<float>::quiet_NaN());
    check(runtime.simulationSteps() == 0, "Invalid deltas do not alter simulation");
}

void physicsAndController() {
    EntityId id;
    Runtime runtime(physicsScene(id));
    runtime.play();
    for (int i = 0; i != 180; ++i) runtime.update(Runtime::FixedStep);
    near(runtime.scene().find(id)->transform.position.y, .5f, "Dynamic box rests on static collider", .005f);
    check(runtime.scene().find(id)->rigidBody->grounded, "Ground contact tracked");
    near(runtime.scene().find(id)->rigidBody->velocity.y, 0, "Resting vertical velocity resolves", .001f);
    runtime.stop();
    near(runtime.scene().find(id)->transform.position.y, 4, "Stopping physics restores authored position");
    runtime.scene().find(id)->script = ScriptComponent{"PlayerController", {{"speed", 3}, {"jumpSpeed", 6}}};
    runtime.play();
    for (int i = 0; i != 180; ++i) runtime.update(Runtime::FixedStep);
    RuntimeInput input;
    input.movement = {1, 0, 0}; input.jump = true;
    runtime.update(Runtime::FixedStep, input);
    check(runtime.scene().find(id)->rigidBody->velocity.y > 5, "Grounded controller jumps");
    near(runtime.scene().find(id)->rigidBody->velocity.x, 3, "Controller drives horizontal velocity");
    input.jump = false;
    for (int i = 0; i != 60; ++i) runtime.update(Runtime::FixedStep, input);
    check(runtime.scene().find(id)->transform.position.x > 2.9f, "Controller moves through simulation");

    Scene cameraScene;
    auto& camera = cameraScene.createEntity("Controlled Camera");
    const auto cameraId = camera.id;
    camera.camera = CameraComponent{};
    camera.script = ScriptComponent{"PlayerController", {{"lookSensitivity", 1}}};
    Runtime cameraRuntime(cameraScene); cameraRuntime.play();
    RuntimeInput look; look.lookDelta = {10, 0};
    cameraRuntime.update(.1f, look);
    near(cameraRuntime.scene().find(cameraId)->transform.rotation.y, -10, "Mouse delta applied once across multiple fixed steps");
}

struct Counts { int created = 0, updated = 0, destroyed = 0; };
class CountingBehaviour final : public Behaviour {
    Counts& m_counts;
public:
    explicit CountingBehaviour(Counts& counts) : m_counts(counts) {}
    void onCreate(Scene&, EntityId, const nlohmann::json&) override { ++m_counts.created; }
    void onFixedUpdate(Scene&, EntityId, float, const RuntimeInput&) override { ++m_counts.updated; }
    void onDestroy(Scene&, EntityId) override { ++m_counts.destroyed; }
};
class ThrowingBehaviour final : public Behaviour {
    Counts& m_counts;
public:
    explicit ThrowingBehaviour(Counts& counts) : m_counts(counts) {}
    void onFixedUpdate(Scene&, EntityId, float, const RuntimeInput&) override {
        ++m_counts.updated;
        throw std::runtime_error("Expected user callback failure");
    }
    void onDestroy(Scene&, EntityId) override { ++m_counts.destroyed; }
};
class RemovingBehaviour final : public Behaviour {
    Counts& m_counts;
public:
    explicit RemovingBehaviour(Counts& counts) : m_counts(counts) {}
    void onFixedUpdate(Scene& scene, EntityId id, float, const RuntimeInput&) override { ++m_counts.updated; scene.destroyEntity(id); }
    void onDestroy(Scene&, EntityId) override { ++m_counts.destroyed; }
};
class ReentrantBehaviour final : public Behaviour {
    Counts& m_counts;
    std::function<void()> m_action;
public:
    ReentrantBehaviour(Counts& counts, std::function<void()> action) : m_counts(counts), m_action(std::move(action)) {}
    void onFixedUpdate(Scene&, EntityId, float, const RuntimeInput&) override { ++m_counts.updated; m_action(); }
    void onDestroy(Scene&, EntityId) override { ++m_counts.destroyed; }
};
void behaviourLifecycle() {
    Scene scene;
    const auto id = scene.createEntity("Scripted").id;
    scene.find(id)->script = ScriptComponent{"Custom", nlohmann::json::object()};
    Counts counts;
    Runtime runtime(scene);
    runtime.behaviours().registerType("Custom", [&] { return std::make_unique<CountingBehaviour>(counts); });
    runtime.play(); runtime.update(Runtime::FixedStep);
    check(counts.created == 1 && counts.updated == 1, "Native registered behaviour creates and updates");
    runtime.scene().find(id)->enabled = false;
    runtime.update(Runtime::FixedStep);
    check(counts.destroyed == 1, "Disabling entity destroys active behaviour");
    runtime.scene().find(id)->enabled = true;
    runtime.update(Runtime::FixedStep);
    check(counts.created == 2, "Re-enabling entity recreates behaviour");
    runtime.stop();
    check(counts.destroyed == 2, "Stopping destroys every active behaviour exactly once");
    runtime.editorScene().find(id)->script->type = "NotRegistered";
    runtime.play();
    for (int i = 0; i != 10; ++i) runtime.update(Runtime::FixedStep);
    check(runtime.errors().size() == 1, "Unknown behaviour is reported once without crashing or spamming");
    runtime.scene().find(id)->script = ScriptComponent{"Spin", {{"degreesPerSecond", {0, 60, 0}}}};
    runtime.update(Runtime::FixedStep);
    near(runtime.scene().find(id)->transform.rotation.y, 1, "Replacing failed behaviour type starts the new behaviour");

    runtime.stop();
    runtime.editorScene().find(id)->script->type = "Throwing";
    Counts thrown;
    runtime.behaviours().registerType("Throwing", [&] { return std::make_unique<ThrowingBehaviour>(thrown); });
    runtime.play();
    for (int i = 0; i != 10; ++i) runtime.update(Runtime::FixedStep);
    check(thrown.updated == 1 && thrown.destroyed == 1, "Throwing user callback is disabled and destroyed once");
    check(runtime.errors().size() == 1, "Throwing callback reports one error");
    runtime.stop();
    runtime.editorScene().find(id)->script->type = "Removing";
    Counts removed;
    runtime.behaviours().registerType("Removing", [&] { return std::make_unique<RemovingBehaviour>(removed); });
    runtime.play();
    runtime.update(Runtime::FixedStep); runtime.update(Runtime::FixedStep);
    check(!runtime.scene().find(id) && removed.updated == 1 && removed.destroyed == 1, "Self-removing behaviour is cleaned without stale entity access");
    runtime.stop();
    runtime.editorScene().find(id)->script->type = "Reentrant";
    Counts reentrant;
    runtime.behaviours().registerType("Reentrant", [&] {
        return std::make_unique<ReentrantBehaviour>(reentrant, [&] { runtime.stop(); });
    });
    runtime.play();
    runtime.update(Runtime::FixedStep); runtime.update(Runtime::FixedStep);
    check(runtime.isPlaying() && runtime.scene().find(id), "Callback lifecycle reentry leaves runtime scene and iteration intact");
    check(reentrant.updated == 1 && reentrant.destroyed == 1 && runtime.errors().size() == 1,
        "Reentrant lifecycle callback is disabled, destroyed, and reported once");
    runtime.stop();
    check(runtime.state() == RuntimeState::Edit, "Host can stop normally after a rejected callback stop");
    runtime.behaviours().registerType("Reentrant", [&] {
        return std::make_unique<ReentrantBehaviour>(reentrant, [&] { runtime.update(Runtime::FixedStep); });
    });
    runtime.play();
    runtime.update(Runtime::FixedStep);
    check(runtime.simulationSteps() == 1 && runtime.errors().size() == 1, "Recursive update is rejected without nested simulation");
    runtime.stop();
}
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"hierarchy and stable IDs", hierarchy},
        {"complete scene serialization", serialization},
        {"malformed scene validation", malformedScenes},
        {"project import/export and path safety", projects},
        {"runtime play/pause/step/stop", runtimeControls},
        {"physics and native player controller", physicsAndController},
        {"native behaviour lifecycle", behaviourLifecycle},
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/' << tests.size() << " test groups passed; " << assertions << " assertions\n";
    return failed == 0 ? 0 : 1;
}
