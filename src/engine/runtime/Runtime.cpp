#include "Runtime.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace myth::engine {
namespace {
bool finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

struct CallbackScope {
    unsigned& depth;
    explicit CallbackScope(unsigned& value) : depth(value) { ++depth; }
    ~CallbackScope() { --depth; }
};

float propertyFloat(const nlohmann::json& properties, const char* key, float fallback) {
    if (!properties.contains(key)) return fallback;
    if (!properties.at(key).is_number()) throw std::runtime_error(std::string(key) + " must be a number");
    const auto value = properties.at(key).get<float>();
    if (!std::isfinite(value)) throw std::runtime_error(std::string(key) + " must be finite");
    return value;
}

glm::vec3 propertyVector(const nlohmann::json& properties, const char* key, glm::vec3 fallback) {
    if (!properties.contains(key)) return fallback;
    const auto& value = properties.at(key);
    if (!value.is_array() || value.size() != 3) throw std::runtime_error(std::string(key) + " must contain three numbers");
    for (int i = 0; i != 3; ++i) {
        if (!value[static_cast<std::size_t>(i)].is_number()) throw std::runtime_error(std::string(key) + " contains a non-number");
        fallback[i] = value[static_cast<std::size_t>(i)].get<float>();
    }
    if (!finite(fallback)) throw std::runtime_error(std::string(key) + " must be finite");
    return fallback;
}

void setWorldPosition(Scene& scene, EntityId id, const glm::vec3& position) {
    auto* entity = scene.find(id);
    if (!entity) return;
    if (!entity->parent) entity->transform.position = position;
    else entity->transform.position = glm::vec3(glm::inverse(scene.worldMatrix(entity->parent)) * glm::vec4(position, 1.0f));
}

class SpinBehaviour final : public Behaviour {
    glm::vec3 m_speed{0, 30, 0};
public:
    void onCreate(Scene&, EntityId, const nlohmann::json& properties) override {
        m_speed = propertyVector(properties, "degreesPerSecond", m_speed);
    }
    void onFixedUpdate(Scene& scene, EntityId id, float delta, const RuntimeInput&) override {
        if (auto* entity = scene.find(id)) {
            entity->transform.rotation += m_speed * delta;
            for (int axis = 0; axis != 3; ++axis) entity->transform.rotation[axis] = std::fmod(entity->transform.rotation[axis], 360.0f);
        }
    }
};

class OscillateBehaviour final : public Behaviour {
    glm::vec3 m_origin{0};
    glm::vec3 m_axis{0, 1, 0};
    float m_amplitude = 1;
    float m_frequency = 1;
    float m_phase = 0;
    double m_time = 0;
public:
    void onCreate(Scene& scene, EntityId id, const nlohmann::json& properties) override {
        m_origin = scene.find(id)->transform.position;
        m_axis = propertyVector(properties, "axis", m_axis);
        if (glm::length(m_axis) < 1e-6f) throw std::runtime_error("Oscillate axis must be non-zero");
        m_axis = glm::normalize(m_axis);
        m_amplitude = propertyFloat(properties, "amplitude", 1);
        m_frequency = propertyFloat(properties, "frequency", 1);
        m_phase = propertyFloat(properties, "phase", 0);
        if (m_amplitude < 0 || m_frequency < 0) throw std::runtime_error("Oscillate amplitude and frequency must be non-negative");
    }
    void onFixedUpdate(Scene& scene, EntityId id, float delta, const RuntimeInput&) override {
        m_time += delta;
        if (auto* entity = scene.find(id))
            entity->transform.position = m_origin + m_axis * (m_amplitude * static_cast<float>(std::sin(m_time * m_frequency * 6.283185307179586 + m_phase)));
    }
};

class PlayerControllerBehaviour final : public Behaviour {
    float m_speed = 5;
    float m_jumpSpeed = 6;
    float m_lookSensitivity = 0.12f;
public:
    void onCreate(Scene&, EntityId, const nlohmann::json& properties) override {
        m_speed = propertyFloat(properties, "speed", m_speed);
        m_jumpSpeed = propertyFloat(properties, "jumpSpeed", m_jumpSpeed);
        m_lookSensitivity = propertyFloat(properties, "lookSensitivity", m_lookSensitivity);
        if (m_speed < 0 || m_jumpSpeed < 0 || m_lookSensitivity < 0) throw std::runtime_error("PlayerController properties must be non-negative");
    }
    void onFixedUpdate(Scene& scene, EntityId id, float delta, const RuntimeInput& input) override {
        auto* entity = scene.find(id);
        if (!entity) return;
        glm::vec3 direction = input.movement;
        if (!finite(direction)) direction = glm::vec3(0);
        if (glm::length(direction) > 1.0f) direction = glm::normalize(direction);
        if (entity->rigidBody && entity->rigidBody->dynamic) {
            auto& body = *entity->rigidBody;
            body.velocity.x = direction.x * m_speed;
            body.velocity.z = direction.z * m_speed;
            if (!body.useGravity) body.velocity.y = direction.y * m_speed;
            if (input.jump && body.grounded) {
                body.velocity.y = m_jumpSpeed;
                body.grounded = false;
            }
        } else {
            const auto position = glm::vec3(scene.worldMatrix(id)[3]);
            setWorldPosition(scene, id, position + direction * m_speed * delta);
        }
        if (std::isfinite(input.lookDelta.x) && std::isfinite(input.lookDelta.y)) {
            entity->transform.rotation.y -= input.lookDelta.x * m_lookSensitivity;
            if (entity->camera) entity->transform.rotation.x = std::clamp(entity->transform.rotation.x - input.lookDelta.y * m_lookSensitivity, -89.0f, 89.0f);
        }
    }
};

struct Bounds {
    glm::vec3 center{0};
    glm::vec3 extent{0.5f};
};

Bounds bounds(const Scene& scene, EntityId id) {
    const auto* entity = scene.find(id);
    const auto matrix = scene.worldMatrix(id);
    Bounds result;
    result.center = glm::vec3(matrix[3]);
    // This is the enclosing world-axis-aligned box after scaling/rotation. It is
    // intentionally conservative; this solver is not an oriented-box solver.
    const glm::mat3 basis(matrix);
    const auto half = entity->rigidBody->halfExtents;
    result.extent = glm::abs(basis[0]) * half.x + glm::abs(basis[1]) * half.y + glm::abs(basis[2]) * half.z;
    return result;
}

bool isAncestor(const Scene& scene, EntityId ancestor, EntityId id) {
    std::unordered_set<EntityId> visited;
    for (const auto* entity = scene.find(id); entity && entity->parent; entity = scene.find(entity->parent)) {
        if (entity->parent == ancestor) return true;
        if (!visited.insert(entity->id).second) break;
    }
    return false;
}
} // namespace

