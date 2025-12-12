#pragma once

#include <chrono>

namespace myth {

class Timer {
public:
    Timer();
    void tick();
    float deltaTime() const { return m_deltaTime; }
    float clampedDeltaTime(float maxDt = 0.1f) const;
    float totalTime() const;
    float fps() const { return m_fps; }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    
    TimePoint m_startTime;
    TimePoint m_lastTime;
    float m_deltaTime = 0.016f;
    float m_fps = 60.0f;
    float m_fpsAccum = 0.0f;
    int m_frameCount = 0;
};

} // namespace myth
