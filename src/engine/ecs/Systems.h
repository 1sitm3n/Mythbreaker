#pragma once

#include "World.h"
#include "engine/Input.h"
#include <GLFW/glfw3.h>

namespace myth {
namespace ecs {

// Player input system
inline void updatePlayerInput(World& world, float dt, bool mouseCaptured, 
                               float mouseDeltaX, float mouseDeltaY,
                               ThirdPersonCameraController* cam) {
    if (world.playerEntity == NULL_ENTITY) return;
    
    auto* transform = world.transforms.tryGet(world.playerEntity);
    auto* velocity = world.velocities.tryGet(world.playerEntity);
    auto* controller = world.playerControllers.tryGet(world.playerEntity);
    if (!transform || !velocity || !controller) return;
    
    auto& input = Input::instance();
    
    // Get camera-relative directions
    glm::vec3 camForward(0, 0, 1);
    glm::vec3 camRight(1, 0, 0);
    if (cam) {
        camForward = glm::normalize(glm::vec3(sin(glm::radians(cam->yaw)), 0.0f, cos(glm::radians(cam->yaw))));
        camRight = glm::normalize(glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    
    // Movement
    glm::vec3 moveDir(0.0f);
    if (input.isKeyDown(GLFW_KEY_W)) moveDir += camForward;
    if (input.isKeyDown(GLFW_KEY_S)) moveDir -= camForward;
    if (input.isKeyDown(GLFW_KEY_A)) moveDir -= camRight;
    if (input.isKeyDown(GLFW_KEY_D)) moveDir += camRight;
    
    float speed = controller->moveSpeed;
    if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT)) speed *= 2.0f;
    
    if (glm::length(moveDir) > 0.01f) {
        moveDir = glm::normalize(moveDir);
        velocity->linear.x = moveDir.x * speed;
        velocity->linear.z = moveDir.z * speed;
        controller->targetYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
    } else {
        velocity->linear.x *= 0.85f;
        velocity->linear.z *= 0.85f;
    }
    
    // Jump
    if (input.isKeyPressed(GLFW_KEY_SPACE) && controller->isGrounded) {
        velocity->linear.y = controller->jumpForce;
        controller->isGrounded = false;
    }
}

// Physics/movement system
inline void updateMovement(World& world, float dt) {
    world.playerControllers.each([&](Entity e, PlayerController& controller) {
        auto* transform = world.transforms.tryGet(e);
        auto* velocity = world.velocities.tryGet(e);
        if (!transform || !velocity) return;
        
        // Smooth rotation
        float yawDiff = controller.targetYaw - transform->rotation.y;
        if (yawDiff > 180.0f) yawDiff -= 360.0f;
        if (yawDiff < -180.0f) yawDiff += 360.0f;
        transform->rotation.y += yawDiff * controller.turnSmoothSpeed * dt;
        if (transform->rotation.y < 0.0f) transform->rotation.y += 360.0f;
        if (transform->rotation.y > 360.0f) transform->rotation.y -= 360.0f;
        
        // Gravity
        if (!controller.isGrounded) {
            velocity->linear.y -= controller.gravity * dt;
        }
        
        // Apply velocity
        transform->position += velocity->linear * dt;
        
        // Ground collision
        if (transform->position.y <= 0.0f) {
            transform->position.y = 0.0f;
            velocity->linear.y = 0.0f;
            controller.isGrounded = true;
        }
    });
}

// Camera system
inline void updateCamera(World& world, float dt, bool mouseCaptured, 
                         double mouseDeltaX, double mouseDeltaY, float scrollDelta) {
    world.cameraControllers.each([&](Entity e, ThirdPersonCameraController& cam) {
        // Mouse input
        if (mouseCaptured) {
            cam.yaw -= static_cast<float>(mouseDeltaX) * cam.mouseSensitivity;
            cam.pitch += static_cast<float>(mouseDeltaY) * cam.mouseSensitivity;
            cam.pitch = glm::clamp(cam.pitch, cam.minPitch, cam.maxPitch);
            if (cam.yaw < 0.0f) cam.yaw += 360.0f;
            if (cam.yaw > 360.0f) cam.yaw -= 360.0f;
        }
        
        // Scroll zoom
        cam.distance = glm::clamp(cam.distance - scrollDelta * 0.5f, 3.0f, 20.0f);
        
        // Follow target
        if (cam.targetEntity != NULL_ENTITY && world.transforms.has(cam.targetEntity)) {
            const auto& targetTransform = world.transforms.get(cam.targetEntity);
            
            float horizontalDist = cam.distance * cos(glm::radians(cam.pitch));
            float verticalDist = cam.distance * sin(glm::radians(cam.pitch));
            
            glm::vec3 targetPos;
            targetPos.x = targetTransform.position.x - horizontalDist * sin(glm::radians(cam.yaw));
            targetPos.z = targetTransform.position.z - horizontalDist * cos(glm::radians(cam.yaw));
            targetPos.y = targetTransform.position.y + cam.heightOffset + verticalDist;
            
            float t = 1.0f - exp(-cam.smoothSpeed * dt);
            cam.currentPosition = glm::mix(cam.currentPosition, targetPos, t);
        }
    });
}

// Get camera view matrix
inline glm::mat4 getCameraViewMatrix(const World& world) {
    if (world.cameraEntity == NULL_ENTITY) return glm::mat4(1.0f);
    
    const auto* cam = world.cameraControllers.tryGet(world.cameraEntity);
    if (!cam || cam->targetEntity == NULL_ENTITY) return glm::mat4(1.0f);
    
    const auto* targetTransform = world.transforms.tryGet(cam->targetEntity);
    if (!targetTransform) return glm::mat4(1.0f);
    
    glm::vec3 lookTarget = targetTransform->position + glm::vec3(0.0f, 1.1f, 0.0f);
    return glm::lookAt(cam->currentPosition, lookTarget, glm::vec3(0.0f, 1.0f, 0.0f));
}

inline glm::vec3 getCameraPosition(const World& world) {
    if (world.cameraEntity == NULL_ENTITY) return glm::vec3(0, 5, 10);
    const auto* cam = world.cameraControllers.tryGet(world.cameraEntity);
    return cam ? cam->currentPosition : glm::vec3(0, 5, 10);
}

} // namespace ecs
} // namespace myth
