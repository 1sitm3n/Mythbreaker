#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <random>

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 color;
    float life;
    float maxLife;
    float size;
    float rotation;
};

enum class EmitterType {
    Burst,      // All particles at once
    Continuous  // Steady stream
};

struct ParticleEmitter {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, 1.0f, 0.0f);
    float spread = 0.5f;           // Cone spread angle
    float minSpeed = 1.0f;
    float maxSpeed = 3.0f;
    float minLife = 0.5f;
    float maxLife = 2.0f;
    float minSize = 0.05f;
    float maxSize = 0.15f;
    glm::vec4 startColor = glm::vec4(1.0f);
    glm::vec4 endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    glm::vec3 gravity = glm::vec3(0.0f, -2.0f, 0.0f);
    EmitterType type = EmitterType::Burst;
    float emitRate = 10.0f;  // Particles per second for continuous
    int burstCount = 10;     // Particles per burst
    bool active = false;
    float emitTimer = 0.0f;
};

class ParticleSystem {
public:
    static ParticleSystem& instance() {
        static ParticleSystem inst;
        return inst;
    }
    
    void init(size_t maxParticles = 2000);
    void update(float dt);
    void clear();
    
    // Emitter management
    int createEmitter(const ParticleEmitter& emitter);
    void setEmitterPosition(int id, const glm::vec3& pos);
    void activateEmitter(int id, bool active);
    void burstEmitter(int id);
    void removeEmitter(int id);
    
    // Quick effects (create temporary burst emitters)
    void spawnDust(const glm::vec3& position, int count = 8);
    void spawnSparks(const glm::vec3& position, int count = 15);
    void spawnMagic(const glm::vec3& position, const glm::vec4& color, int count = 20);
    void spawnPickup(const glm::vec3& position, int count = 12);
    void spawnBlood(const glm::vec3& position, int count = 10);
    void spawnRain(const glm::vec3& position, int count = 5);
    
    const std::vector<Particle>& getParticles() const { return m_particles; }
    size_t getActiveCount() const { return m_activeCount; }
    
private:
    ParticleSystem() = default;
    void emit(const ParticleEmitter& emitter, int count);
    glm::vec3 randomDirection(const glm::vec3& dir, float spread);
    float randomFloat(float min, float max);
    
    std::vector<Particle> m_particles;
    std::vector<ParticleEmitter> m_emitters;
    size_t m_activeCount = 0;
    size_t m_maxParticles = 2000;
    std::mt19937 m_rng{std::random_device{}()};
};

