#include "Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace myth::engine {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaximumEntities = 100000;
constexpr std::size_t MaximumHierarchyDepth = 1024;
constexpr std::uintmax_t MaximumJsonBytes = 64 * 1024 * 1024;

[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("Invalid scene: " + message);
}

bool finite(float value) { return std::isfinite(value); }
bool finite(const glm::vec3& value) { return finite(value.x) && finite(value.y) && finite(value.z); }
bool finite(const glm::vec4& value) { return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w); }

Json vectorJson(const glm::vec3& value) { return Json::array({value.x, value.y, value.z}); }
Json vectorJson(const glm::vec4& value) { return Json::array({value.x, value.y, value.z, value.w}); }

float number(const Json& json, const char* key, float fallback) {
    if (!json.contains(key)) return fallback;
    if (!json.at(key).is_number()) invalid(std::string(key) + " must be a number");
    const double result = json.at(key).get<double>();
    if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
        invalid(std::string(key) + " must be a finite float");
    return static_cast<float>(result);
}

glm::vec3 vec3(const Json& json, const char* key, glm::vec3 fallback) {
    if (!json.contains(key)) return fallback;
    const auto& value = json.at(key);
    if (!value.is_array() || value.size() != 3) invalid(std::string(key) + " must contain three numbers");
    for (std::size_t i = 0; i != 3; ++i) {
        if (!value[i].is_number()) invalid(std::string(key) + " contains a non-number");
        const double item = value[i].get<double>();
        if (!std::isfinite(item) || std::abs(item) > std::numeric_limits<float>::max()) invalid(std::string(key) + " contains an invalid float");
        fallback[static_cast<int>(i)] = static_cast<float>(item);
    }
    return fallback;
}

glm::vec4 vec4(const Json& json, const char* key, glm::vec4 fallback) {
    if (!json.contains(key)) return fallback;
    const auto& value = json.at(key);
    if (!value.is_array() || value.size() != 4) invalid(std::string(key) + " must contain four numbers");
    for (std::size_t i = 0; i != 4; ++i) {
        if (!value[i].is_number()) invalid(std::string(key) + " contains a non-number");
        const double item = value[i].get<double>();
        if (!std::isfinite(item) || std::abs(item) > std::numeric_limits<float>::max()) invalid(std::string(key) + " contains an invalid float");
        fallback[static_cast<int>(i)] = static_cast<float>(item);
    }
    return fallback;
}

bool boolean(const Json& json, const char* key, bool fallback) {
    if (!json.contains(key)) return fallback;
    if (!json.at(key).is_boolean()) invalid(std::string(key) + " must be true or false");
    return json.at(key).get<bool>();
}

std::string string(const Json& json, const char* key, const std::string& fallback) {
    if (!json.contains(key)) return fallback;
    if (!json.at(key).is_string()) invalid(std::string(key) + " must be a string");
    return json.at(key).get<std::string>();
}

EntityId entityId(const Json& json, const char* key, EntityId fallback) {
    if (!json.contains(key)) return fallback;
    const auto& value = json.at(key);
    if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        invalid(std::string(key) + " must be a non-negative integer");
    return value.get<EntityId>();
}

void requireObject(const Json& value, const char* description) {
    if (!value.is_object()) invalid(std::string(description) + " must be an object");
}

void validateJsonValues(const Json& value, unsigned depth = 0) {
    if (depth > 64) invalid("script properties are nested too deeply");
    if (value.is_number_float() && !std::isfinite(value.get<double>())) invalid("script properties contain a non-finite number");
    if (value.is_array() || value.is_object()) {
        for (const auto& item : value) validateJsonValues(item, depth + 1);
    }
}