void BehaviourRegistry::registerType(const std::string& name, Factory factory) {
    if (name.empty() || name.size() > 128 || !factory) throw std::runtime_error("A behaviour needs a non-empty name and factory");
    m_factories[name] = std::move(factory);
}

bool BehaviourRegistry::contains(const std::string& name) const { return m_factories.contains(name); }
std::unique_ptr<Behaviour> BehaviourRegistry::create(const std::string& name) const {
    const auto it = m_factories.find(name);
    return it == m_factories.end() ? nullptr : it->second();
}
std::vector<std::string> BehaviourRegistry::types() const {
    std::vector<std::string> result;
    for (const auto& [name, factory] : m_factories) { (void)factory; result.push_back(name); }
    std::sort(result.begin(), result.end());
    return result;
}

Runtime::Runtime() : Runtime(Scene::createDefault()) {}
Runtime::Runtime(Scene scene) : m_editScene(std::move(scene)) {
    m_editScene.validate();
    m_registry.registerType("Spin", [] { return std::make_unique<SpinBehaviour>(); });
    m_registry.registerType("Oscillate", [] { return std::make_unique<OscillateBehaviour>(); });
    m_registry.registerType("PlayerController", [] { return std::make_unique<PlayerControllerBehaviour>(); });
}
Runtime::~Runtime() { destroyBehaviours(); }

