#pragma once
#include <filesystem>

namespace myth::engine {
std::filesystem::path executableDirectory(const char* argv0);
std::filesystem::path findShaderDirectory(const std::filesystem::path& executableDir,
                                         const std::filesystem::path& overridePath = {});
std::filesystem::path findDefaultProject(const std::filesystem::path& executableDir);
}
