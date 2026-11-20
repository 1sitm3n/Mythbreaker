#pragma once

#include "Scene.h"
#include <filesystem>
#include <string>
#include <vector>

namespace myth::engine {

struct ProjectConfig {
    std::string name = "Mythbreaker Project";
    std::string startScene = "Scenes/Main.scene.json";
    std::string assetDirectory = "Assets";
};

class Project {
public:
    ProjectConfig config;
    std::filesystem::path root;
    std::filesystem::path manifestPath;

    // create() accepts a new or empty directory. load() accepts a manifest or
    // project directory. Neither changes the process working directory.
    static Project create(const std::filesystem::path& directory, const std::string& name);
    static Project load(const std::filesystem::path& manifestOrDirectory);
    void save() const;

    std::filesystem::path resolve(const std::filesystem::path& relative) const;
    Scene loadStartScene() const;
    Scene loadScene(const std::filesystem::path& relative) const;
    void saveScene(const Scene& scene, const std::filesystem::path& relative = {}) const;
    std::vector<std::filesystem::path> listAssets() const; // Project-relative paths.
    void validateAssets(const Scene& scene) const; // Includes glTF/OBJ sidecar files.

    // Imports into a unique project-local bundle. glTF buffer/image and OBJ/MTL
    // dependencies are copied too; external/network URIs and escaping paths fail.
    // Returns the project-relative path to the primary imported file.
    std::string importAsset(const std::filesystem::path& source) const;

    // Exports a complete portable project into a new/empty external directory.
    // Packaging an executable is the application host's responsibility.
    void exportTo(const std::filesystem::path& destination) const;
};

} // namespace myth::engine
