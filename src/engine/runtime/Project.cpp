#include "Project.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace myth::engine {
namespace fs = std::filesystem;
namespace {
constexpr const char* DefaultManifest = "Mythbreaker.project.json";

bool contained(const fs::path& parent, const fs::path& candidate) {
    const auto base = fs::weakly_canonical(parent);
    const auto path = fs::weakly_canonical(candidate);
    auto p = path.begin();
    for (auto b = base.begin(); b != base.end(); ++b, ++p)
        if (p == path.end() || *p != *b) return false;
    return true;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void validateConfig(const ProjectConfig& config) {
    if (config.name.empty() || config.name.size() > 256) throw std::runtime_error("Project name must contain 1 to 256 characters");
    if (!isSafeRelativePath(config.startScene) || !isSafeRelativePath(config.assetDirectory))
        throw std::runtime_error("Project paths must stay inside the project directory");
    if (fs::path(config.startScene).extension() != ".json") throw std::runtime_error("Start scene must be a JSON scene file");
}

std::string safeStem(const fs::path& path) {
    std::string name = path.stem().string();
    for (char& value : name) {
        const auto c = static_cast<unsigned char>(value);
        if (!std::isalnum(c) && value != '-' && value != '_') value = '_';
    }
    if (name.empty()) name = "Asset";
    return name.substr(0, 80);
}

std::string decodeUri(const std::string& uri) {
    if (uri.find("?") != std::string::npos || uri.find("#") != std::string::npos)
        throw std::runtime_error("Asset URI query/fragment is unsupported: " + uri);
    std::string result;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] != '%') { result += uri[i]; continue; }
        if (i + 2 >= uri.size()) throw std::runtime_error("Malformed asset URI: " + uri);
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = hex(uri[i + 1]), lo = hex(uri[i + 2]);
        if (hi < 0 || lo < 0) throw std::runtime_error("Malformed asset URI: " + uri);
        result += static_cast<char>((hi << 4) | lo);
        i += 2;
    }
    return result;
}

void addDependency(const fs::path& base, const fs::path& relative, std::set<fs::path>& files) {
    if (!isSafeRelativePath(relative) || !contained(base, base / relative))
        throw std::runtime_error("Asset dependency escapes its source folder: " + relative.string());
    if (!fs::is_regular_file(base / relative)) throw std::runtime_error("Missing asset dependency: " + (base / relative).string());
    files.insert(relative.lexically_normal());
}

