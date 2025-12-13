#pragma once

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace myth {

// Region states representing "reality stability"
enum class RegionState {
    Stable,      // Normal, mundane reality
    Awakening,   // Reality beginning to thin
    Fractured,   // Reality breaking down
    Mythic       // Full mythic manifestation
};

inline const char* regionStateName(RegionState state) {
    switch (state) {
        case RegionState::Stable: return "Stable";
        case RegionState::Awakening: return "Awakening";
        case RegionState::Fractured: return "Fractured";
        case RegionState::Mythic: return "Mythic";
        default: return "Unknown";
    }
}

// Visual properties for each state
struct RegionVisuals {
    glm::vec3 fogColor;
    glm::vec3 skyColor;
    float fogDensity;
    float colorIntensity;
    
    static RegionVisuals forState(RegionState state) {
        switch (state) {
            case RegionState::Stable:
                return {{0.05f, 0.05f, 0.08f}, {0.02f, 0.02f, 0.05f}, 0.02f, 1.0f};
            case RegionState::Awakening:
                return {{0.08f, 0.05f, 0.12f}, {0.04f, 0.02f, 0.08f}, 0.025f, 1.1f};
            case RegionState::Fractured:
                return {{0.12f, 0.04f, 0.15f}, {0.06f, 0.02f, 0.10f}, 0.03f, 1.25f};
            case RegionState::Mythic:
                return {{0.15f, 0.05f, 0.20f}, {0.08f, 0.03f, 0.15f}, 0.04f, 1.5f};
            default:
                return {{0.05f, 0.05f, 0.08f}, {0.02f, 0.02f, 0.05f}, 0.02f, 1.0f};
        }
    }
};

// Region data tracked per chunk/area
struct RegionData {
    RegionState state = RegionState::Stable;
    float realityPressure = 0.0f;      // 0.0 - 1.0, builds with player presence
    float timeSinceVisit = 0.0f;       // Time since player was here
    float stateTimer = 0.0f;           // Time in current state
    
    // Thresholds for state transitions
    static constexpr float AWAKENING_THRESHOLD = 0.3f;
    static constexpr float FRACTURED_THRESHOLD = 0.6f;
    static constexpr float MYTHIC_THRESHOLD = 0.9f;
    
    // Pressure dynamics
    static constexpr float PRESSURE_BUILD_RATE = 0.1f;   // Per second when player present
    static constexpr float PRESSURE_DECAY_RATE = 0.02f;  // Per second when player absent
    static constexpr float DECAY_DELAY = 10.0f;          // Seconds before decay starts
};

// Hash for region coordinates
struct RegionCoord {
    int x, z;
    bool operator==(const RegionCoord& other) const { return x == other.x && z == other.z; }
};

struct RegionCoordHash {
    size_t operator()(const RegionCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 16);
    }
};

// Region state machine managing all regions
class RegionStateMachine {
public:
    float regionSize = 20.0f;  // Each region is 20x20 units
    
    void update(const glm::vec3& playerPos, float dt) {
        // Get player's current region
        RegionCoord playerRegion = getRegionCoord(playerPos);
        
        // Update all tracked regions
        for (auto& [coord, data] : m_regions) {
            bool playerPresent = (coord == playerRegion);
            
            if (playerPresent) {
                // Build pressure when player is present
                data.realityPressure += RegionData::PRESSURE_BUILD_RATE * dt;
                data.realityPressure = glm::min(data.realityPressure, 1.0f);
                data.timeSinceVisit = 0.0f;
            } else {
                // Decay pressure when player is absent (after delay)
                data.timeSinceVisit += dt;
                if (data.timeSinceVisit > RegionData::DECAY_DELAY) {
                    data.realityPressure -= RegionData::PRESSURE_DECAY_RATE * dt;
                    data.realityPressure = glm::max(data.realityPressure, 0.0f);
                }
            }
            
            // Update state based on pressure
            updateRegionState(data, dt);
        }
        
        // Ensure player's region exists
        if (m_regions.find(playerRegion) == m_regions.end()) {
            m_regions[playerRegion] = RegionData{};
        }
        
        // Track current region for external access
        m_currentRegion = playerRegion;
    }
    
