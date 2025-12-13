#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <cassert>

namespace myth {
namespace ecs {

using Entity = uint32_t;
constexpr Entity NULL_ENTITY = UINT32_MAX;

class EntityRegistry {
public:
    Entity create() {
        Entity e;
        if (!m_freeList.empty()) {
            e = m_freeList.front();
            m_freeList.pop();
            m_generations[e]++;
        } else {
            e = static_cast<Entity>(m_generations.size());
            m_generations.push_back(0);
            m_alive.push_back(false);
        }
        m_alive[e] = true;
        m_count++;
        return e;
    }
    
    void destroy(Entity e) {
        if (e < m_alive.size() && m_alive[e]) {
            m_alive[e] = false;
            m_freeList.push(e);
            m_count--;
        }
    }
    
    bool isAlive(Entity e) const {
        return e < m_alive.size() && m_alive[e];
    }
    
    size_t count() const { return m_count; }
    size_t capacity() const { return m_generations.size(); }
    
    template<typename Func>
    void each(Func&& func) {
        for (Entity e = 0; e < m_alive.size(); e++) {
            if (m_alive[e]) func(e);
        }
    }

private:
    std::vector<uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::queue<Entity> m_freeList;
    size_t m_count = 0;
};

template<typename T>
class ComponentArray {
public:
    void add(Entity e, const T& component) {
        if (e >= m_sparse.size()) {
            m_sparse.resize(e + 1, UINT32_MAX);
        }
        if (m_sparse[e] == UINT32_MAX) {
            m_sparse[e] = static_cast<uint32_t>(m_dense.size());
            m_dense.push_back(e);
            m_components.push_back(component);
        } else {
            m_components[m_sparse[e]] = component;
        }
    }
    
    void remove(Entity e) {
        if (!has(e)) return;
        
        uint32_t idx = m_sparse[e];
        Entity lastEntity = m_dense.back();
        
        m_dense[idx] = lastEntity;
        m_components[idx] = m_components.back();
        m_sparse[lastEntity] = idx;
        
        m_dense.pop_back();
        m_components.pop_back();
        m_sparse[e] = UINT32_MAX;
    }
    
    bool has(Entity e) const {
        return e < m_sparse.size() && m_sparse[e] != UINT32_MAX;
    }
    
    T& get(Entity e) {
        assert(has(e));
        return m_components[m_sparse[e]];
    }
    
    const T& get(Entity e) const {
        assert(has(e));
        return m_components[m_sparse[e]];
    }
    
    T* tryGet(Entity e) {
        return has(e) ? &m_components[m_sparse[e]] : nullptr;
    }
    
    const T* tryGet(Entity e) const {
        return has(e) ? &m_components[m_sparse[e]] : nullptr;
    }
    
    size_t size() const { return m_dense.size(); }
    
    template<typename Func>
    void each(Func&& func) {
        for (size_t i = 0; i < m_dense.size(); i++) {
            func(m_dense[i], m_components[i]);
        }
    }
    
    template<typename Func>
    void each(Func&& func) const {
        for (size_t i = 0; i < m_dense.size(); i++) {
            func(m_dense[i], m_components[i]);
        }
    }

private:
    std::vector<uint32_t> m_sparse;
    std::vector<Entity> m_dense;
    std::vector<T> m_components;
};

} // namespace ecs
} // namespace myth