nlohmann::json readGltfMetadata(const fs::path& source) {
    if (lower(source.extension().string()) != ".glb") return readJsonFile(source);
    // A GLB may keep images or additional buffers outside its binary container.
    // Inspect the JSON chunk without loading the potentially large geometry chunk.
    std::ifstream input(source, std::ios::binary);
    std::array<unsigned char, 20> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())))
        throw std::runtime_error("Truncated GLB header: " + source.string());
    const auto read32 = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(header[offset]) |
            (static_cast<std::uint32_t>(header[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(header[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(header[offset + 3]) << 24);
    };
    const auto size = fs::file_size(source);
    const auto jsonLength = read32(12);
    if (read32(0) != 0x46546C67 || read32(4) != 2 || read32(8) != size ||
        read32(16) != 0x4E4F534A || jsonLength == 0 || jsonLength % 4 != 0 ||
        jsonLength > 64 * 1024 * 1024 || jsonLength > size - header.size())
        throw std::runtime_error("Invalid GLB v2 JSON chunk: " + source.string());
    std::string text(jsonLength, '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Truncated GLB JSON chunk: " + source.string());
    try { return nlohmann::json::parse(text); }
    catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("Invalid GLB JSON: " + std::string(error.what()));
    }
}

void collectGltf(const fs::path& source, std::set<fs::path>& files) {
    const auto json = readGltfMetadata(source);
    if (!json.is_object() || !json.contains("asset") || !json.at("asset").is_object() ||
        !json.at("asset").contains("version") || json.at("asset").at("version") != "2.0")
        throw std::runtime_error("Only glTF 2.0 assets are supported");
    for (const auto* category : {"buffers", "images"}) {
        if (!json.contains(category)) continue;
        if (!json.at(category).is_array()) throw std::runtime_error("Malformed glTF " + std::string(category));
        for (const auto& item : json.at(category)) {
            if (!item.is_object()) throw std::runtime_error("Malformed glTF dependency");
            if (!item.contains("uri")) continue;
            if (!item.at("uri").is_string()) throw std::runtime_error("glTF URI must be a string");
            const auto uri = item.at("uri").get<std::string>();
            if (uri.starts_with("data:")) continue;
            addDependency(source.parent_path(), decodeUri(uri), files);
        }
    }
}

std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    value = value.substr(begin, end - begin + 1);
    if (value.size() > 1 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    return value;
}

void collectMtl(const fs::path& base, const fs::path& relative, std::set<fs::path>& files) {
    std::ifstream input(base / relative);
    if (!input) throw std::runtime_error("Cannot read OBJ material file");
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream parser(line);
        std::string token;
        parser >> token;
        if (token == "map_Kd" || token == "map_Ks" || token == "map_Bump" || token == "bump" || token == "map_d" || token == "norm") {
            std::string reference; std::getline(parser, reference); reference = trim(reference);
            if (reference.empty()) continue;
            // Reject unsupported options explicitly; silently omitting dependencies
            // would produce a project that fails only after it is moved elsewhere.
            if (reference.front() == '-') throw std::runtime_error("MTL texture options are unsupported; use an unqualified texture path or import glTF/GLB");
            addDependency(base, relative.parent_path() / reference, files);
        }
    }
}

void collectObj(const fs::path& source, std::set<fs::path>& files) {
    std::ifstream input(source);
    if (!input) throw std::runtime_error("Cannot read OBJ asset");
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream parser(line);
        std::string token; parser >> token;
        if (token != "mtllib") continue;
        std::string reference; std::getline(parser, reference); reference = trim(reference);
        if (reference.empty()) throw std::runtime_error("OBJ material library name is empty");
        // Prefer the complete name (which may contain spaces). If it does not
        // exist, OBJ also permits a whitespace-separated list of library files.
        if (fs::is_regular_file(source.parent_path() / reference)) {
            addDependency(source.parent_path(), reference, files);
            collectMtl(source.parent_path(), reference, files);
        } else {
            std::istringstream libraries(reference);
            while (libraries >> reference) {
                addDependency(source.parent_path(), reference, files);
                collectMtl(source.parent_path(), reference, files);
            }
        }
    }
}

bool directoryEmptyOrMissing(const fs::path& path) {
    return !fs::exists(path) || (fs::is_directory(path) && fs::is_empty(path));
}
} // namespace

Project Project::create(const fs::path& directory, const std::string& projectName) {
    if (directory.empty()) throw std::runtime_error("Project directory is empty");
    if (!directoryEmptyOrMissing(directory)) throw std::runtime_error("Create a project in a new or empty directory");
    Project project;
    project.config.name = projectName;
    validateConfig(project.config);
    project.root = fs::weakly_canonical(fs::absolute(directory));
    project.manifestPath = project.root / DefaultManifest;
    fs::create_directories(project.root / project.config.assetDirectory);
    fs::create_directories(project.root / "Scenes");
    auto scene = Scene::createDefault();
    scene.name = projectName;
    project.saveScene(scene);
    project.save();
    return project;
}