bool decomposeTransform(const glm::mat4& matrix, Transform& transform) {
    glm::quat orientation;
    glm::vec3 skew;
    glm::vec4 perspective;
    if (!glm::decompose(matrix, transform.scale, orientation, transform.position, skew, perspective)) return false;
    if (glm::length(skew) > 0.0001f) return false;
    transform.rotation = glm::degrees(glm::eulerAngles(glm::normalize(orientation)));
    if (!finite(transform.position) || !finite(transform.rotation) || !finite(transform.scale)) return false;
    const auto recomposed = transform.matrix();
    for (int column = 0; column != 4; ++column)
        for (int row = 0; row != 4; ++row)
            if (std::abs(recomposed[column][row] - matrix[column][row]) > 0.001f * (1.0f + std::abs(matrix[column][row]))) return false;
    return true;
}
} // namespace

glm::mat4 Transform::matrix() const {
    return glm::translate(glm::mat4(1.0f), position) *
           glm::mat4_cast(glm::quat(glm::radians(rotation))) *
           glm::scale(glm::mat4(1.0f), scale);
}

bool isSafeRelativePath(const std::filesystem::path& path) {
    const auto text = path.generic_string();
    if (text.empty() || text.size() > 4096 || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    if (text.find(':') != std::string::npos || text.find('\\') != std::string::npos || text.find('\0') != std::string::npos) return false;
    for (const auto& part : path)
        if (part == ".." || part == "." || part.empty()) return false;
    return true;
}

bool isValidAssetReference(const std::string& reference, bool allowEmpty) {
    if (reference.empty()) return allowEmpty;
    if (reference.starts_with("builtin:"))
        return reference == "builtin:cube" || reference == "builtin:plane" || reference == "builtin:sphere";
    return isSafeRelativePath(std::filesystem::path(reference));
}

void Scene::rebuildIndex() const {
    if (!m_indexDirty) return;
    m_index.clear();
    m_index.reserve(m_entities.size());
    for (std::size_t i = 0; i != m_entities.size(); ++i) m_index.emplace(m_entities[i].id, i);
    m_indexDirty = false;
}

SceneEntity* Scene::find(EntityId id) {
    return const_cast<SceneEntity*>(static_cast<const Scene&>(*this).find(id));
}

const SceneEntity* Scene::find(EntityId id) const {
    if (id == InvalidEntity) return nullptr;
    rebuildIndex();
    auto it = m_index.find(id);
    if (it == m_index.end()) return nullptr;
    if (it->second >= m_entities.size() || m_entities[it->second].id != id) {
        m_indexDirty = true;
        rebuildIndex();
        it = m_index.find(id);
        if (it == m_index.end()) return nullptr;
    }
    return &m_entities[it->second];
}

SceneEntity& Scene::createEntity(const std::string& entityName, EntityId parent) {
    if (parent != InvalidEntity && !find(parent)) throw std::runtime_error("Cannot create entity: parent does not exist");
    if (entityName.empty() || entityName.size() > 256) throw std::runtime_error("Entity name must contain 1 to 256 characters");
    if (m_entities.size() >= MaximumEntities) throw std::runtime_error("Scene entity limit reached");
    if (m_nextId == InvalidEntity || m_nextId == std::numeric_limits<EntityId>::max()) throw std::runtime_error("Entity ID space exhausted");
    SceneEntity entity;
    entity.id = m_nextId++;
    entity.parent = parent;
    entity.name = entityName;
    m_entities.push_back(std::move(entity));
    m_indexDirty = true;
    return m_entities.back();
}

bool Scene::destroyEntity(EntityId id) {
    if (!find(id)) return false;
    std::unordered_set<EntityId> removed{id};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entity : m_entities)
            if (removed.contains(entity.parent) && removed.insert(entity.id).second) changed = true;
    }
    std::erase_if(m_entities, [&removed](const SceneEntity& entity) { return removed.contains(entity.id); });
    m_indexDirty = true;
    return true;
}

bool Scene::isEnabled(EntityId id) const {
    std::unordered_set<EntityId> visited;
    for (const auto* entity = find(id); entity; entity = find(entity->parent)) {
        if (!entity->enabled || !visited.insert(entity->id).second || visited.size() > MaximumHierarchyDepth) return false;
        if (entity->parent == InvalidEntity) return true;
        if (!find(entity->parent)) return false;
    }
    return false;
}

