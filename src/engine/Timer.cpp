#include "Timer.h"

namespace myth {

Timer::Timer() : m_startTime(Clock::now()), m_lastTime(m_startTime) {}

void Timer::tick() {
    auto now = Clock::now();
    m_deltaTime = std::chrono::duration<float>(now - m_lastTime).count();
    m_lastTime = now;
    
    m_fpsAccum += m_deltaTime;
    m_frameCount++;
    
    if (m_fpsAccum >= 1.0f) {
        m_fps = static_cast<float>(m_frameCount) / m_fpsAccum;
        m_frameCount = 0;
        m_fpsAccum = 0.0f;
    }
}

float Timer::clampedDeltaTime(float maxDt) const {
    return m_deltaTime > maxDt ? maxDt : m_deltaTime;
}

float Timer::totalTime() const {
    return std::chrono::duration<float>(Clock::now() - m_startTime).count();
}

} // namespace myth
