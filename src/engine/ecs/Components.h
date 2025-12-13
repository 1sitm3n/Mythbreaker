#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace myth {
namespace ecs {

// Transform component
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // Euler angles (degrees)
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};
    
    glm::mat4 getMatrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        return m;
    }
};

// Velocity component for physics
struct Velocity {
    glm::vec3 linear = {0.0f, 0.0f, 0.0f};
    glm::vec3 angular = {0.0f, 0.0f, 0.0f};
};

// Renderable component - references mesh data
struct Renderable {
    uint32_t meshId = 0;        // Which mesh to render
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int32_t vertexOffset = 0;
    bool visible = true;
};

// Player controller component
struct PlayerController {
    float moveSpeed = 10.0f;
    float turnSmoothSpeed = 10.0f;
    float jumpForce = 8.0f;
    float gravity = 20.0f;
    float targetYaw = 0.0f;
    bool isGrounded = true;
};

// Third person camera component
struct ThirdPersonCameraController {
    float yaw = 0.0f;
    float pitch = 25.0f;
    float distance = 8.0f;
    float heightOffset = 2.0f;
    float mouseSensitivity = 0.15f;
    float minPitch = -30.0f;
    float maxPitch = 60.0f;
    float smoothSpeed = 10.0f;
    glm::vec3 currentPosition = {0.0f, 5.0f, 10.0f};
    Entity targetEntity = UINT32_MAX; // Entity to follow
};

// Tag components (empty, just for identification)
struct PlayerTag {};
struct CameraTag {};
struct LandmarkTag {};

// Mesh IDs
enum class MeshId : uint32_t {
    Cube = 0,
    Player = 1,
    COUNT
};

} // namespace ecs
} // namespace myth
