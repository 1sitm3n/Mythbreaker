#include "engine/platform/Package.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace myth::engine {
namespace fs = std::filesystem;

void packageProject(const Project& project, const fs::path& destination, const fs::path& executableDirectory) {
    if (destination.empty()) throw std::runtime_error("Choose a package destination.");
    const auto target = fs::weakly_canonical(fs::absolute(destination));
    const auto sourceRoot = fs::weakly_canonical(project.root);
    auto relative = target.lexically_relative(sourceRoot);
    if (target == sourceRoot || (!relative.empty() && *relative.begin() != ".."))
        throw std::runtime_error("Package outside the source project to avoid recursive copying.");
    if (fs::exists(target) && (!fs::is_directory(target) || !fs::is_empty(target)))
        throw std::runtime_error("Package destination must be new or empty.");
#ifdef _WIN32
    const std::string playerName = "MythbreakerRuntime.exe";
#else
    const std::string playerName = "MythbreakerRuntime";
#endif
    const auto player = executableDirectory / playerName;
    if (!fs::is_regular_file(player))
        throw std::runtime_error("Standalone player not found beside the editor/CLI. Build MythbreakerRuntime before packaging.");
    if (!fs::is_directory(executableDirectory / "shaders"))
        throw std::runtime_error("Compiled shaders are missing beside the standalone player.");
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging = target.parent_path() / (target.filename().string() + ".staging-" + std::to_string(token));
    fs::create_directories(staging.parent_path());
    if (!fs::create_directory(staging)) throw std::runtime_error("Temporary package destination already exists.");
    try {
        project.exportTo(staging / "project");
        // Directory-based launching must select the exact configuration that
        // was opened, including projects loaded from a custom manifest name.
        auto packaged = project;
        packaged.root = staging / "project";
        packaged.manifestPath = packaged.root / "Mythbreaker.project.json";
        packaged.save();
        fs::copy_file(player, staging / playerName);
        fs::permissions(staging / playerName, fs::status(player).permissions());
        fs::copy(executableDirectory / "shaders", staging / "shaders", fs::copy_options::recursive);
        for (const auto& entry : fs::directory_iterator(executableDirectory)) {
            if (!entry.is_regular_file()) continue;
            const auto extension = entry.path().extension().string();
            if (extension == ".dll" || extension == ".so" || extension == ".dylib" || entry.path().filename().string().find(".so.") != std::string::npos)
                fs::copy_file(entry.path(), staging / entry.path().filename());
        }
        std::ofstream notes(staging / "README.txt");
        notes << project.config.name << "\n\nRun Launch.cmd on Windows or ./Launch.sh on Linux.\n"
              << "A Vulkan 1.2 capable graphics driver is required.\n"
              << "The package contains the saved project. Open it with MythbreakerEditor to edit.\n"
              << "Escape exits the player; Tab releases/captures the mouse.\n";
        notes.close();
        if (!notes) throw std::runtime_error("Could not write package readme.");
#ifdef _WIN32
        std::ofstream launcher(staging / "Launch.cmd");
        launcher << "@echo off\r\ncd /d \"%~dp0\"\r\n\"%~dp0MythbreakerRuntime.exe\" --project \"%~dp0project\" %*\r\n";
#else
        std::ofstream launcher(staging / "Launch.sh");
        launcher << "#!/bin/sh\ncd -- \"$(dirname -- \"$0\")\" || exit 1\nexec ./MythbreakerRuntime --project ./project \"$@\"\n";
#endif
        launcher.close();
        if (!launcher) throw std::runtime_error("Could not write package launcher.");
#ifndef _WIN32
        fs::permissions(staging / "Launch.sh", fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add);
#endif
        for (const auto* name : {"LICENSE", "THIRD_PARTY_NOTICES.md"}) {
            auto source = executableDirectory / name;
#ifdef MYTHBREAKER_SOURCE_DIR
            if (!fs::exists(source)) source = fs::path(MYTHBREAKER_SOURCE_DIR) / name;
#endif
            if (fs::is_regular_file(source)) fs::copy_file(source, staging / name);
        }
        if (fs::is_directory(executableDirectory / "licenses"))
            fs::copy(executableDirectory / "licenses", staging / "licenses", fs::copy_options::recursive);
        if (fs::exists(target)) fs::remove(target); // Verified empty; remove never recurses.
        fs::rename(staging, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
}
}
