#pragma once

#include <algorithm>

namespace myth {

struct Stats {
    // Health
    float health = 100.0f;
    float maxHealth = 100.0f;
    float healthRegen = 1.0f;  // per second
    
    // Stamina
    float stamina = 100.0f;
    float maxStamina = 100.0f;
    float staminaRegen = 15.0f;  // per second
    float staminaRegenDelay = 1.0f;  // seconds after use before regen starts
    float staminaRegenTimer = 0.0f;
    
    // Mana
    float mana = 100.0f;
    float maxMana = 100.0f;
    float manaRegen = 5.0f;  // per second
    
    // State
    bool isDead = false;
    bool isExhausted = false;  // stamina too low
    
    // Costs
    static constexpr float SPRINT_COST = 20.0f;  // per second
    static constexpr float JUMP_COST = 15.0f;    // per jump
    static constexpr float EXHAUSTED_THRESHOLD = 5.0f;
    static constexpr float RECOVER_THRESHOLD = 20.0f;
    
    void update(float dt) {
        if (isDead) return;
        
        // Health regen (only if not at max)
        if (health < maxHealth) {
            health = std::min(maxHealth, health + healthRegen * dt);
        }
        
        // Stamina regen (with delay after use)
        if (staminaRegenTimer > 0.0f) {
            staminaRegenTimer -= dt;
        } else if (stamina < maxStamina) {
            stamina = std::min(maxStamina, stamina + staminaRegen * dt);
        }
        
        // Mana regen
        if (mana < maxMana) {
            mana = std::min(maxMana, mana + manaRegen * dt);
        }
        
        // Exhaustion state
        if (isExhausted && stamina >= RECOVER_THRESHOLD) {
            isExhausted = false;
        } else if (!isExhausted && stamina < EXHAUSTED_THRESHOLD) {
            isExhausted = true;
        }
        
        // Death check
        if (health <= 0.0f) {
            health = 0.0f;
            isDead = true;
        }
    }
    
    bool useStamina(float amount) {
        if (stamina >= amount) {
            stamina -= amount;
            staminaRegenTimer = staminaRegenDelay;
            return true;
        }
        return false;
    }
    
    bool canSprint() const {
        return !isExhausted && stamina > EXHAUSTED_THRESHOLD;
    }
    
    bool canJump() const {
        return !isExhausted && stamina >= JUMP_COST;
    }
    
    void takeDamage(float amount) {
        if (isDead) return;
        health = std::max(0.0f, health - amount);
        if (health <= 0.0f) {
            isDead = true;
        }
    }
    
    void heal(float amount) {
        if (isDead) return;
        health = std::min(maxHealth, health + amount);
    }
    
    void respawn() {
        health = maxHealth;
        stamina = maxStamina;
        mana = maxMana;
        isDead = false;
        isExhausted = false;
        staminaRegenTimer = 0.0f;
    }
    
    // Percentage getters for UI
    float healthPercent() const { return maxHealth > 0 ? health / maxHealth : 0.0f; }
    float staminaPercent() const { return maxStamina > 0 ? stamina / maxStamina : 0.0f; }
    float manaPercent() const { return maxMana > 0 ? mana / maxMana : 0.0f; }
};

} // namespace myth
