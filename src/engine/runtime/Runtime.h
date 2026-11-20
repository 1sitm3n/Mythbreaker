#pragma once

#include "Scene.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace myth::engine {

struct RuntimeInput {
    glm::vec3 movement{0.0f}; // Desired world-space direction, length clamped to one.
    glm::vec2 lookDelta{0.0f};
    bool jump = false;
};

// Applications can register native C++ behaviours without modifying the renderer
// or editor. Scene files refer to the registered type by name and store its data.
// Callbacks may change scene entities. Runtime lifecycle/update methods are host
// operations and cannot be called recursively from a factory or callback.
class Behaviour {
public:
    virtual ~Behaviour() = default;
    virtual void onCreate(Scene&, EntityId, const nlohmann::json&) {}
    virtual void onFixedUpdate(Scene&, EntityId, float, const RuntimeInput&) {}
    virtual void onDestroy(Scene&, EntityId) {}
};

class BehaviourRegistry {
public:
    using Factory = std::function<std::unique_ptr<Behaviour>()>;
    void registerType(const std::string& name, Factory factory);
    bool contains(const std::string& name) const;
    std::unique_ptr<Behaviour> create(const std::string& name) const;
    std::vector<std::string> types() const;
private:
    std::unordered_map<std::string, Factory> m_factories;
};

enum class RuntimeState { Edit, Playing, Paused };

class Runtime {
public:
    Runtime();
    explicit Runtime(Scene scene);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Scene& scene();
    const Scene& scene() const;
    Scene& editorScene() { return m_editScene; }
    const Scene& editorScene() const { return m_editScene; }
    void setScene(Scene scene);

    void play(); // Clones the edit scene; play changes never mutate authoring data.
    void stop(); // Restores the authoring scene, including deleted runtime entities.
    void pause();
    void resume();
    void stepOnce(const RuntimeInput& input = {});
    void update(float frameDelta, const RuntimeInput& input = {});

    RuntimeState state() const { return m_state; }
    bool isPlaying() const { return m_state != RuntimeState::Edit; }
    bool isPaused() const { return m_state == RuntimeState::Paused; }
    double simulationTime() const { return m_simulationTime; }
    std::uint64_t simulationSteps() const { return m_simulationSteps; }
    float interpolationAlpha() const;
    BehaviourRegistry& behaviours() { return m_registry; }
    const BehaviourRegistry& behaviours() const { return m_registry; }
    const std::vector<std::string>& errors() const { return m_errors; }
    void clearErrors() { m_errors.clear(); }

    static constexpr float FixedStep = 1.0f / 60.0f;
    static constexpr int MaxStepsPerFrame = 15;

private:
    struct ActiveBehaviour {
        EntityId entity = InvalidEntity;
        std::string type;
        std::unique_ptr<Behaviour> instance;
    };
    void reconcileBehaviours();
    void destroyBehaviours() noexcept;
    void fixedUpdate(float delta, const RuntimeInput& input);
    void updatePhysics(float delta);
    void reportError(const std::string& message);
    void requireHostCall(const char* operation) const;

    Scene m_editScene;
    Scene m_playScene;
    RuntimeState m_state = RuntimeState::Edit;
    BehaviourRegistry m_registry;
    std::vector<ActiveBehaviour> m_activeBehaviours;
    std::unordered_map<EntityId, std::string> m_failedBehaviours;
    std::vector<std::string> m_errors;
    double m_accumulator = 0.0;
    double m_simulationTime = 0.0;
    std::uint64_t m_simulationSteps = 0;
    glm::vec2 m_pendingLook{0.0f};
    bool m_pendingJump = false;
    unsigned m_callbackDepth = 0;
};

} // namespace myth::engine
