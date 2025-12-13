#pragma once

#include "Entity.h"
#include "Components.h"

namespace myth {
namespace ecs {

// World holds all ECS data
struct World {
    EntityRegistry entities;
    
    // Component arrays
    ComponentArray<Transform> transforms;
    ComponentArray<Velocity> velocities;
    ComponentArray<Renderable> renderables;
    ComponentArray<PlayerController> playerControllers;
    ComponentArray<ThirdPersonCameraController> cameraControllers;
    ComponentArray<PlayerTag> playerTags;
    ComponentArray<CameraTag> cameraTags;
    ComponentArray<LandmarkTag> landmarkTags;
    
    // Quick access to special entities
    Entity playerEntity = NULL_ENTITY;
    Entity cameraEntity = NULL_ENTITY;
    
    // Create entity with transform
    Entity createEntity(const glm::vec3& pos = {0,0,0}, const glm::vec3& rot = {0,0,0}, const glm::vec3& scl = {1,1,1}) {
        Entity e = entities.create();
        Transform t;
        t.position = pos;
        t.rotation = rot;
        t.scale = scl;
        transforms.add(e, t);
        return e;
    }
    
    // Create player entity
    Entity createPlayer(const glm::vec3& pos) {
        Entity e = createEntity(pos);
        velocities.add(e, Velocity{});
        playerControllers.add(e, PlayerController{});
        playerTags.add(e, PlayerTag{});
        
        Renderable r;
        r.meshId = static_cast<uint32_t>(MeshId::Player);
        renderables.add(e, r);
        
        playerEntity = e;
        return e;
    }
    
    // Create camera entity
    Entity createCamera(Entity target) {
        Entity e = entities.create();
        ThirdPersonCameraController cam;
        cam.targetEntity = target;
        cameraControllers.add(e, cam);
        cameraTags.add(e, CameraTag{});
        cameraEntity = e;
        return e;
    }
    
    // Create landmark entity
    Entity createLandmark(const glm::vec3& pos, const glm::vec3& scale, float rotY) {
        Entity e = createEntity(pos, {0, rotY, 0}, scale);
        landmarkTags.add(e, LandmarkTag{});
        
        Renderable r;
        r.meshId = static_cast<uint32_t>(MeshId::Cube);
        renderables.add(e, r);
        
        return e;
    }
    
    // Destroy entity and all its components
    void destroyEntity(Entity e) {
        transforms.remove(e);
        velocities.remove(e);
        renderables.remove(e);
        playerControllers.remove(e);
        cameraControllers.remove(e);
        playerTags.remove(e);
        cameraTags.remove(e);
        landmarkTags.remove(e);
        entities.destroy(e);
        
        if (e == playerEntity) playerEntity = NULL_ENTITY;
        if (e == cameraEntity) cameraEntity = NULL_ENTITY;
    }
};

} // namespace ecs
} // namespace myth
