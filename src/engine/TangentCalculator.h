#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

namespace myth {

class TangentCalculator {
public:
    static void calculate(
        const glm::vec3* positions, const glm::vec3* normals,
        const glm::vec2* texcoords, uint32_t numVertices,
        const uint32_t* indices, uint32_t numIndices,
        std::vector<glm::vec4>& outTangents
    ) {
        std::vector<glm::vec3> tan1(numVertices, glm::vec3(0.0f));
        std::vector<glm::vec3> tan2(numVertices, glm::vec3(0.0f));

        for (uint32_t i = 0; i < numIndices; i += 3) {
            uint32_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
            glm::vec3 edge1 = positions[i1] - positions[i0];
            glm::vec3 edge2 = positions[i2] - positions[i0];
            glm::vec2 dUV1 = texcoords[i1] - texcoords[i0];
            glm::vec2 dUV2 = texcoords[i2] - texcoords[i0];
            float denom = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
            if (std::abs(denom) < 1e-8f) continue;
            float r = 1.0f / denom;
            glm::vec3 tangent   = (edge1 * dUV2.y - edge2 * dUV1.y) * r;
            glm::vec3 bitangent = (edge2 * dUV1.x - edge1 * dUV2.x) * r;
            tan1[i0] += tangent; tan1[i1] += tangent; tan1[i2] += tangent;
            tan2[i0] += bitangent; tan2[i1] += bitangent; tan2[i2] += bitangent;
        }

        outTangents.resize(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) {
            const glm::vec3& n = normals[i];
            const glm::vec3& t = tan1[i];
            if (glm::length(t) < 1e-6f) {
                glm::vec3 up = (std::abs(n.y) < 0.99f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
                outTangents[i] = glm::vec4(glm::normalize(glm::cross(up, n)), 1.0f);
                continue;
            }
            glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
            float w = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            outTangents[i] = glm::vec4(tangent, w);
        }
    }

    static void calculateNonIndexed(
        const glm::vec3* positions, const glm::vec3* normals,
        const glm::vec2* texcoords, uint32_t numVertices,
        std::vector<glm::vec4>& outTangents
    ) {
        std::vector<uint32_t> indices(numVertices);
        for (uint32_t i = 0; i < numVertices; i++) indices[i] = i;
        calculate(positions, normals, texcoords, numVertices, indices.data(), numVertices, outTangents);
    }
};

} // namespace myth
