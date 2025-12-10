#pragma once

#include "EventBus.h"

#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include <iostream>

// Basic region ID type.
// For now we just treat it as a uint32_t.
// Later we can define an enum or central registry.
using RegionId = std::uint32_t;

// High-level states a region can be in.
// refine/extend this later as needed.
enum class RegionState : std::uint8_t
{
    Normal = 0,          // default, stable mode
    LeakingFinality,     // resurrection starting to fail, true death leaking back in
    ContainmentFailure,  // god-engine can no longer hide the problem
    CollapsePending,     // region is on the edge of metaphysical collapse
    PostContinuity       // after the old lie is gone new reality rules apply
};

// One possible transition from `from` to `to` when `condition(event)` is true.
struct RegionStateTransition
{
    RegionState from;
    RegionState to;

    // Given an incoming event, decide if we should transition.
    std::function<bool(const Event&)> condition;

    // Callback invoked once when the transition happens.
    std::function<void(const Event&)> onTransition;
};

class RegionStateMachine
{
public:
    RegionStateMachine(RegionId regionId, EventBus& bus)
        : m_regionId(regionId)
        , m_eventBus(bus)
    {
    }

    RegionId getRegionId() const { return m_regionId; }

    RegionState getCurrentState() const { return m_currentState; }

    void addTransition(const RegionStateTransition& t)
    {
        m_transitions.push_back(t);
    }

    // Called by whoever is forwarding events (usually GameWorld / logic layer).
    void handleEvent(const Event& e)
    {
        for (auto& t : m_transitions)
        {
            if (t.from == m_currentState && t.condition && t.condition(e))
            {
                RegionState oldState = m_currentState;
                m_currentState = t.to;

                if (t.onTransition)
                {
                    t.onTransition(e);
                }

                emitStateChangedEvent(oldState, m_currentState);
                break; // Only one transition per event
            }
        }
    }

private:
    void emitStateChangedEvent(RegionState from, RegionState to)
    {
        Event e;
        e.type = EventType::RegionStateChanged;
        e.sourceId = m_regionId;

        e.data["from"] = std::to_string(static_cast<int>(from));
        e.data["to"]   = std::to_string(static_cast<int>(to));

        // This is a good place to log for debugging.
        std::cout << "[RegionStateMachine] Region " << m_regionId
                  << " transitioned from " << static_cast<int>(from)
                  << " to " << static_cast<int>(to) << "\n";

        m_eventBus.emit(e);
    }

private:
    RegionId m_regionId;
    RegionState m_currentState = RegionState::Normal;
    std::vector<RegionStateTransition> m_transitions;
    EventBus& m_eventBus;
};