Scene& Runtime::scene() { return isPlaying() ? m_playScene : m_editScene; }
const Scene& Runtime::scene() const { return isPlaying() ? m_playScene : m_editScene; }
void Runtime::setScene(Scene scene) {
    requireHostCall("setScene");
    scene.validate();
    stop();
    m_editScene = std::move(scene);
}

void Runtime::play() {
    requireHostCall("play");
    if (m_state == RuntimeState::Paused) { resume(); return; }
    if (m_state == RuntimeState::Playing) return;
    m_editScene.validate();
    m_playScene = m_editScene.clone();
    m_accumulator = 0;
    m_simulationTime = 0;
    m_simulationSteps = 0;
    m_pendingLook = glm::vec2(0);
    m_pendingJump = false;
    m_failedBehaviours.clear();
    m_errors.clear();
    m_state = RuntimeState::Playing;
    reconcileBehaviours();
}

void Runtime::stop() {
    requireHostCall("stop");
    destroyBehaviours();
    m_playScene = Scene{};
    m_state = RuntimeState::Edit;
    m_accumulator = 0;
    m_pendingLook = glm::vec2(0);
    m_pendingJump = false;
    m_failedBehaviours.clear();
}

void Runtime::pause() {
    requireHostCall("pause");
    if (m_state == RuntimeState::Playing) m_state = RuntimeState::Paused;
    m_pendingLook = glm::vec2(0); m_pendingJump = false;
}
void Runtime::resume() {
    requireHostCall("resume");
    if (m_state == RuntimeState::Paused) m_state = RuntimeState::Playing;
}
void Runtime::stepOnce(const RuntimeInput& input) {
    requireHostCall("stepOnce");
    if (m_state == RuntimeState::Paused) fixedUpdate(FixedStep, input);
}

void Runtime::update(float frameDelta, const RuntimeInput& input) {
    requireHostCall("update");
    if (m_state != RuntimeState::Playing || !std::isfinite(frameDelta) || frameDelta <= 0.0f) return;
    m_accumulator += std::min(static_cast<double>(frameDelta), 0.25);
    if (std::isfinite(input.lookDelta.x) && std::isfinite(input.lookDelta.y)) m_pendingLook += input.lookDelta;
    m_pendingJump = m_pendingJump || input.jump;
    int steps = 0;
    constexpr double fixedDuration = 1.0 / 60.0;
    while (m_accumulator + 1e-10 >= fixedDuration && steps < MaxStepsPerFrame) {
        auto stepInput = input;
        stepInput.lookDelta = m_pendingLook;
        stepInput.jump = m_pendingJump;
        m_pendingLook = glm::vec2(0); m_pendingJump = false;
        fixedUpdate(FixedStep, stepInput);
        m_accumulator = std::max(0.0, m_accumulator - fixedDuration);
        ++steps;
    }
    // Avoid a spiral of death after a stall. The fixed step itself never grows.
    if (steps == MaxStepsPerFrame && m_accumulator >= fixedDuration) m_accumulator = std::fmod(m_accumulator, fixedDuration);
}

float Runtime::interpolationAlpha() const { return static_cast<float>(m_accumulator / FixedStep); }
void Runtime::reportError(const std::string& message) {
    if (m_errors.size() == 128) m_errors.erase(m_errors.begin());
    m_errors.push_back(message);
}

void Runtime::requireHostCall(const char* operation) const {
    if (m_callbackDepth != 0)
        throw std::runtime_error(std::string("Runtime::") + operation + " must be called by the host outside behaviour callbacks");
}

void Runtime::destroyBehaviours() noexcept {
    for (auto& active : m_activeBehaviours) {
        try { CallbackScope callback(m_callbackDepth); active.instance->onDestroy(m_playScene, active.entity); }
        catch (...) { /* Destruction must remain safe after a failed user callback. */ }
    }
    m_activeBehaviours.clear();
}

