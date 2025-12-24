#pragma once
#include <glm/glm.hpp>
#include "ParticleSystem.h"
#include "TimeSystem.h"
#include "Logger.h"
#include <random>
#include <ctime>
#include <algorithm>

enum class WeatherType {
    Clear,
    Cloudy,
    Foggy,
    Rainy,
    Stormy
};

class WeatherSystem {
public:
    static WeatherSystem& instance() {
        static WeatherSystem inst;
        return inst;
    }
    
    void init() {
        m_rng.seed(static_cast<unsigned>(time(nullptr)));
        myth::Logger::info("Weather system initialized");
    }
    
    void update(float dt, const glm::vec3& playerPos, int regionState) {
        // Update wind
        m_windTime += dt;
        m_wind.x = sin(m_windTime * 0.5f) * m_windStrength;
        m_wind.z = cos(m_windTime * 0.3f) * m_windStrength * 0.5f;
        
        // Set weather based on region state
        updateWeatherForRegion(regionState);
        
        // Smooth fog transition
        m_currentFogDensity = glm::mix(m_currentFogDensity, m_targetFogDensity, dt * 2.0f);
        
        // Spawn rain if needed
        if (m_weather == WeatherType::Rainy || m_weather == WeatherType::Stormy) {
            m_rainTimer -= dt;
            if (m_rainTimer <= 0.0f) {
                spawnRaindrops(playerPos);
                m_rainTimer = 0.02f; // 50 drops per second
            }
        }
        
        // Lightning in storms
        if (m_weather == WeatherType::Stormy) {
            m_lightningTimer -= dt;
            if (m_lightningTimer <= 0.0f) {
                triggerLightning();
                std::uniform_real_distribution<float> dist(3.0f, 10.0f);
                m_lightningTimer = dist(m_rng);
            }
        }
        
        // Fade lightning flash
        m_lightningFlash = (std::max)(0.0f, m_lightningFlash - dt * 5.0f);
    }
    
    void updateWeatherForRegion(int regionState) {
        // Region states: 0=Stable, 1=Awakening, 2=Fractured, 3=Mythic
        switch (regionState) {
            case 0: // Stable - clear or cloudy
                setWeather(WeatherType::Clear);
                m_targetFogDensity = 0.01f;
                m_windStrength = 1.0f;
                break;
            case 1: // Awakening - rainy
                setWeather(WeatherType::Rainy);
                m_targetFogDensity = 0.03f;
                m_windStrength = 2.0f;
                break;
            case 2: // Fractured - stormy
                setWeather(WeatherType::Stormy);
                m_targetFogDensity = 0.02f;
                m_windStrength = 5.0f;
                break;
            case 3: // Mythic - heavy fog, mysterious
                setWeather(WeatherType::Foggy);
                m_targetFogDensity = 0.05f;
                m_windStrength = 0.5f;
                break;
            default:
                setWeather(WeatherType::Clear);
                m_targetFogDensity = 0.01f;
                break;
        }
    }
    
    void setWeather(WeatherType type) {
        if (m_weather != type) {
            m_weather = type;
        }
    }
    
    WeatherType getWeather() const { return m_weather; }
    float getFogDensity() const { return m_currentFogDensity; }
    glm::vec3 getWind() const { return m_wind; }
    float getLightningFlash() const { return m_lightningFlash; }
    
    // Get ambient light modifier based on weather
    float getAmbientModifier() const {
        float base = 1.0f;
        switch (m_weather) {
            case WeatherType::Cloudy: base = 0.7f; break;
            case WeatherType::Foggy: base = 0.6f; break;
            case WeatherType::Rainy: base = 0.5f; break;
            case WeatherType::Stormy: base = 0.4f; break;
            default: break;
        }
        return base + m_lightningFlash * 2.0f; // Lightning brightens everything
    }
    
    // Get sky tint based on weather
    glm::vec3 getSkyTint() const {
        switch (m_weather) {
            case WeatherType::Cloudy: return glm::vec3(0.7f, 0.7f, 0.75f);
            case WeatherType::Foggy: return glm::vec3(0.8f, 0.8f, 0.85f);
            case WeatherType::Rainy: return glm::vec3(0.5f, 0.5f, 0.6f);
            case WeatherType::Stormy: return glm::vec3(0.3f, 0.3f, 0.4f);
            default: return glm::vec3(1.0f, 1.0f, 1.0f);
        }
    }
    
    glm::vec3 getFogColor() const {
        return TimeSystem::instance().getFogColor() * getSkyTint();
    }
    
private:
    WeatherSystem() = default;
    
    void spawnRaindrops(const glm::vec3& playerPos) {
        std::uniform_real_distribution<float> distX(-20.0f, 20.0f);
        std::uniform_real_distribution<float> distZ(-20.0f, 20.0f);
        
        int dropCount = (m_weather == WeatherType::Stormy) ? 5 : 3;
        
        for (int i = 0; i < dropCount; ++i) {
            glm::vec3 pos = playerPos + glm::vec3(distX(m_rng), 15.0f, distZ(m_rng));
            // Spawn falling rain
            ParticleSystem::instance().spawnRain(pos, 1);
        }
    }
    
    void triggerLightning() {
        m_lightningFlash = 1.0f;
        // Could add thunder sound here
    }
    
    WeatherType m_weather = WeatherType::Clear;
    float m_currentFogDensity = 0.01f;
    float m_targetFogDensity = 0.01f;
    glm::vec3 m_wind = glm::vec3(0.0f, 0.0f, 0.0f);
    float m_windStrength = 1.0f;
    float m_windTime = 0.0f;
    float m_rainTimer = 0.0f;
    float m_lightningTimer = 5.0f;
    float m_lightningFlash = 0.0f;
    
    std::mt19937 m_rng;
};


