#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// All event kinds in the game.
// Will be extended over time as I need more cases.
enum class EventType : uint32_t
{
    None = 0,

    // Player-related
    PlayerEnteredRegion,
    PlayerInteractedWithEntity,
    PlayerDied,
    PlayerLearnedKnowledge,

    // World / region / factions
    RegionStateChanged,
    FactionStateChanged,
    GodEngineStateChanged,

    // Metaphysical anomalies
    FinalityLeakDetected,
    ResurrectionFailure,

    // Combat
    CombatStarted,
    CombatEnded,
    ActorKilled,
};

// Generic event payload.
// Keeping it simple and flexible: a type, source/target IDs, and string key/value data.
// Later I can add typed payloads if I want.
struct Event
{
    EventType type = EventType::None;
    std::uint64_t timestamp = 0;   // game time or frame counter
    std::uint32_t sourceId = 0;    // who emitted the event (entity/system ID)
    std::uint32_t targetId = 0;    // optional target

    // Simple string map payload.
    // Use .data["regionId"] = "1", etc.
    std::unordered_map<std::string, std::string> data;
};
