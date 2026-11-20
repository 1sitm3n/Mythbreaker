#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <algorithm>
#include <cmath>

namespace myth::editor {

// Editor-only navigation. Scene cameras remain authored components and are never
// changed by navigating the editor viewport.
class EditorCamera {
public:
    glm::vec3 position{8.0f, 6.0f, 10.0f};
    float yaw = -128.66f;
    float pitch = -21.33f;
    float speed = 6.0f;
    float fov = 60.0f;
    float nearClip = 0.05f;
    float farClip = 2000.0f;

    glm::vec3 forward() const {
        const float y = glm::radians(yaw), p = glm::radians(pitch);
        return glm::normalize(glm::vec3(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)));
    }
    glm::vec3 right() const { return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0))); }
    glm::vec3 up() const { return glm::normalize(glm::cross(right(), forward())); }
    glm::mat4 view() const { return glm::lookAtRH(position, position + forward(), glm::vec3(0, 1, 0)); }
    glm::mat4 projection(float aspect) const {
        auto matrix = glm::perspectiveRH_ZO(glm::radians(fov), std::max(0.01f, aspect), nearClip, farClip);
        matrix[1][1] *= -1.0f;
        return matrix;
    }
    void look(float dx, float dy) {
        yaw += dx * 0.18f;
        pitch = std::clamp(pitch - dy * 0.18f, -89.0f, 89.0f);
    }
    void move(float forwardInput, float rightInput, float upInput, float dt, bool fast) {
        glm::vec3 direction = forward() * forwardInput + right() * rightInput + glm::vec3(0, 1, 0) * upInput;
        if (glm::length(direction) > 0.001f) position += glm::normalize(direction) * speed * (fast ? 3.0f : 1.0f) * std::min(dt, 0.1f);
    }
    void focus(const glm::vec3& target, float radius = 1.0f) {
        position = target - forward() * std::max(2.0f, radius * 3.0f);
    }
    void orbit(const glm::vec3& target, float dx, float dy) {
        const float distance = std::max(0.2f, glm::length(position - target));
        look(dx, dy);
        position = target - forward() * distance;
    }
};

} // namespace myth::editor
