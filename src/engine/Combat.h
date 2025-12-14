#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace myth {

// Combat stats for any entity that can fight
struct Combat {
    float damage = 10.0f;
    float attackRange = 2.0f;
    float attackCooldown = 1.0f;
    float cooldownTimer = 0.0f;
    bool isAttacking = false;
    float attackDuration = 0.3f;
    float attackTimer = 0.0f;
    
    bool canAttack() const { return cooldownTimer <= 0.0f && !isAttacking; }
    
    void startAttack() {
        if (canAttack()) {
            isAttacking = true;
            attackTimer = attackDuration;
            cooldownTimer = attackCooldown;
        }
    }
    
    void update(float dt) {
        if (cooldownTimer > 0.0f) cooldownTimer -= dt;
        if (isAttacking) {
            attackTimer -= dt;
            if (attackTimer <= 0.0f) isAttacking = false;
        }
    }
};

// Health for enemies (simpler than full Stats)
struct Health {
    float current = 50.0f;
    float max = 50.0f;
    bool isDead = false;
    
    void takeDamage(float amount) {
        if (isDead) return;
        current -= amount;
        if (current <= 0.0f) {
            current = 0.0f;
            isDead = true;
        }
    }
    
    void heal(float amount) {
        if (isDead) return;
        current = (std::min)(max, current + amount);
    }
    
    float percent() const { return max > 0 ? current / max : 0.0f; }
};

// AI states for enemies
enum class AIState : uint8_t {
    Idle,
    Patrol,
    Chase,
    Attack,
    Dead
};

// Enemy AI component
struct Enemy {
    AIState state = AIState::Idle;
    float aggroRange = 15.0f;
    float attackRange = 2.0f;
    float moveSpeed = 4.0f;
    float damage = 10.0f;
    float attackCooldown = 1.5f;
    float cooldownTimer = 0.0f;
    
    // Patrol
    glm::vec3 homePosition{0.0f};
    glm::vec3 patrolTarget{0.0f};
    float patrolRadius = 10.0f;
    float patrolWaitTimer = 0.0f;
    
    // Visual feedback
    float hitFlashTimer = 0.0f;
    
    bool canAttack() const { return cooldownTimer <= 0.0f; }
    
    void update(float dt) {
        if (cooldownTimer > 0.0f) cooldownTimer -= dt;
        if (hitFlashTimer > 0.0f) hitFlashTimer -= dt;
        if (patrolWaitTimer > 0.0f) patrolWaitTimer -= dt;
    }
};

// Tag for enemy entities
struct EnemyTag {};

} // namespace myth