void Runtime::reconcileBehaviours() {
    std::erase_if(m_failedBehaviours, [this](const auto& failure) {
        const auto* entity = m_playScene.find(failure.first);
        return !entity || !entity->script || !m_playScene.isEnabled(entity->id) || entity->script->type != failure.second;
    });
    for (std::size_t index = 0; index < m_activeBehaviours.size();) {
        auto& active = m_activeBehaviours[index];
        const auto* entity = m_playScene.find(active.entity);
        if (!entity || !m_playScene.isEnabled(active.entity) || !entity->script || entity->script->type != active.type) {
            try { CallbackScope callback(m_callbackDepth); active.instance->onDestroy(m_playScene, active.entity); }
            catch (const std::exception& error) { reportError("Behaviour destroy: " + std::string(error.what())); }
            catch (...) { reportError("Behaviour destroy: unknown exception"); }
            m_activeBehaviours.erase(m_activeBehaviours.begin() + static_cast<std::ptrdiff_t>(index));
        } else ++index;
    }
    std::unordered_set<EntityId> activeIds;
    for (const auto& active : m_activeBehaviours) activeIds.insert(active.entity);
    std::vector<EntityId> candidates;
    for (const auto& entity : m_playScene.entities())
        if (entity.script && m_playScene.isEnabled(entity.id) && !activeIds.contains(entity.id)) candidates.push_back(entity.id);
    for (const auto id : candidates) {
        const auto* entity = m_playScene.find(id);
        if (!entity || !entity->script || !m_playScene.isEnabled(id)) continue;
        const auto script = *entity->script;
        const auto failed = m_failedBehaviours.find(id);
        if (failed != m_failedBehaviours.end() && failed->second == script.type) continue;
        try {
            CallbackScope callback(m_callbackDepth);
            auto behaviour = m_registry.create(script.type);
            if (!behaviour) throw std::runtime_error("Unregistered behaviour: " + script.type);
            behaviour->onCreate(m_playScene, id, script.properties);
            m_activeBehaviours.push_back({id, script.type, std::move(behaviour)});
        } catch (const std::exception& error) {
            reportError("Entity " + std::to_string(id) + ": " + error.what());
            m_failedBehaviours[id] = script.type;
        } catch (...) {
            reportError("Entity " + std::to_string(id) + ": unknown behaviour creation exception");
            m_failedBehaviours[id] = script.type;
        }
    }
}

void Runtime::fixedUpdate(float delta, const RuntimeInput& input) {
    reconcileBehaviours();
    for (auto& active : m_activeBehaviours) {
        if (!m_playScene.find(active.entity) || !m_playScene.isEnabled(active.entity)) continue;
        try { CallbackScope callback(m_callbackDepth); active.instance->onFixedUpdate(m_playScene, active.entity, delta, input); }
        catch (const std::exception& error) {
            reportError("Entity " + std::to_string(active.entity) + " update: " + error.what());
            m_failedBehaviours[active.entity] = active.type;
        } catch (...) {
            reportError("Entity " + std::to_string(active.entity) + " update: unknown exception");
            m_failedBehaviours[active.entity] = active.type;
        }
    }
    for (std::size_t index = 0; index < m_activeBehaviours.size();) {
        const auto& active = m_activeBehaviours[index];
        if (m_failedBehaviours.contains(active.entity)) {
            try { CallbackScope callback(m_callbackDepth); active.instance->onDestroy(m_playScene, active.entity); } catch (...) {}
            m_activeBehaviours.erase(m_activeBehaviours.begin() + static_cast<std::ptrdiff_t>(index));
        } else ++index;
    }
    updatePhysics(delta);
    for (auto& entity : m_playScene.entities()) {
        if (entity.animation && entity.animation->playing && m_playScene.isEnabled(entity.id))
            entity.animation->time += delta * entity.animation->speed;
    }
    m_simulationTime += delta;
    ++m_simulationSteps;
}