glm::mat4 Scene::worldMatrix(EntityId id) const {
    glm::mat4 world(1.0f);
    std::unordered_set<EntityId> visited;
    const auto* entity = find(id);
    if (!entity) throw std::runtime_error("Cannot get transform for a missing entity");
    while (entity) {
        if (!visited.insert(entity->id).second || visited.size() > MaximumHierarchyDepth)
            throw std::runtime_error("Scene hierarchy contains a cycle or is too deep");
        world = entity->transform.matrix() * world;
        if (entity->parent == InvalidEntity) break;
        entity = find(entity->parent);
        if (!entity) throw std::runtime_error("Scene hierarchy contains a missing parent");
    }
    return world;
}

bool Scene::reparent(EntityId id, EntityId parent, bool keepWorld) {
    auto* entity = find(id);
    if (!entity || id == parent || (parent && !find(parent))) return false;
    std::unordered_set<EntityId> visited;
    for (const auto* ancestor = find(parent); ancestor; ancestor = find(ancestor->parent)) {
        if (ancestor->id == id || !visited.insert(ancestor->id).second || visited.size() >= MaximumHierarchyDepth) return false;
    }
    Transform local = entity->transform;
    if (keepWorld) {
        const glm::mat4 parentWorld = parent ? worldMatrix(parent) : glm::mat4(1.0f);
        if (std::abs(glm::determinant(parentWorld)) < 1e-12f) return false;
        if (!decomposeTransform(glm::inverse(parentWorld) * worldMatrix(id), local)) return false;
    }
    entity->parent = parent;
    entity->transform = local;
    return true;
}

EntityId Scene::duplicateEntity(EntityId id) {
    const auto* original = find(id);
    if (!original) return InvalidEntity;
    // Snapshot before insertion: vector growth must not invalidate source entities.
    const auto originals = m_entities;
    std::unordered_set<EntityId> included{id};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entity : originals)
            if (included.contains(entity.parent) && included.insert(entity.id).second) changed = true;
    }
    if (m_entities.size() + included.size() > MaximumEntities ||
        std::numeric_limits<EntityId>::max() - m_nextId <= included.size())
        throw std::runtime_error("Cannot duplicate subtree: scene capacity exceeded");
    std::unordered_map<EntityId, EntityId> remap;
    for (const auto& entity : originals) if (included.contains(entity.id)) remap.emplace(entity.id, m_nextId++);
    for (const auto& entity : originals) {
        if (!included.contains(entity.id)) continue;
        auto copy = entity;
        copy.id = remap.at(entity.id);
        if (copy.parent && remap.contains(copy.parent)) copy.parent = remap.at(copy.parent);
        if (entity.id == id) copy.name = copy.name.substr(0, 251) + " Copy";
        if (copy.camera) copy.camera->primary = false;
        m_entities.push_back(std::move(copy));
    }
    m_indexDirty = true;
    return remap.at(id);
}

