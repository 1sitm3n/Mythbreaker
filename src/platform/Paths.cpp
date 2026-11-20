#include "engine/platform/Paths.h"
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace myth::engine {
namespace fs = std::filesystem;

fs::path executableDirectory(const char* argv0) {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) return fs::path(std::wstring(buffer.data(), size)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) return fs::weakly_canonical(buffer.data()).parent_path();
#else
    std::vector<char> buffer(32768);
    const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0) return fs::path(std::string(buffer.data(), static_cast<size_t>(size))).parent_path();
#endif
    return fs::absolute(argv0 ? argv0 : ".").parent_path();
}

fs::path findShaderDirectory(const fs::path& executableDir, const fs::path& overridePath) {
    const auto path = overridePath.empty() ? executableDir / "shaders" : fs::absolute(overridePath);
    if (!fs::is_directory(path))
        throw std::runtime_error("Shader directory not found: " + path.string() + ". Build the desktop targets or pass --shaders <directory>.");
    return path;
}

fs::path findDefaultProject(const fs::path& executableDir) {
    std::vector<fs::path> candidates{executableDir / "project", executableDir / "projects/Sandbox"};
#ifdef MYTHBREAKER_SOURCE_DIR
    candidates.emplace_back(fs::path(MYTHBREAKER_SOURCE_DIR) / "examples/Sandbox");
#endif
    for (const auto& directory : candidates)
        if (fs::is_regular_file(directory / "Mythbreaker.project.json")) return directory;
    throw std::runtime_error("No project found. Use --project <Mythbreaker.project.json> or create one with MythbreakerCLI create <directory> <name>.");
}
}
