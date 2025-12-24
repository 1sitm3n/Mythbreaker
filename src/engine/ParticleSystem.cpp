#include "ParticleSystem.h"
#include "Logger.h"
#include <algorithm>

using namespace myth;

void ParticleSystem::init(size_t maxParticles) {
    m_maxParticles = maxParticles;
    m_particles.resize(maxParticles);
    m_activeCount = 0;
    Logger::info("Particle system initialized (" + std::to_string(maxParticles) + " max particles)");
}

void ParticleSystem::update(float dt) {
    for (auto& emitter : m_emitters) {
        if (!emitter.active || emitter.type != EmitterType::Continuous) continue;
        emitter.emitTimer += dt;
        float emitInterval = 1.0f / emitter.emitRate;
        while (emitter.emitTimer >= emitInterval) {
            emit(emitter, 1);
            emitter.emitTimer -= emitInterval;
        }
    }
    size_t writeIdx = 0;
    for (size_t i = 0; i < m_activeCount; ++i) {
        Particle& p = m_particles[i];
        p.life -= dt;
        if (p.life > 0.0f) {
            p.velocity += glm::vec3(0.0f, -2.0f, 0.0f) * dt;
            p.position += p.velocity * dt;
            p.rotation += dt * 2.0f;
            float t = 1.0f - (p.life / p.maxLife);
            p.color.a = 1.0f - t;
            if (writeIdx != i) m_particles[writeIdx] = p;
            ++writeIdx;
        }
    }
    m_activeCount = writeIdx;
}

void ParticleSystem::clear() {
    m_activeCount = 0;
    m_emitters.clear();
}

int ParticleSystem::createEmitter(const ParticleEmitter& emitter) {
    m_emitters.push_back(emitter);
    return static_cast<int>(m_emitters.size() - 1);
}

void ParticleSystem::setEmitterPosition(int id, const glm::vec3& pos) {
    if (id >= 0 && id < static_cast<int>(m_emitters.size())) m_emitters[id].position = pos;
}

void ParticleSystem::activateEmitter(int id, bool active) {
    if (id >= 0 && id < static_cast<int>(m_emitters.size())) m_emitters[id].active = active;
}

void ParticleSystem::burstEmitter(int id) {
    if (id >= 0 && id < static_cast<int>(m_emitters.size())) emit(m_emitters[id], m_emitters[id].burstCount);
}

void ParticleSystem::removeEmitter(int id) {
    if (id >= 0 && id < static_cast<int>(m_emitters.size())) m_emitters[id].active = false;
}

void ParticleSystem::emit(const ParticleEmitter& emitter, int count) {
    for (int i = 0; i < count && m_activeCount < m_maxParticles; ++i) {
        Particle& p = m_particles[m_activeCount++];
        p.position = emitter.position;
        p.velocity = randomDirection(emitter.direction, emitter.spread) * randomFloat(emitter.minSpeed, emitter.maxSpeed);
        p.life = randomFloat(emitter.minLife, emitter.maxLife);
        p.maxLife = p.life;
        p.size = randomFloat(emitter.minSize, emitter.maxSize);
        p.color = emitter.startColor;
        p.rotation = randomFloat(0.0f, 6.28f);
    }
}

glm::vec3 ParticleSystem::randomDirection(const glm::vec3& dir, float spread) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    glm::vec3 random(dist(m_rng), dist(m_rng), dist(m_rng));
    return glm::normalize(dir + random * spread);
}

float ParticleSystem::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(m_rng);
}

void ParticleSystem::spawnDust(const glm::vec3& position, int count) {
    ParticleEmitter e;
    e.position = position;
    e.direction = glm::vec3(0.0f, 0.5f, 0.0f);
    e.spread = 2.0f; e.minSpeed = 0.3f; e.maxSpeed = 1.0f;
    e.minLife = 0.4f; e.maxLife = 1.0f; e.minSize = 0.15f; e.maxSize = 0.35f;
    e.startColor = glm::vec4(0.7f, 0.6f, 0.5f, 0.8f);
    emit(e, count);
}

void ParticleSystem::spawnSparks(const glm::vec3& position, int count) {
    Logger::infof("Spawning {} sparks at ({:.1f},{:.1f},{:.1f}), active before: {}", count, position.x, position.y, position.z, m_activeCount);
    ParticleEmitter e;
    e.position = position;
    e.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    e.spread = 2.0f; e.minSpeed = 3.0f; e.maxSpeed = 8.0f;
    e.minLife = 0.3f; e.maxLife = 0.7f; e.minSize = 0.3f; e.maxSize = 0.6f;
    e.startColor = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
    emit(e, count);
}

void ParticleSystem::spawnMagic(const glm::vec3& position, const glm::vec4& color, int count) {
    ParticleEmitter e;
    e.position = position;
    e.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    e.spread = 1.0f; e.minSpeed = 1.0f; e.maxSpeed = 3.0f;
    e.minLife = 0.5f; e.maxLife = 1.2f; e.minSize = 0.05f; e.maxSize = 0.12f;
    e.startColor = color;
    emit(e, count);
}

void ParticleSystem::spawnPickup(const glm::vec3& position, int count) {
    Logger::infof("Spawning {} pickup particles at ({:.1f},{:.1f},{:.1f}), active before: {}", count, position.x, position.y, position.z, m_activeCount);
    ParticleEmitter e;
    e.position = position;
    e.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    e.spread = 0.8f; e.minSpeed = 2.0f; e.maxSpeed = 4.0f;
    e.minLife = 0.6f; e.maxLife = 1.2f; e.minSize = 0.4f; e.maxSize = 0.8f;
    e.startColor = glm::vec4(1.0f, 0.9f, 0.3f, 1.0f);
    emit(e, count);
}

void ParticleSystem::spawnBlood(const glm::vec3& position, int count) {
    ParticleEmitter e;
    e.position = position;
    e.direction = glm::vec3(0.0f, 0.5f, 0.0f);
    e.spread = 1.8f; e.minSpeed = 2.0f; e.maxSpeed = 5.0f;
    e.minLife = 0.3f; e.maxLife = 0.7f; e.minSize = 0.03f; e.maxSize = 0.07f;
    e.startColor = glm::vec4(0.7f, 0.1f, 0.1f, 0.9f);
    emit(e, count);
}


