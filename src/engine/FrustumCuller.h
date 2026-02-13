#pragma once

// Prevent Windows min/max macros from interfering
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <glm/glm.hpp>
#include <array>
#include <algorithm>

namespace myth {

struct AABB {
    glm::vec3 lo = glm::vec3(0.0f);
    glm::vec3 hi = glm::vec3(0.0f);

    AABB() = default;
    AABB(const glm::vec3& mn, const glm::vec3& mx) : lo(mn), hi(mx) {}

    static AABB fromCenterExtents(const glm::vec3& center, const glm::vec3& extents) {
        return AABB(center - extents, center + extents);
    }

    static AABB fromChunk(int cx, int cz, float chunkSize, float minY, float maxY) {
        float x = cx * chunkSize - chunkSize / 2.0f;
        float z = cz * chunkSize - chunkSize / 2.0f;
        return AABB(glm::vec3(x, minY, z), glm::vec3(x + chunkSize, maxY, z + chunkSize));
    }

    AABB transformed(const glm::mat4& m) const {
        glm::vec3 center = (lo + hi) * 0.5f;
        glm::vec3 extents = (hi - lo) * 0.5f;
        glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));
        glm::vec3 newExtents(0.0f);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                newExtents[i] += glm::abs(m[j][i]) * extents[j];
        return AABB(newCenter - newExtents, newCenter + newExtents);
    }

    glm::vec3 center() const { return (lo + hi) * 0.5f; }
    glm::vec3 extents() const { return (hi - lo) * 0.5f; }
    float radius() const { return glm::length(extents()); }
};

struct Plane {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float distance = 0.0f;
    float distanceTo(const glm::vec3& point) const { return glm::dot(normal, point) + distance; }
};

class Frustum {
public:
    enum Side { Left = 0, Right, Top, Bottom, Near, Far, COUNT };

    void update(const glm::mat4& vp) {
        m_planes[Left]  = {glm::vec3(vp[0][3]+vp[0][0], vp[1][3]+vp[1][0], vp[2][3]+vp[2][0]), vp[3][3]+vp[3][0]};
        m_planes[Right] = {glm::vec3(vp[0][3]-vp[0][0], vp[1][3]-vp[1][0], vp[2][3]-vp[2][0]), vp[3][3]-vp[3][0]};
        m_planes[Bottom]= {glm::vec3(vp[0][3]+vp[0][1], vp[1][3]+vp[1][1], vp[2][3]+vp[2][1]), vp[3][3]+vp[3][1]};
        m_planes[Top]   = {glm::vec3(vp[0][3]-vp[0][1], vp[1][3]-vp[1][1], vp[2][3]-vp[2][1]), vp[3][3]-vp[3][1]};
        m_planes[Near]  = {glm::vec3(vp[0][3]+vp[0][2], vp[1][3]+vp[1][2], vp[2][3]+vp[2][2]), vp[3][3]+vp[3][2]};
        m_planes[Far]   = {glm::vec3(vp[0][3]-vp[0][2], vp[1][3]-vp[1][2], vp[2][3]-vp[2][2]), vp[3][3]-vp[3][2]};

        for (auto& p : m_planes) {
            float len = glm::length(p.normal);
            if (len > 0.0001f) { p.normal /= len; p.distance /= len; }
        }
    }

    bool isVisible(const AABB& aabb) const {
        for (const auto& plane : m_planes) {
            glm::vec3 pVertex;
            pVertex.x = (plane.normal.x >= 0.0f) ? aabb.hi.x : aabb.lo.x;
            pVertex.y = (plane.normal.y >= 0.0f) ? aabb.hi.y : aabb.lo.y;
            pVertex.z = (plane.normal.z >= 0.0f) ? aabb.hi.z : aabb.lo.z;
            if (plane.distanceTo(pVertex) < 0.0f) return false;
        }
        return true;
    }

    bool isVisible(const glm::vec3& center, float radius) const {
        for (const auto& plane : m_planes)
            if (plane.distanceTo(center) < -radius) return false;
        return true;
    }

private:
    std::array<Plane, COUNT> m_planes;
};

class FrustumCuller {
public:
    void update(const glm::mat4& viewProjection) {
        m_frustum.update(viewProjection);
        m_totalTested = 0;
        m_totalCulled = 0;
    }

    bool testAABB(const AABB& aabb) {
        m_totalTested++;
        bool visible = m_frustum.isVisible(aabb);
        if (!visible) m_totalCulled++;
        return visible;
    }

    bool testSphere(const glm::vec3& center, float radius) {
        m_totalTested++;
        bool visible = m_frustum.isVisible(center, radius);
        if (!visible) m_totalCulled++;
        return visible;
    }

    bool testChunk(int cx, int cz, float chunkSize,
                   float minTerrainY = -20.0f, float maxTerrainY = 80.0f) {
        return testAABB(AABB::fromChunk(cx, cz, chunkSize, minTerrainY, maxTerrainY));
    }

    bool testEntity(const glm::vec3& position, const glm::vec3& scale, float baseBoundRadius = 1.0f) {
        float maxScale = scale.x; if (scale.y > maxScale) maxScale = scale.y; if (scale.z > maxScale) maxScale = scale.z;
        return testSphere(position, baseBoundRadius * maxScale);
    }

    uint32_t totalTested() const { return m_totalTested; }
    uint32_t totalCulled() const { return m_totalCulled; }
    uint32_t totalVisible() const { return m_totalTested - m_totalCulled; }
    float cullPercentage() const {
        return m_totalTested > 0 ? (float)m_totalCulled / m_totalTested * 100.0f : 0.0f;
    }

private:
    Frustum  m_frustum;
    uint32_t m_totalTested = 0;
    uint32_t m_totalCulled = 0;
};

} // namespace myth