Project Project::load(const fs::path& manifestOrDirectory) {
    fs::path manifest = fs::absolute(manifestOrDirectory);
    if (fs::is_directory(manifest)) {
        const auto standard = manifest / DefaultManifest;
        if (fs::is_regular_file(standard)) manifest = standard;
        else {
            std::vector<fs::path> candidates;
            for (const auto& entry : fs::directory_iterator(manifest))
                if (entry.is_regular_file() && entry.path().filename().string().ends_with(".project.json")) candidates.push_back(entry.path());
            if (candidates.size() != 1) throw std::runtime_error("Project directory needs one .project.json manifest");
            manifest = candidates.front();
        }
    }
    manifest = fs::weakly_canonical(manifest);
    const auto json = readJsonFile(manifest);
    try {
        if (!json.is_object() || json.value("format", "") != "mythbreaker.project" || !json.contains("version") || json.at("version") != 1)
            throw std::runtime_error("Unsupported Mythbreaker project format or version");
        Project project;
        project.root = manifest.parent_path();
        project.manifestPath = manifest;
        project.config.name = json.at("name").get<std::string>();
        project.config.startScene = json.at("startScene").get<std::string>();
        project.config.assetDirectory = json.value("assetDirectory", std::string("Assets"));
        validateConfig(project.config);
        const auto scenePath = project.resolve(project.config.startScene);
        if (!fs::is_regular_file(scenePath)) throw std::runtime_error("Project start scene does not exist: " + scenePath.string());
        project.resolve(project.config.assetDirectory);
        return project;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("Invalid project manifest: " + std::string(error.what()));
    }
}

void Project::save() const {
    validateConfig(config);
    if (root.empty() || manifestPath.empty() || !contained(root, manifestPath)) throw std::runtime_error("Invalid project root or manifest path");
    writeJsonAtomically(manifestPath, {{"format", "mythbreaker.project"}, {"version", 1}, {"name", config.name},
        {"startScene", config.startScene}, {"assetDirectory", config.assetDirectory}});
}

fs::path Project::resolve(const fs::path& relative) const {
    if (root.empty() || !isSafeRelativePath(relative) || !contained(root, root / relative))
        throw std::runtime_error("Project-relative path escapes the project directory: " + relative.string());
    return fs::weakly_canonical(root / relative);
}

Scene Project::loadStartScene() const { return loadScene(config.startScene); }
Scene Project::loadScene(const fs::path& relative) const { return Scene::load(resolve(relative)); }
void Project::saveScene(const Scene& scene, const fs::path& relative) const {
    scene.save(resolve(relative.empty() ? fs::path(config.startScene) : relative));
}