    RegionCoord getRegionCoord(const glm::vec3& pos) const {
        return {
            static_cast<int>(floor(pos.x / regionSize)),
            static_cast<int>(floor(pos.z / regionSize))
        };
    }
    
    const RegionData& getCurrentRegionData() const {
        auto it = m_regions.find(m_currentRegion);
        if (it != m_regions.end()) return it->second;
        return m_defaultRegion;
    }
    
    RegionData& getOrCreateRegion(const RegionCoord& coord) {
        return m_regions[coord];
    }
    
    const RegionData* getRegion(const RegionCoord& coord) const {
        auto it = m_regions.find(coord);
        return it != m_regions.end() ? &it->second : nullptr;
    }
    
    RegionVisuals getCurrentVisuals() const {
        const auto& data = getCurrentRegionData();
        
        // Interpolate between current and next state for smooth transitions
        RegionVisuals current = RegionVisuals::forState(data.state);
        RegionState nextState = getNextState(data.state);
        RegionVisuals next = RegionVisuals::forState(nextState);
        
        // Calculate blend factor based on pressure within current state range
        float blend = getStateProgress(data);
        
        RegionVisuals result;
        result.fogColor = glm::mix(current.fogColor, next.fogColor, blend * 0.5f);
        result.skyColor = glm::mix(current.skyColor, next.skyColor, blend * 0.5f);
        result.fogDensity = glm::mix(current.fogDensity, next.fogDensity, blend * 0.5f);
        result.colorIntensity = glm::mix(current.colorIntensity, next.colorIntensity, blend * 0.5f);
        
        return result;
    }
    
    size_t trackedRegionCount() const { return m_regions.size(); }
    
    RegionCoord currentRegion() const { return m_currentRegion; }

private:
    void updateRegionState(RegionData& data, float dt) {
        RegionState newState = data.state;
        
        // Check for state transitions based on pressure
        if (data.realityPressure >= RegionData::MYTHIC_THRESHOLD) {
            newState = RegionState::Mythic;
        } else if (data.realityPressure >= RegionData::FRACTURED_THRESHOLD) {
            newState = RegionState::Fractured;
        } else if (data.realityPressure >= RegionData::AWAKENING_THRESHOLD) {
            newState = RegionState::Awakening;
        } else {
            newState = RegionState::Stable;
        }
        
        if (newState != data.state) {
            data.state = newState;
            data.stateTimer = 0.0f;
        } else {
            data.stateTimer += dt;
        }
    }
    
    RegionState getNextState(RegionState current) const {
        switch (current) {
            case RegionState::Stable: return RegionState::Awakening;
            case RegionState::Awakening: return RegionState::Fractured;
            case RegionState::Fractured: return RegionState::Mythic;
            case RegionState::Mythic: return RegionState::Mythic;
            default: return RegionState::Stable;
        }
    }
    
    float getStateProgress(const RegionData& data) const {
        float low = 0.0f, high = RegionData::AWAKENING_THRESHOLD;
        switch (data.state) {
            case RegionState::Stable:
                low = 0.0f; high = RegionData::AWAKENING_THRESHOLD; break;
            case RegionState::Awakening:
                low = RegionData::AWAKENING_THRESHOLD; high = RegionData::FRACTURED_THRESHOLD; break;
            case RegionState::Fractured:
                low = RegionData::FRACTURED_THRESHOLD; high = RegionData::MYTHIC_THRESHOLD; break;
            case RegionState::Mythic:
                return 1.0f;
        }
        return glm::clamp((data.realityPressure - low) / (high - low), 0.0f, 1.0f);
    }
    
    std::unordered_map<RegionCoord, RegionData, RegionCoordHash> m_regions;
    RegionCoord m_currentRegion{0, 0};
    RegionData m_defaultRegion;
};

} // namespace myth