void Scene::validate() const {
    if (name.empty() || name.size() > 256) invalid("name must contain 1 to 256 characters");
    if (m_entities.size() > MaximumEntities) invalid("too many entities");
    if (!finite(environment.skyColor) || !finite(environment.ambientColor) || !finite(environment.gravity) ||
        glm::any(glm::lessThan(environment.skyColor, glm::vec3(0))) || glm::any(glm::lessThan(environment.ambientColor, glm::vec3(0))) ||
        !finite(environment.ambientIntensity) || environment.ambientIntensity < 0 || !finite(environment.exposure) || environment.exposure <= 0)
        invalid("environment contains invalid values");
    std::unordered_map<EntityId, const SceneEntity*> ids;
    for (const auto& entity : m_entities) {
        if (!entity.id || entity.id == std::numeric_limits<EntityId>::max() || !ids.emplace(entity.id, &entity).second)
            invalid("entity IDs must be unique positive integers with room for a successor");
        if (entity.name.empty() || entity.name.size() > 256) invalid("entity name must contain 1 to 256 characters");
        const auto& transform = entity.transform;
        if (!finite(transform.position) || !finite(transform.rotation) || !finite(transform.scale) ||
            glm::any(glm::lessThan(glm::abs(transform.scale), glm::vec3(1e-6f)))) invalid("transform contains non-finite values or zero scale");
        if (entity.mesh) {
            const auto& mesh = *entity.mesh;
            const auto& material = mesh.material;
            if (!isValidAssetReference(mesh.source) || (!material.albedoTexture.empty() && !isSafeRelativePath(material.albedoTexture))) invalid("invalid or escaping asset reference");
            if (!finite(material.baseColor) || glm::any(glm::lessThan(material.baseColor, glm::vec4(0))) || material.baseColor.a > 1 ||
                !finite(material.metallic) || material.metallic < 0 || material.metallic > 1 ||
                !finite(material.roughness) || material.roughness < 0 || material.roughness > 1) invalid("material values are outside their valid ranges");
        }
        if (entity.camera) {
            const auto& camera = *entity.camera;
            if (!finite(camera.fov) || camera.fov < 1 || camera.fov > 175 || !finite(camera.nearClip) || camera.nearClip <= 0 ||
                !finite(camera.farClip) || camera.farClip <= camera.nearClip) invalid("camera projection is invalid");
        }
        if (entity.light) {
            const auto& light = *entity.light;
            if ((light.type != LightType::Directional && light.type != LightType::Point) || !finite(light.color) ||
                glm::any(glm::lessThan(light.color, glm::vec3(0))) || !finite(light.intensity) || light.intensity < 0 ||
                !finite(light.range) || light.range <= 0) invalid("light values are invalid");
        }
        if (entity.rigidBody) {
            const auto& body = *entity.rigidBody;
            if (!finite(body.mass) || body.mass < 0.000001f || !finite(body.restitution) || body.restitution < 0 || body.restitution > 1 ||
                !finite(body.velocity) || !finite(body.halfExtents) || glm::any(glm::lessThanEqual(body.halfExtents, glm::vec3(0))))
                invalid("rigid body values are invalid");
        }
        if (entity.script) {
            if (entity.script->type.empty() || entity.script->type.size() > 128 || !entity.script->properties.is_object()) invalid("script needs a type and object properties");
            validateJsonValues(entity.script->properties);
        }
        if (entity.audio && ((!entity.audio->source.empty() && !isSafeRelativePath(entity.audio->source)) || !finite(entity.audio->volume) ||
            entity.audio->volume < 0.0f || entity.audio->volume > 1.0f)) invalid("audio source or volume is invalid");
        if (entity.animation && (entity.animation->clipIndex < 0 || !finite(entity.animation->speed) || entity.animation->speed < 0.0f ||
            entity.animation->speed > 10.0f || !finite(entity.animation->time) || entity.animation->time < 0.0f)) invalid("animation playback values are invalid");
    }
    if (m_nextId == InvalidEntity || m_nextId == std::numeric_limits<EntityId>::max()) invalid("invalid next entity ID");
    for (const auto& entity : m_entities) if (entity.id >= m_nextId) invalid("next entity ID must exceed all assigned IDs");
    for (const auto& entity : m_entities) {
        std::unordered_set<EntityId> visited;
        const auto* ancestor = &entity;
        while (ancestor) {
            if (!visited.insert(ancestor->id).second || visited.size() > MaximumHierarchyDepth) invalid("cyclic or excessively deep hierarchy");
            if (!ancestor->parent) break;
            const auto it = ids.find(ancestor->parent);
            if (it == ids.end()) invalid("entity refers to a missing parent");
            ancestor = it->second;
        }
    }
}

