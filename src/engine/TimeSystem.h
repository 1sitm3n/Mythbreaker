#pragma once
#include <glm/glm.hpp>

class TimeSystem {
public:
    static TimeSystem& instance() {
        static TimeSystem inst;
        return inst;
    }
    
    void update(float dt) {
        m_gameTime += dt * m_timeScale;
        // Full day cycle = 600 seconds real time (10 minutes) at 1x scale
        while (m_gameTime >= m_dayLength) m_gameTime -= m_dayLength;
    }
    
    // Returns 0.0 = midnight, 0.25 = sunrise (6am), 0.5 = noon, 0.75 = sunset (6pm)
    float getNormalizedTime() const { return m_gameTime / m_dayLength; }
    
    // Returns hour of day (0-24)
    float getHourOfDay() const { return getNormalizedTime() * 24.0f; }
    
    // Sun angle in radians (0 = horizon east, PI/2 = zenith, PI = horizon west)
    float getSunAngle() const {
        float t = getNormalizedTime();
        // Sun rises at 0.25 (6am), sets at 0.75 (6pm)
        if (t < 0.25f || t > 0.75f) return -0.2f; // Below horizon
        float dayProgress = (t - 0.25f) / 0.5f; // 0 to 1 during daytime
        return dayProgress * 3.14159f; // 0 to PI
    }
    
    // Returns sun direction vector (for lighting)
    glm::vec3 getSunDirection() const {
        float angle = getSunAngle();
        // Sun moves east to west, Y is up
        return glm::normalize(glm::vec3(-cos(angle), sin(angle), 0.3f));
    }
    
    // Light intensity (0 at night, 1 at noon)
    float getSunIntensity() const {
        float angle = getSunAngle();
        if (angle < 0) return 0.15f; // Ambient night light
        return 0.15f + 0.85f * sin(angle);
    }
    
    // Sky color at current time
    glm::vec3 getSkyColor() const {
        float hour = getHourOfDay();
        
        // Night (dark blue)
        glm::vec3 night(0.05f, 0.05f, 0.15f);
        // Dawn/Dusk (orange-pink)
        glm::vec3 dawn(0.8f, 0.4f, 0.3f);
        // Day (light blue)
        glm::vec3 day(0.4f, 0.6f, 0.9f);
        // Noon (bright blue)
        glm::vec3 noon(0.5f, 0.7f, 1.0f);
        
        if (hour < 5.0f) return night;
        if (hour < 6.0f) return glm::mix(night, dawn, hour - 5.0f);
        if (hour < 7.0f) return glm::mix(dawn, day, hour - 6.0f);
        if (hour < 12.0f) return glm::mix(day, noon, (hour - 7.0f) / 5.0f);
        if (hour < 17.0f) return glm::mix(noon, day, (hour - 12.0f) / 5.0f);
        if (hour < 18.0f) return glm::mix(day, dawn, hour - 17.0f);
        if (hour < 19.0f) return glm::mix(dawn, night, hour - 18.0f);
        return night;
    }
    
    // Fog color (warmer at sunrise/sunset)
    glm::vec3 getFogColor() const {
        glm::vec3 sky = getSkyColor();
        return glm::mix(sky, glm::vec3(0.7f, 0.7f, 0.8f), 0.3f);
    }
    
    bool isNight() const { 
        float h = getHourOfDay();
        return h < 6.0f || h > 19.0f; 
    }
    
    bool isDawn() const {
        float h = getHourOfDay();
        return h >= 5.0f && h < 7.0f;
    }
    
    bool isDusk() const {
        float h = getHourOfDay();
        return h >= 17.0f && h < 19.0f;
    }
    
    void setTimeScale(float scale) { m_timeScale = scale; }
    float getTimeScale() const { return m_timeScale; }
    
    void setTime(float normalizedTime) { m_gameTime = normalizedTime * m_dayLength; }
    
private:
    TimeSystem() = default;
    
    float m_gameTime = 150.0f;  // Start at 6am (0.25 * 600)
    float m_dayLength = 600.0f; // 10 minutes real time = 1 game day
    float m_timeScale = 1.0f;   // 1x = normal, 10x = fast forward
};
