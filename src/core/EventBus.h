#pragma once

#include "Event.h"

#include <functional>
#include <unordered_map>
#include <vector>

class EventBus
{
public:
    using EventCallback = std::function<void(const Event&)>;

    // Subscribe to a specific event type.
    // Returns a subscription ID that can be used to unsubscribe later.
    int subscribe(EventType type, EventCallback callback)
    {
        const int id = m_nextId++;
        m_listeners[type].emplace_back(id, std::move(callback));
        return id;
    }

    // Unsubscribe a previously registered callback.
    void unsubscribe(EventType type, int id)
    {
        auto it = m_listeners.find(type);
        if (it == m_listeners.end())
            return;

        auto& vec = it->second;
        for (auto it2 = vec.begin(); it2 != vec.end(); ++it2)
        {
            if (it2->first == id)
            {
                vec.erase(it2);
                break;
            }
        }
    }

    // Emit an event to all listeners of this event type.
    void emit(const Event& e)
    {
        auto it = m_listeners.find(e.type);
        if (it == m_listeners.end())
            return;

        for (auto& pair : it->second)
        {
            const auto& callback = pair.second;
            callback(e);
        }
    }

private:
    int m_nextId = 1;
    // For each EventType, a vector of (subscriptionId, callback)
    std::unordered_map<EventType, std::vector<std::pair<int, EventCallback>>> m_listeners;
};
