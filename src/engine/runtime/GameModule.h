#pragma once
#include "Runtime.h"

namespace myth::engine {
// Implement this function in your game's C++ source and configure CMake with
// -DMYTHBREAKER_GAME_SOURCE=/absolute/path/to/MyGame.cpp. The same registration
// is linked into the editor, standalone player, and headless simulation tool.
void registerGameBehaviours(BehaviourRegistry& registry);
}