nlohmann::json Scene::toJson() const {
    validate();
    Json result{{"format", "mythbreaker.scene"}, {"version", 1}, {"name", name}, {"nextId", m_nextId}};
    result["environment"] = {{"skyColor", vectorJson(environment.skyColor)}, {"ambientColor", vectorJson(environment.ambientColor)},
        {"ambientIntensity", environment.ambientIntensity}, {"exposure", environment.exposure}, {"gravity", vectorJson(environment.gravity)}};
    auto& entities = result["entities"] = Json::array();
    for (const auto& entity : m_entities) {
        Json item{{"id", entity.id}, {"parent", entity.parent}, {"name", entity.name}, {"enabled", entity.enabled}};
        item["transform"] = {{"position", vectorJson(entity.transform.position)}, {"rotation", vectorJson(entity.transform.rotation)}, {"scale", vectorJson(entity.transform.scale)}};
        if (entity.mesh) {
            const auto& mesh = *entity.mesh;
            item["mesh"] = {{"source", mesh.source}, {"visible", mesh.visible}, {"castsShadow", mesh.castsShadow},
                {"material", {{"baseColor", vectorJson(mesh.material.baseColor)}, {"metallic", mesh.material.metallic},
                    {"roughness", mesh.material.roughness}, {"albedoTexture", mesh.material.albedoTexture}}}};
        }
        if (entity.camera) {
            const auto& camera = *entity.camera;
            item["camera"] = {{"fov", camera.fov}, {"nearClip", camera.nearClip}, {"farClip", camera.farClip}, {"primary", camera.primary}};
        }
        if (entity.light) {
            const auto& light = *entity.light;
            item["light"] = {{"type", light.type == LightType::Directional ? "directional" : "point"}, {"color", vectorJson(light.color)},
                {"intensity", light.intensity}, {"range", light.range}};
        }
        if (entity.rigidBody) {
            const auto& body = *entity.rigidBody;
            item["rigidBody"] = {{"dynamic", body.dynamic}, {"useGravity", body.useGravity}, {"mass", body.mass},
                {"restitution", body.restitution}, {"velocity", vectorJson(body.velocity)}, {"halfExtents", vectorJson(body.halfExtents)}};
        }
        if (entity.script) item["script"] = {{"type", entity.script->type}, {"properties", entity.script->properties}};
        if (entity.audio) item["audio"] = {{"source", entity.audio->source}, {"volume", entity.audio->volume},
            {"loop", entity.audio->loop}, {"playOnStart", entity.audio->playOnStart}, {"spatial", entity.audio->spatial}};
        if (entity.animation) item["animation"] = {{"clipIndex", entity.animation->clipIndex}, {"speed", entity.animation->speed},
            {"loop", entity.animation->loop}, {"playing", entity.animation->playing}, {"time", entity.animation->time}};
        entities.push_back(std::move(item));
    }
    return result;
}

