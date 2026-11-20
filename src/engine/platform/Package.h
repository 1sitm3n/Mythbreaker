#pragma once
#include "engine/runtime/Project.h"

namespace myth::engine {
// Exports the saved project plus this build's standalone player and shaders.
// The destination must not exist or must be empty; existing files are never replaced.
void packageProject(const Project& project, const std::filesystem::path& destination,
                    const std::filesystem::path& executableDirectory);
}
