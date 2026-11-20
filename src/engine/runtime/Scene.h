#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace myth::engine {

using EntityId = std::uint64_t;
inline constexpr EntityId InvalidEntity = 0;

struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // Euler angles in degrees, XYZ local rotation.
    glm::vec3 scale{1.0f};
    glm::mat4 matrix() const;
};

struct Material {
    glm::vec4 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.6f;
    std::string albedoTexture;
};

struct MeshComponent {
    std::string source = "builtin:cube";
    Material material;
    bool visible = true;
    bool castsShadow = true;
};

struct CameraComponent {
    float fov = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    bool primary = true;
};

enum class LightType { Directional, Point };

struct LightComponent {
    LightType type = LightType::Directional;
    glm::vec3 color{1.0f};
    float intensity = 3.0f;
    float range = 10.0f;
};

// Deliberately small deterministic physics model: world-aligned boxes, no angular
// dynamics. halfExtents are in local units and include the entity's world scale.
struct RigidBodyComponent {
    bool dynamic = true;
    bool useGravity = true;
    float mass = 1.0f;
    float restitution = 0.1f;
    glm::vec3 velocity{0.0f}; // World-space velocity.
    glm::vec3 halfExtents{0.5f};
    bool grounded = false; // Transient contact state; deliberately not serialized.
};

struct ScriptComponent {
    std::string type;
    nlohmann::json properties = nlohmann::json::object();
};

struct AudioSourceComponent {
    std::string source;
    float volume = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = true;
};

struct AnimationComponent {
    int clipIndex = 0;
    float speed = 1.0f; // Non-negative playback rate, up to 10x.
    bool loop = true;
    bool playing = true;
    float time = 0.0f; // Authoring start offset; advanced only in the play scene.
};

struct SceneEntity {
    EntityId id = InvalidEntity;
    EntityId parent = InvalidEntity;
    std::string name = "Entity";
    bool enabled = true;
    Transform transform;
    std::optional<MeshComponent> mesh;
    std::optional<CameraComponent> camera;
    std::optional<LightComponent> light;
    std::optional<RigidBodyComponent> rigidBody;
    std::optional<ScriptComponent> script;
    std::optional<AudioSourceComponent> audio;
    std::optional<AnimationComponent> animation;
};

struct Environment {
    glm::vec3 skyColor{0.07f, 0.09f, 0.13f};
    glm::vec3 ambientColor{1.0f};
    float ambientIntensity = 0.25f;
    float exposure = 1.0f;
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
};

class Scene {
public:
    std::string name = "Untitled";
    Environment environment;

    // Mutable access is for component editing. Create/delete/reparent through the
    // scene API; IDs and vector topology are owned by Scene.
    std::vector<SceneEntity>& entities() { m_indexDirty = true; return m_entities; }
    const std::vector<SceneEntity>& entities() const { return m_entities; }
    SceneEntity& createEntity(const std::string& entityName = "Entity", EntityId parent = InvalidEntity);
    bool destroyEntity(EntityId id); // Includes all descendants.
    SceneEntity* find(EntityId id);
    const SceneEntity* find(EntityId id) const;
    bool isEnabled(EntityId id) const; // Includes ancestor state.
    glm::mat4 worldMatrix(EntityId id) const;
    bool reparent(EntityId id, EntityId parent, bool keepWorld = true);
    EntityId duplicateEntity(EntityId id); // Copies the entire subtree, with fresh IDs.
    Scene clone() const { return *this; }
    static Scene createDefault();

    // Throws std::runtime_error on invalid data. Deserialization is transactional:
    // it returns a new scene and never mutates the caller's existing scene.
    void validate() const;
    nlohmann::json toJson() const;
    static Scene fromJson(const nlohmann::json& json);
    void save(const std::filesystem::path& path) const;
    static Scene load(const std::filesystem::path& path);

private:
    void rebuildIndex() const;
    std::vector<SceneEntity> m_entities;
    EntityId m_nextId = 1;
    mutable std::unordered_map<EntityId, std::size_t> m_index;
    mutable bool m_indexDirty = true;
};

// Portable asset references are project-relative paths, or supported builtin: URIs.
bool isSafeRelativePath(const std::filesystem::path& path);
bool isValidAssetReference(const std::string& reference, bool allowEmpty = false);

// Shared by project manifests and scenes. Writes a temporary sibling, flushes it,
// and uses an atomic replacement operation where supported by the host filesystem.
void writeJsonAtomically(const std::filesystem::path& path, const nlohmann::json& json);
nlohmann::json readJsonFile(const std::filesystem::path& path);

} // namespace myth::engine
