# Mythbreaker Engine

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20">
  <img src="https://img.shields.io/badge/Vulkan-1.3-red.svg" alt="Vulkan 1.3">
  <img src="https://img.shields.io/badge/Platform-Windows-lightgrey.svg" alt="Windows">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
</p>

**Mythbreaker** is a custom 3D game engine built entirely from scratch using modern C++20 and the Vulkan graphics API. This project demonstrates advanced graphics programming, engine architecture, and game systems development without relying on existing game engines or frameworks.

## 🎮 Project Vision

A third-person mythic-fantasy open-world RPG engine featuring a surreal "fractured cosmology" of floating plateaus and impossible geometry. The engine prioritizes clean architecture, modern rendering techniques, and data-oriented design principles.

---

## 🛠️ Technical Skills Demonstrated

### Graphics Programming & Vulkan
- **Complete Vulkan Rendering Pipeline**: Instance creation, device selection, swapchain management, render passes, graphics pipelines, command buffers, and synchronization primitives
- **Multi-Pass Rendering**: Scene pass to HDR offscreen buffer, post-processing pass to swapchain
- **Shadow Mapping**: Real-time directional shadow maps with depth-only pass and PCF filtering
- **Skeletal Animation**: GPU-based bone matrix computation, glTF model loading, animation blending
- **Particle Systems**: GPU-friendly particle rendering with billboarding and multiple emitter types
- **Post-Processing Pipeline**: HDR rendering, bloom extraction, ACES tone mapping, vignette, color grading
- **Dynamic Sky Rendering**: Procedural sky dome with sun positioning and atmospheric scattering

### Engine Architecture
- **Entity Component System (ECS)**: Custom data-oriented ECS with sparse set storage for cache-efficient iteration
- **Memory Management**: Vulkan Memory Allocator (VMA) integration for optimal GPU memory allocation
- **Resource Management**: RAII-based Vulkan object lifetime management
- **Modular Design**: Clear separation between engine core, rendering backend, and game systems

### Game Systems
- **Third-Person Camera**: Orbiting camera with smooth follow, pitch clamping, and collision avoidance
- **Combat System**: Attack mechanics, damage calculation, stamina management, hit detection
- **Inventory System**: Item pickup, storage, equipment slots, consumables
- **Stats & Leveling**: XP progression, stat allocation, level-up rewards
- **NPC & Dialogue**: Interactive NPCs with branching dialogue trees
- **Quest Framework**: Quest states, objectives, completion tracking
- **Save/Load System**: JSON-based game state serialization

### Procedural Generation
- **Biome System**: Multi-biome terrain with temperature/moisture-based classification
- **Noise Functions**: Fractal Brownian Motion (FBM), ridged noise, value noise
- **Vegetation Placement**: Density-based tree and rock spawning per biome
- **Chunk Streaming**: Dynamic terrain loading/unloading based on player position

### Audio
- **3D Audio System**: Spatial audio using miniaudio library
- **Procedural Sound Effects**: Attack sounds, footsteps, ambient effects
- **Dynamic Music**: Background ambient tracks with region-based transitions

### Additional Systems
- **Weather System**: Dynamic rain particles, fog density, weather state machine
- **Day/Night Cycle**: Time progression with sun movement and lighting changes
- **Input Handling**: GLFW-based input with configurable key bindings
- **Logging System**: Categorized logging with timestamps for debugging

---

## 🏗️ Architecture Overview
```
Mythbreaker/
├── src/
│   ├── app/                    # Application entry point
│   │   └── main.cpp            # Game loop, system coordination
│   ├── engine/                 # Core engine systems
│   │   ├── vulkan/             # Vulkan rendering backend
│   │   │   ├── VulkanContext   # Instance, device, queues
│   │   │   ├── VulkanSwapchain # Swapchain management
│   │   │   ├── VulkanPipeline  # Graphics pipeline creation
│   │   │   ├── VulkanBuffer    # Buffer management with VMA
│   │   │   └── VulkanTexture   # Image/sampler management
│   │   ├── ECS.h               # Entity Component System
│   │   ├── Camera.h            # Third-person camera
│   │   ├── Input.h             # Input handling
│   │   ├── Audio.h             # Audio system
│   │   ├── ParticleSystem.h    # Particle effects
│   │   ├── BiomeSystem.h       # Procedural terrain
│   │   ├── TimeSystem.h        # Day/night cycle
│   │   ├── WeatherSystem.h     # Weather simulation
│   │   └── PostProcess.h       # Post-processing effects
│   └── game/                   # Game-specific systems
│       ├── Combat.h            # Combat mechanics
│       ├── Inventory.h         # Item management
│       ├── Stats.h             # Character progression
│       └── Dialogue.h          # NPC interaction
├── shaders/                    # GLSL shaders
│   ├── lit.vert/frag           # PBR-style lighting
│   ├── skinned.vert            # Skeletal animation
│   ├── sky.vert/frag           # Procedural sky
│   ├── particle.vert/frag      # Billboard particles
│   ├── shadow.vert             # Shadow map generation
│   ├── postprocess.vert/frag   # Post-processing
│   └── ui.vert/frag            # UI rendering
├── assets/                     # Game assets
│   ├── models/                 # glTF models
│   └── textures/               # Image textures
└── third_party/                # Vendored dependencies
    ├── glfw/                   # Windowing
    ├── glm/                    # Mathematics
    ├── vma/                    # Memory allocation
    ├── stb/                    # Image loading
    ├── tinygltf/               # glTF loading
    └── miniaudio/              # Audio
```

