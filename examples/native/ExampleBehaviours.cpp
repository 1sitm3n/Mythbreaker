#include "engine/runtime/GameModule.h"
#include <cmath>
#include <stdexcept>

namespace myth::engine {
namespace {
class Orbit final : public Behaviour {
    glm::vec3 m_center{0};
    float m_radius = 2, m_speed = 45, m_angle = 0;
public:
    void onCreate(Scene& scene, EntityId id, const nlohmann::json& properties) override {
        m_radius = properties.value("radius", 2.0f);
        m_speed = properties.value("degreesPerSecond", 45.0f);
        if (!std::isfinite(m_radius) || !std::isfinite(m_speed) || m_radius < 0)
            throw std::runtime_error("Orbit needs a non-negative finite radius and finite angular speed.");
        if (const auto* entity = scene.find(id)) m_center = entity->transform.position - glm::vec3(m_radius, 0, 0);
    }
    void onFixedUpdate(Scene& scene, EntityId id, float delta, const RuntimeInput&) override {
        m_angle += glm::radians(m_speed) * delta;
        if (auto* entity = scene.find(id)) entity->transform.position = m_center + glm::vec3(std::cos(m_angle) * m_radius, 0, std::sin(m_angle) * m_radius);
    }
};
class PulseLight final : public Behaviour {
    float m_base = 2, m_amplitude = 1, m_frequency = 0.5f, m_time = 0;
public:
    void onCreate(Scene&, EntityId, const nlohmann::json& properties) override {
        m_base = properties.value("baseIntensity", 2.0f);
        m_amplitude = properties.value("amplitude", 1.0f);
        m_frequency = properties.value("frequency", 0.5f);
        if (!std::isfinite(m_base) || !std::isfinite(m_amplitude) || !std::isfinite(m_frequency))
            throw std::runtime_error("PulseLight properties must be finite.");
    }
    void onFixedUpdate(Scene& scene, EntityId id, float delta, const RuntimeInput&) override {
        m_time += delta;
        if (auto* entity = scene.find(id); entity && entity->light)
            entity->light->intensity = std::max(0.0f, m_base + m_amplitude * std::sin(m_time * m_frequency * 6.2831853f));
    }
};
}
void registerGameBehaviours(BehaviourRegistry& registry) {
    registry.registerType("Orbit", [] { return std::make_unique<Orbit>(); });
    registry.registerType("PulseLight", [] { return std::make_unique<PulseLight>(); });
}
}