Scene Scene::fromJson(const nlohmann::json& json) {
    try {
        requireObject(json, "scene");
        if (string(json, "format", "") != "mythbreaker.scene" || !json.contains("version") || json.at("version") != 1)
            invalid("unsupported format or version");
        if (!json.contains("entities") || !json.at("entities").is_array()) invalid("entities must be an array");
        if (json.at("entities").size() > MaximumEntities) invalid("too many entities");
        Scene scene;
        scene.name = string(json, "name", "Untitled");
        if (json.contains("environment")) {
            const auto& value = json.at("environment");
            requireObject(value, "environment");
            auto& environment = scene.environment;
            environment.skyColor = vec3(value, "skyColor", environment.skyColor);
            environment.ambientColor = vec3(value, "ambientColor", environment.ambientColor);
            environment.ambientIntensity = number(value, "ambientIntensity", environment.ambientIntensity);
            environment.exposure = number(value, "exposure", environment.exposure);
            environment.gravity = vec3(value, "gravity", environment.gravity);
        }
        EntityId largestId = 0;
        for (const auto& item : json.at("entities")) {
            requireObject(item, "entity");
            SceneEntity entity;
            entity.id = entityId(item, "id", 0);
            entity.parent = entityId(item, "parent", 0);
            entity.name = string(item, "name", "Entity");
            entity.enabled = boolean(item, "enabled", true);
            largestId = std::max(largestId, entity.id);
            if (item.contains("transform")) {
                const auto& value = item.at("transform"); requireObject(value, "transform");
                entity.transform.position = vec3(value, "position", entity.transform.position);
                entity.transform.rotation = vec3(value, "rotation", entity.transform.rotation);
                entity.transform.scale = vec3(value, "scale", entity.transform.scale);
            }
            if (item.contains("mesh")) {
                const auto& value = item.at("mesh"); requireObject(value, "mesh");
                auto& mesh = entity.mesh.emplace();
                mesh.source = string(value, "source", mesh.source);
                mesh.visible = boolean(value, "visible", true);
                mesh.castsShadow = boolean(value, "castsShadow", true);
                if (value.contains("material")) {
                    const auto& material = value.at("material"); requireObject(material, "material");
                    mesh.material.baseColor = vec4(material, "baseColor", mesh.material.baseColor);
                    mesh.material.metallic = number(material, "metallic", mesh.material.metallic);
                    mesh.material.roughness = number(material, "roughness", mesh.material.roughness);
                    mesh.material.albedoTexture = string(material, "albedoTexture", "");
                }
            }
            if (item.contains("camera")) {
                const auto& value = item.at("camera"); requireObject(value, "camera");
                auto& camera = entity.camera.emplace();
                camera.fov = number(value, "fov", camera.fov);
                camera.nearClip = number(value, "nearClip", camera.nearClip);
                camera.farClip = number(value, "farClip", camera.farClip);
                camera.primary = boolean(value, "primary", true);
            }
            if (item.contains("light")) {
                const auto& value = item.at("light"); requireObject(value, "light");
                auto& light = entity.light.emplace();
                const auto type = string(value, "type", "directional");
                if (type != "directional" && type != "point") invalid("unknown light type");
                light.type = type == "point" ? LightType::Point : LightType::Directional;
                light.color = vec3(value, "color", light.color);
                light.intensity = number(value, "intensity", light.intensity);
                light.range = number(value, "range", light.range);
            }
            if (item.contains("rigidBody")) {
                const auto& value = item.at("rigidBody"); requireObject(value, "rigid body");
                auto& body = entity.rigidBody.emplace();
                body.dynamic = boolean(value, "dynamic", body.dynamic);
                body.useGravity = boolean(value, "useGravity", body.useGravity);
                body.mass = number(value, "mass", body.mass);
                body.restitution = number(value, "restitution", body.restitution);
                body.velocity = vec3(value, "velocity", body.velocity);
                body.halfExtents = vec3(value, "halfExtents", body.halfExtents);
            }
            if (item.contains("script")) {
                const auto& value = item.at("script"); requireObject(value, "script");
                auto& script = entity.script.emplace();
                script.type = string(value, "type", "");
                if (value.contains("properties")) script.properties = value.at("properties");
            }
            if (item.contains("audio")) {
                const auto& value = item.at("audio"); requireObject(value, "audio source");
                auto& audio = entity.audio.emplace();
                audio.source = string(value, "source", "");
                audio.volume = number(value, "volume", 1.0f);
                audio.loop = boolean(value, "loop", false);
                audio.playOnStart = boolean(value, "playOnStart", true);
                audio.spatial = boolean(value, "spatial", true);
            }
            if (item.contains("animation")) {
                const auto& value = item.at("animation"); requireObject(value, "animation");
                auto& animation = entity.animation.emplace();
                const auto clip = entityId(value, "clipIndex", 0);
                if (clip > static_cast<EntityId>(std::numeric_limits<int>::max())) invalid("animation clip index is too large");
                animation.clipIndex = static_cast<int>(clip);
                animation.speed = number(value, "speed", 1.0f);
                animation.loop = boolean(value, "loop", true);
                animation.playing = boolean(value, "playing", true);
                animation.time = number(value, "time", 0.0f);
            }
            scene.m_entities.push_back(std::move(entity));
        }
        if (largestId == std::numeric_limits<EntityId>::max()) invalid("entity ID space exhausted");
        scene.m_nextId = entityId(json, "nextId", largestId + 1);
        if (scene.m_nextId <= largestId || scene.m_nextId == std::numeric_limits<EntityId>::max()) invalid("invalid next entity ID");
        scene.validate();
        return scene;
    } catch (const nlohmann::json::exception& error) {
        invalid(error.what());
    }
}