void Runtime::updatePhysics(float delta) {
    std::vector<EntityId> bodies;
    float smallestExtent = 1.0f;
    float largestTravel = 0.0f;
    for (auto& entity : m_playScene.entities()) {
        if (!entity.rigidBody || !m_playScene.isEnabled(entity.id)) continue;
        auto& body = *entity.rigidBody;
        if (!finite(body.velocity) || !finite(body.halfExtents) || glm::any(glm::lessThanEqual(body.halfExtents, glm::vec3(0))) || body.mass < 0.000001f || !std::isfinite(body.mass)) {
            reportError("Skipping invalid physics body " + std::to_string(entity.id));
            continue;
        }
        body.grounded = false;
        bodies.push_back(entity.id);
        const auto box = bounds(m_playScene, entity.id);
        smallestExtent = std::min(smallestExtent, std::max(0.005f, std::min({box.extent.x, box.extent.y, box.extent.z})));
        if (body.dynamic) largestTravel = std::max(largestTravel, glm::length(body.velocity) * delta);
    }
    // Adaptive substeps reduce tunneling for ordinary gameplay speeds. This is not
    // continuous collision detection; very fast projectiles need swept queries.
    const int substeps = static_cast<int>(std::clamp(std::ceil(largestTravel / std::max(0.005f, smallestExtent * 0.5f)), 1.0f, 32.0f));
    const float step = delta / static_cast<float>(substeps);
    for (int substep = 0; substep < substeps; ++substep) {
        for (const auto id : bodies) {
            auto* entity = m_playScene.find(id);
            auto& body = *entity->rigidBody;
            if (!body.dynamic) continue;
            if (body.useGravity) body.velocity += m_playScene.environment.gravity * step;
            setWorldPosition(m_playScene, id, glm::vec3(m_playScene.worldMatrix(id)[3]) + body.velocity * step);
        }
        for (int iteration = 0; iteration != 4; ++iteration) {
            for (std::size_t a = 0; a < bodies.size(); ++a) {
                for (std::size_t b = a + 1; b < bodies.size(); ++b) {
                    const auto aId = bodies[a], bId = bodies[b];
                    auto& aBody = *m_playScene.find(aId)->rigidBody;
                    auto& bBody = *m_playScene.find(bId)->rigidBody;
                    if ((!aBody.dynamic && !bBody.dynamic) || isAncestor(m_playScene, aId, bId) || isAncestor(m_playScene, bId, aId)) continue;
                    const auto aBounds = bounds(m_playScene, aId), bBounds = bounds(m_playScene, bId);
                    const auto difference = bBounds.center - aBounds.center;
                    const auto penetration = aBounds.extent + bBounds.extent - glm::abs(difference);
                    if (penetration.x < -0.0001f || penetration.y < -0.0001f || penetration.z < -0.0001f) continue;
                    int axis = penetration.y < penetration.x ? 1 : 0;
                    if (penetration.z < penetration[axis]) axis = 2;
                    glm::vec3 normal(0);
                    normal[axis] = difference[axis] >= 0 ? 1.0f : -1.0f;
                    const float inverseA = aBody.dynamic ? 1.0f / aBody.mass : 0.0f;
                    const float inverseB = bBody.dynamic ? 1.0f / bBody.mass : 0.0f;
                    const float inverseSum = inverseA + inverseB;
                    const float depth = std::max(0.0f, penetration[axis]);
                    if (aBody.dynamic) setWorldPosition(m_playScene, aId, aBounds.center - normal * (depth * inverseA / inverseSum));
                    if (bBody.dynamic) setWorldPosition(m_playScene, bId, bBounds.center + normal * (depth * inverseB / inverseSum));
                    if (normal.y < -0.5f && aBody.dynamic) aBody.grounded = true;
                    if (normal.y > 0.5f && bBody.dynamic) bBody.grounded = true;
                    const auto aVelocity = aBody.dynamic ? aBody.velocity : glm::vec3(0);
                    const auto bVelocity = bBody.dynamic ? bBody.velocity : glm::vec3(0);
                    const float relativeSpeed = glm::dot(bVelocity - aVelocity, normal);
                    if (relativeSpeed < 0.0f) {
                        const float restitution = std::abs(relativeSpeed) < 0.5f ? 0.0f : std::min(aBody.restitution, bBody.restitution);
                        const float impulse = -(1.0f + restitution) * relativeSpeed / inverseSum;
                        if (aBody.dynamic) aBody.velocity -= normal * (impulse * inverseA);
                        if (bBody.dynamic) bBody.velocity += normal * (impulse * inverseB);
                    }
                }
            }
        }
    }
}

} // namespace myth::engine