---

## 🎯 Implemented Milestones

| Milestone | Feature | Status |
|-----------|---------|--------|
| M0-M2 | Vulkan initialization, window, triangle | ✅ |
| M3 | Depth buffer, 3D camera, mesh rendering | ✅ |
| M4 | Third-person controller, follow camera | ✅ |
| M5 | Chunked terrain, streaming architecture | ✅ |
| M6 | Entity Component System | ✅ |
| M7 | Region state machine, emergent effects | ✅ |
| M8 | Save/Load system | ✅ |
| M9-M10 | Combat system, inventory | ✅ |
| M11-M12 | Item pickups, stats/leveling | ✅ |
| M13-M15 | NPCs, dialogue, quests | ✅ |
| M16 | Skeletal animation (glTF) | ✅ |
| M17 | Shadow mapping | ✅ |
| M20 | Audio system | ✅ |
| M21 | Particle effects | ✅ |
| M22 | Weather & day/night cycle | ✅ |
| M23 | Post-processing (HDR, bloom, tone mapping) | ✅ |
| M24 | Biome system, procedural terrain | ✅ |

---

## 🖥️ Build Requirements

### Prerequisites
- **OS**: Windows 10/11
- **Compiler**: Visual Studio 2022 (MSVC v143)
- **CMake**: 3.20+
- **Vulkan SDK**: 1.3+ ([Download](https://vulkan.lunarg.com/sdk/home))
- **GPU**: Vulkan 1.3 compatible (tested on RTX 4090)

### Build Instructions
```bash
# Clone repository
git clone https://github.com/1sitm3n/Mythbreaker.git
cd Mythbreaker

# Compile shaders
.\scripts\compile_shaders.ps1

# Configure and build
cmake -S . -B build
cmake --build build --config Release

# Run
.\build\bin\Release\Mythbreaker.exe
```

### Controls
| Key | Action |
|-----|--------|
| WASD | Move |
| Mouse | Look |
| Space | Jump |
| LMB | Attack |
| I | Inventory |
| F | Interact/Talk |
| E | Use item |
| T | Fast-forward time |
| 1-5 | Quick slots |

---

## 📊 Performance

- **Target**: 60+ FPS at 1080p
- **Achieved**: 800-2000+ FPS on RTX 4090
- **Memory**: ~200MB GPU memory
- **Draw Calls**: Batched terrain, instanced vegetation

---

## 🔧 Technologies & Libraries

| Technology | Purpose |
|------------|---------|
| **C++20** | Core language with modern features |
| **Vulkan 1.3** | Low-level graphics API |
| **GLFW** | Window/input management |
| **GLM** | Mathematics (vectors, matrices, quaternions) |
| **VMA** | Vulkan memory allocation |
| **stb_image** | Texture loading |
| **tinygltf** | glTF model/animation loading |
| **tinyobjloader** | OBJ model loading |
| **miniaudio** | Cross-platform audio |
| **nlohmann/json** | Save file serialization |

---

## 📚 Key Learning Outcomes

This project demonstrates proficiency in:

1. **Low-Level Graphics Programming**: Direct GPU programming without engine abstractions
2. **Vulkan API Mastery**: Complete understanding of modern graphics API concepts
3. **Engine Architecture**: Designing scalable, maintainable game engine systems
4. **Data-Oriented Design**: Cache-efficient ECS implementation
5. **Shader Programming**: GLSL vertex/fragment shaders for various effects
6. **3D Mathematics**: Transformations, projections, quaternions, interpolation
7. **Procedural Generation**: Noise-based terrain, vegetation, weather
8. **Real-Time Rendering**: Shadow mapping, post-processing, particle systems
9. **Game Systems Design**: Combat, inventory, dialogue, progression systems
10. **Resource Management**: Memory allocation, asset loading, lifetime management

---

## 🚀 Future Roadmap

- [ ] PBR Materials with texture maps
- [ ] Screen-space reflections for water
- [ ] Frustum culling optimization
- [ ] Level-of-detail (LOD) system
- [ ] Navmesh pathfinding
- [ ] Multiplayer networking foundation

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 👤 Author

**Mehmet** - Game Engine Developer

*Built from scratch as a demonstration of advanced graphics programming and game engine development skills.*

---

<p align="center">
  <i>No commercial game engines were used in the making of this project.</i>
</p>