void Scene::save(const std::filesystem::path& path) const { writeJsonAtomically(path, toJson()); }
Scene Scene::load(const std::filesystem::path& path) { return fromJson(readJsonFile(path)); }

nlohmann::json readJsonFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) throw std::runtime_error("Cannot read JSON file: " + path.string() + ": " + ec.message());
    if (size > MaximumJsonBytes) throw std::runtime_error("JSON file exceeds 64 MiB limit: " + path.string());
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open JSON file: " + path.string());
    try {
        Json value = Json::parse(file);
        if (file.bad()) throw std::runtime_error("I/O error reading JSON file: " + path.string());
        return value;
    } catch (const Json::exception& error) {
        throw std::runtime_error("Invalid JSON file " + path.string() + ": " + error.what());
    }
}

void writeJsonAtomically(const std::filesystem::path& path, const nlohmann::json& json) {
    if (path.empty() || path.filename().empty()) throw std::runtime_error("Save path is empty");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    static std::atomic<std::uint64_t> sequence{0};
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(sequence++);
    auto temporary = path;
    temporary += ".tmp." + suffix;
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot open temporary save file: " + temporary.string());
        const auto text = json.dump(2);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.put('\n');
        file.flush();
        if (!file) throw std::runtime_error("Failed to write save file: " + path.string());
        file.close();
        if (file.fail()) throw std::runtime_error("Failed to close save file: " + path.string());
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Failed to replace save file: " + path.string());
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

Scene Scene::createDefault() {
    Scene scene;
    scene.name = "Sandbox";
    auto& floor = scene.createEntity("Ground");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.transform.scale = {18.0f, 1.0f, 18.0f};
    floor.mesh = MeshComponent{};
    floor.mesh->material.baseColor = {0.22f, 0.27f, 0.32f, 1.0f};
    floor.mesh->material.roughness = 0.85f;
    floor.rigidBody = RigidBodyComponent{};
    floor.rigidBody->dynamic = false;
    auto& cube = scene.createEntity("Copper Cube");
    cube.transform.position = {-1.6f, 1.1f, 0.0f};
    cube.transform.rotation = {0.0f, 25.0f, 0.0f};
    cube.mesh = MeshComponent{};
    cube.mesh->material.baseColor = {0.82f, 0.39f, 0.18f, 1.0f};
    cube.mesh->material.metallic = 0.65f;
    cube.mesh->material.roughness = 0.27f;
    cube.script = ScriptComponent{"Spin", {{"degreesPerSecond", Json::array({0, 30, 0})}}};
    auto& sphere = scene.createEntity("Ceramic Sphere");
    sphere.transform.position = {1.5f, 1.0f, 0.0f};
    sphere.mesh = MeshComponent{};
    sphere.mesh->source = "builtin:sphere";
    sphere.mesh->material.baseColor = {0.16f, 0.52f, 0.74f, 1.0f};
    sphere.mesh->material.roughness = 0.22f;
    auto& dynamic = scene.createEntity("Physics Box");
    dynamic.transform.position = {0.0f, 4.0f, -2.0f};
    dynamic.mesh = MeshComponent{};
    dynamic.mesh->material.baseColor = {0.68f, 0.73f, 0.44f, 1.0f};
    dynamic.rigidBody = RigidBodyComponent{};
    auto& sun = scene.createEntity("Sun");
    sun.transform.rotation = {-50.0f, -30.0f, 0.0f};
    sun.light = LightComponent{};
    sun.light->color = {1.0f, 0.91f, 0.8f};
    auto& camera = scene.createEntity("Main Camera");
    camera.transform.position = {8.0f, 6.0f, 10.0f};
    camera.transform.rotation = {-23.0f, 38.0f, 0.0f};
    camera.camera = CameraComponent{};
    camera.script = ScriptComponent{"PlayerController", {{"speed", 5.0f}, {"jumpSpeed", 6.0f}, {"lookSensitivity", 0.12f}}};
    return scene;
}

} // namespace myth::engine