std::vector<fs::path> Project::listAssets() const {
    std::vector<fs::path> result;
    const auto assets = resolve(config.assetDirectory);
    if (!fs::is_directory(assets)) return result;
    for (const auto& entry : fs::recursive_directory_iterator(assets)) {
        if (entry.is_symlink()) continue;
        if (entry.is_regular_file() && contained(root, entry.path())) result.push_back(fs::relative(entry.path(), root));
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string Project::importAsset(const fs::path& sourcePath) const {
    if (sourcePath.empty()) throw std::runtime_error("Import source is empty");
    const auto source = fs::canonical(sourcePath);
    if (!fs::is_regular_file(source)) throw std::runtime_error("Import requires a regular file");
    const auto extension = lower(source.extension().string());
    static const std::unordered_set<std::string> supported{
        ".gltf", ".glb", ".obj", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".wav", ".mp3", ".flac"};
    if (!supported.contains(extension)) throw std::runtime_error("Unsupported asset extension: " + extension);
    if (!isSafeRelativePath(source.filename())) throw std::runtime_error("Asset filename is not portable");
    std::set<fs::path> files{source.filename()};
    if (extension == ".gltf" || extension == ".glb") collectGltf(source, files);
    if (extension == ".obj") collectObj(source, files);
    const auto assets = resolve(config.assetDirectory);
    fs::create_directories(assets);
    const auto stem = safeStem(source);
    fs::path bundle;
    for (unsigned suffix = 1;; ++suffix) {
        if (suffix == 100000) throw std::runtime_error("Too many imports with the same name");
        bundle = assets / (suffix == 1 ? stem : stem + "_" + std::to_string(suffix));
        std::error_code error;
        if (fs::create_directory(bundle, error)) break;
        // Reserve atomically: another import may claim the same name between
        // directory checks. Never copy into or clean up a directory we do not own.
        if (error && error != std::errc::file_exists)
            throw fs::filesystem_error("Cannot create asset bundle", bundle, error);
    }
    try {
        for (const auto& relative : files) {
            if (!contained(source.parent_path(), source.parent_path() / relative)) throw std::runtime_error("Import dependency escapes source folder");
            const auto destination = bundle / relative;
            fs::create_directories(destination.parent_path());
            fs::copy_file(source.parent_path() / relative, destination, fs::copy_options::none);
        }
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(bundle, ignored); // This call created this unique directory.
        throw;
    }
    return fs::relative(bundle / source.filename(), root).generic_string();
}

void Project::validateAssets(const Scene& scene) const {
    scene.validate();
    std::set<std::string> checked;
    auto require = [&](const std::string& reference) -> fs::path {
        const auto path = resolve(reference);
        if (!fs::is_regular_file(path)) throw std::runtime_error("Missing project asset: " + reference);
        return path;
    };
    for (const auto& entity : scene.entities()) {
        if (entity.mesh) {
            const auto& reference = entity.mesh->source;
            if (!reference.starts_with("builtin:") && checked.insert(reference).second) {
                const auto path = require(reference);
                const auto extension = lower(path.extension().string());
                std::set<fs::path> dependencies;
                if (extension == ".gltf" || extension == ".glb") collectGltf(path, dependencies);
                else if (extension == ".obj") collectObj(path, dependencies);
                else throw std::runtime_error("Unsupported mesh file: " + reference);
            }
            if (!entity.mesh->material.albedoTexture.empty()) require(entity.mesh->material.albedoTexture);
        }
        if (entity.audio && !entity.audio->source.empty()) require(entity.audio->source);
    }
}

void Project::exportTo(const fs::path& destinationPath) const {
    validateConfig(config);
    if (destinationPath.empty()) throw std::runtime_error("Export destination is empty");
    const auto destination = fs::weakly_canonical(fs::absolute(destinationPath));
    if (contained(root, destination) || contained(destination, root)) throw std::runtime_error("Export destination must be outside the source project");
    if (!directoryEmptyOrMissing(destination)) throw std::runtime_error("Export destination must be a new or empty directory");
    // Validate dependencies before copying. Merely finding the .gltf file does
    // not establish that its separately stored buffers and images are available.
    validateAssets(loadStartScene());
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) throw std::runtime_error("Project export does not follow symlinks: " + entry.path().string());
        if (entry.is_regular_file()) files.push_back(fs::relative(entry.path(), root));
    }
    static std::atomic<std::uint64_t> sequence{0};
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++);
    const auto staging = destination.parent_path() / (destination.filename().string() + ".export-" + token);
    fs::create_directories(destination.parent_path());
    if (!fs::create_directory(staging)) throw std::runtime_error("Cannot create export staging directory");
    const bool targetExisted = fs::exists(destination);
    bool removedEmptyTarget = false;
    try {
        const auto manifestRelative = fs::relative(manifestPath, root);
        for (const auto& relative : files) {
            if (relative == manifestRelative) continue; // Write the live manifest below, without inheriting a read-only file attribute.
            fs::create_directories((staging / relative).parent_path());
            fs::copy_file(root / relative, staging / relative, fs::copy_options::none);
        }
        // Export the live project settings rather than a stale on-disk manifest.
        Project exported = *this;
        exported.root = staging;
        exported.manifestPath = staging / manifestRelative;
        exported.save();
        // Re-check at publication time: a directory that acquired files while the
        // export was running must never be removed or overwritten.
        if (fs::exists(destination)) {
            if (!fs::is_directory(destination) || !fs::is_empty(destination))
                throw std::runtime_error("Export destination changed while exporting");
            removedEmptyTarget = fs::remove(destination); // Never recursive.
        }
        fs::rename(staging, destination);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored); // Only the directory created by this call.
        if (targetExisted && removedEmptyTarget && !fs::exists(destination)) fs::create_directory(destination, ignored);
        throw;
    }
}

} // namespace myth::engine
