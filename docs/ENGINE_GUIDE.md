# Mythbreaker Engine Guide

This guide describes the native editor, runtime, and CLI implemented in Mythbreaker 0.2. Start with the [README](../README.md) for build commands. The examples below use a Ninja build at `out/build`; with Visual Studio, use the executable paths under `out/build/bin/Release` and add `.exe`.

## A first working scene

Launch `MythbreakerEditor`. Without `--project`, it opens the starter project copied beside the executable. The Sandbox demonstrates materials, lighting, a spinning object, a falling box, two independent animated fox instances, a pulsing fill light, and a camera with PlayerController attached.

Create your own project with **File → New project**. Enter a name and a new or empty directory, preferably outside the build tree. A project contains its manifest, scenes, and imported assets. New projects start with a usable sample scene; **File → New scene** creates an empty scene.

For a small physics scene:

1. Create a **Cube** named `Ground`. Set position to `(0, -0.5, 0)` and scale to `(20, 1, 20)`. Add **Box physics** and turn off **Dynamic**. The default half extents `(0.5, 0.5, 0.5)` are multiplied by world scale, producing a floor twenty units across.
2. Create another **Cube** at `(0, 4, 0)`. Add **Box physics** with Dynamic and Use gravity enabled. Keep its default unit scale.
3. Add a **Directional light**. Rotate it so its local forward direction points toward the scene. Adjust intensity and color in the Inspector.
4. Navigate to a useful view, create a **Camera**, mark it **Primary game camera**, and choose **Align with editor view**. Use a root-level camera for direct position and rotation alignment.
5. Save as `Scenes/Physics.scene.json`. Choose **File → Use current scene at startup** if the player should launch this scene.
6. Click **Play**. The dynamic box falls onto the static ground. **Pause** freezes simulation; **Step** advances one fixed tick; **Stop** restores the authoring scene.

Mesh appearance and collision are separate components. A visible ground mesh without Box physics will not stop falling objects.

## Projects, scenes, and persistence

### Project files

A project uses this layout by default:

| Path | Contents |
| --- | --- |
| `Mythbreaker.project.json` | Project name, asset directory, startup scene, and format version. |
| `Scenes/Main.scene.json` | Initial scene. Additional scenes can be stored alongside it. |
| `Assets/` | Imported model bundles, textures, and audio files. |

A manifest is ordinary JSON:

```json
{
  "format": "mythbreaker.project",
  "version": 1,
  "name": "My Game",
  "assetDirectory": "Assets",
  "startScene": "Scenes/Main.scene.json"
}
```

Open either the project directory or its manifest. The startup path is relative to the project. The runtime and `simulate` command load this scene, which can differ from the scene currently open in the editor. Use **File → Use current scene at startup** to update the manifest; the export dialog also displays the chosen startup scene.

Projects use local asset references such as `Assets/Robot/Robot.glb`, or supported primitive identifiers such as `builtin:cube`. Absolute asset paths, paths escaping the project, and network dependencies are rejected. This makes a copied project independent of the original import location.

### Scene data

A scene stores its name, environment, entities, and a monotonic ID counter. Each entity has a stable positive ID, name, enabled state, parent ID, and local transform. Optional components hold mesh/material, camera, light, box physics, behaviour, audio, and animation data.

| Convention | Meaning |
| --- | --- |
| Coordinate system | Right-handed, positive Y up. Cameras look along local negative Z. |
| Transform position and scale | Local to the entity's parent. Root transforms are in world space. |
| Rotation | Euler angles in degrees. |
| Entity ID `0` | No entity / no parent. |
| Physics velocity | World-space units per second. |
| Animation time | Seconds. |

Scene load validates the data before replacing the current scene. Invalid IDs, missing parents, hierarchy cycles, non-finite values, zero scales, invalid component ranges, and unsupported file versions produce an error. Saves write a temporary sibling file and use atomic replacement where the filesystem supports it. The format is versioned; there is no general migration system for future schema versions yet.

Use source control for project history. Editor undo covers scene edits in the current session, including entity and component changes. It does not remove imported files from disk, undo project creation/export, or recover arbitrary external file edits. There is no automatic background save or crash recovery journal.

### Saving and switching

**Save scene** updates the current scene file. **Save scene as** writes a scene under a new project-relative path, for example `Scenes/Level02.scene.json`; it does not automatically replace the startup scene. **Open scene** accepts a scene within the current project.

An asterisk marks unsaved authoring changes. Creating/opening another scene or project, or closing the editor, prompts to Save, Discard, or Cancel. Save errors leave the authored work in memory so the path or data can be corrected.

Play uses an in-memory copy of the authoring scene. Stop restores the state that existed before Play, including unsaved authoring edits. Runtime movement, spawned entities, and deleted entities do not become saved level changes. Scene persistence is not a game-specific save-game system; implement progression and save-game rules in your game layer.

## Editor workspace and controls

The workspace contains **Hierarchy**, **Inspector**, **Viewport**, **Assets**, and **Console**. Panels can be docked and resized. **View → Reset workspace layout** restores the default arrangement. Layout changes currently last for the session; they are not saved between launches.

### Selection and hierarchy

Click an entity in Hierarchy or a rendered mesh in the viewport to select it. Viewport picking uses mesh bounds, including current per-instance bounds for animated meshes; it is not triangle-accurate selection. Cameras, lights, and empty entities can be selected in Hierarchy.

Drag an entity onto another to change its parent. The scene API attempts to retain its world transform and rejects cycles or transforms that cannot be represented safely. Drop onto **Drop here to unparent** to return to the root. Duplication copies the whole subtree with new IDs; deletion removes the entity and its descendants.

The Inspector shows the selected entity's components. Use **+ Add component** to attach one, or right-click a component header to remove it. Transform is always present. When no entity is selected, the Inspector shows the scene name and environment. Scene environment is also available below an entity's components.

### Authoring camera

These controls operate when **Game camera** is off:

| Input | Action |
| --- | --- |
| Right mouse held | Look around and capture the pointer. |
| WASD while holding right mouse | Move forward/back and sideways. |
| Q / E while holding right mouse | Move down / up. |
| Shift while flying | Move faster. |
| Mouse wheel while holding right mouse | Adjust movement speed. |
| Mouse wheel otherwise | Dolly the camera forward/back. |
| Alt + right mouse | Orbit the selected entity. |
| F over the viewport | Frame the selection. |
| Drag a colored gizmo axis | Translate the selected entity along a world axis. |
| Escape | Release viewport navigation. |

Rotation and scale are edited in the Inspector. The current viewport gizmo handles translation only. Moving the editor camera does not modify scene cameras.

### Shortcuts

| Shortcut | Action |
| --- | --- |
| Ctrl+Shift+N | New project. |
| Ctrl+O | Open project. |
| Ctrl+N | New scene. |
| Ctrl+S / Ctrl+Shift+S | Save / Save as. |
| Ctrl+I | Import asset. |
| Ctrl+Z | Undo. |
| Ctrl+Y or Ctrl+Shift+Z | Redo. |
| Ctrl+D | Duplicate selected subtree. |
| Delete | Delete selected subtree. |
| Ctrl+Q | Quit, with unsaved-change protection. |

Shortcuts are suppressed while editing text where appropriate. File and folder selection uses path-entry dialogs, not operating-system file pickers. Copy a path into the dialog and choose **Continue**; failures appear in the error dialog and Console.

### Play and game cameras

**Play** creates the runtime copy and starts fixed updates. The Inspector becomes read-only while playing. **Pause** retains the current runtime state; **Step** advances one sixtieth of a second; **Stop** returns to editing.

A primary enabled camera provides Game camera view. If there is no enabled primary camera, the editor reports this and retains the authoring camera. Disable **Game camera** during Play to inspect the running scene freely without moving its game camera.

In Game camera view, hold right mouse to send WASD, mouse delta, and Space input to native behaviours. Attach `PlayerController` to an appropriate entity to consume this input. A camera without a controller does not automatically become a controllable player.

The standalone runtime captures the pointer on launch. WASD moves in camera-relative horizontal directions; Q/E supplies vertical movement for controllers that use it; Space requests a jump. Tab toggles pointer capture, and Escape exits. These keys supply `RuntimeInput`; the active behaviour decides what happens.

## Importing and using assets

### Models and their dependencies

Use **File → Import asset**, **Assets → Import**, or:

```sh
./out/build/bin/MythbreakerCLI import ../MyGame /path/to/Robot.glb
```

The importer creates a project-local bundle with a unique name. It does not overwrite an earlier import with the same source name. Source files remain untouched.

Supported model entry points are glTF 2.0 (`.gltf`, `.glb`) and Wavefront OBJ (`.obj`). A `.gltf` import copies referenced buffers and images. An OBJ import follows its `.mtl` material files and texture references. Keep these dependencies beneath the source model's folder before importing. Embedded glTF data stays embedded. Self-contained GLB is convenient for moving an animated asset as one file.

Remote URIs, dependencies escaping the source bundle, and unsupported MTL texture options are rejected with an error. For example, an MTL line with texture transform flags must be simplified to a plain texture path or converted to glTF. Some model extensions and material features are outside the renderer's supported subset; importing a file does not guarantee support for every authoring-tool feature in it.

After import, double-click a model in Assets or drag it into the viewport. The import dialog can also instantiate the model immediately. Assign another asset by dragging it onto a mesh source field. **Refresh** rescans Assets and clears the renderer cache after external file changes. The CLI import command only copies the files; it does not add an entity to a scene.

### Materials and textures

A mesh component contains a base color, metallic and roughness values, and an optional albedo texture. Imported glTF model parts retain supported source colors and albedo textures; entity material values provide the scene-level controls. For OBJ, project import copies the referenced MTL files and textures, but the current renderer does not automatically apply their material values or texture assignments. Set the entity's material values and assign its albedo texture manually in the Inspector.

PNG, JPEG, TGA, and BMP are practical albedo formats. Import the texture, then drag it onto **Albedo texture**. Use **Clear texture** to remove the override. The importer also accepts HDR image files, but the current scene workflow does not provide an HDR environment-map or image-based-lighting system.

This renderer does not expose a complete glTF PBR material implementation. Normal/occlusion/metallic-roughness texture maps, transparent material authoring, material graphs, and multiple editable material slots are not available in the Inspector. Convert or simplify assets accordingly.

### Animation

When an instantiated glTF/GLB finishes loading and exposes supported clips, the editor attaches an **Animation** component. You can also use **+ Add component → Animation** explicitly, including for an entity whose mesh source was assigned manually. Clip names and durations appear after the model has loaded; a numeric clip index remains available before metadata is ready.

| Setting | Effect |
| --- | --- |
| Clip | Zero-based clip index from the imported model. |
| Speed | Playback rate from 0 to 10; 1 is normal speed. |
| Loop | Wrap clip time; otherwise hold the final pose after its duration. |
| Playing | Allow the runtime to advance this animation's clock. |
| Time | Authored initial offset in seconds; also useful for inspecting a pose in Edit mode. |
| Restart clip | Reset the authored time to zero. |

Play advances the component's clock on the fixed update. Pause stops it, and Stop restores the authored offset. Animation controls are read-only while the scene is playing, following the rest of the Inspector; change the authored clip or settings after Stop.

Node translation, rotation, and scale tracks support STEP, LINEAR, and CUBICSPLINE interpolation. Skeletal vertices and normals are evaluated on the CPU and uploaded per animated instance. Instances of one model can therefore use different clips and times. This is not GPU skinning. There is no blend tree, animation mixing, retargeting, inverse kinematics, root-motion extraction, or morph-target animation. Animated bones do not drive physics colliders automatically.

### Audio

Import WAV, MP3, or FLAC for playback, create an Audio entity or add **Audio source**, and assign the project-relative audio path. Set Volume, Loop, Play on start, and Spatial audio. An empty source is allowed while authoring and produces no voice.

Play on start begins the source when entering Play. Spatial sources follow the entity's world position, and the active view camera supplies the listener. Pause suspends active voices, Resume continues them, and Stop destroys the play-session voices. There is no separate asset-browser audio audition button or timeline editor.

Host code can use `AudioMixer::play`, `stop`, and `stopAll` for explicit control. Behaviour callbacks currently receive scene data and input, rather than an audio service reference; a richer gameplay service API is an extension point.

If no audio device can be opened, the desktop application reports it and continues visually. `--no-audio` skips initialization. `--audio-null` decodes and mixes without a physical output device, which is useful for smoke testing; it does not replace a listening test.

## Components, lighting, and physics

### Cameras and environment

Camera components expose field of view, near/far clip planes, and the primary-camera flag. Keep camera transforms at unit scale. **Align with editor view** sets a root camera's position and rotation to the current authoring view. For a parented camera, position aligns in parent space and rotation is adjusted manually.

The environment sets the clear/sky color, ambient color and intensity, exposure, and gravity. The sky is a scene color, not a procedural atmosphere or skybox system. Tone mapping and exposure are part of the scene shader; the legacy RPG's bloom and other post-processing passes are separate code.

### Lights

The scene renderer uses one enabled directional light and up to eight enabled point lights. Rotate a directional light to aim it. Point lights use position, color, intensity, and range. Directional shadows use a shadow map with filtering; point lights do not cast shadows in this pipeline. The directional shadow region is finite, so very large scenes need a more advanced shadow strategy.

The viewport reports draw calls, visible objects, culled objects, and triangle counts. These are current renderer counters, not a performance guarantee. Frustum culling excludes meshes outside the active camera's view; bounding volumes are also used for picking.

### Box physics

**Box physics** supplies both the collider and simple rigid-body settings:

| Property | Meaning |
| --- | --- |
| Dynamic | Move and resolve the body during simulation; off means static. |
| Use gravity | Apply the scene's gravity to a dynamic body. |
| Mass | Positive mass used for collision response. |
| Restitution | Bounciness. Low-speed resting contacts suppress small bounces. |
| Velocity | Initial world-space linear velocity. |
| Half extents | Positive local half-size, multiplied by world scale. |

The physics solver resolves world-aligned box overlaps and changes translation and linear velocity. Its bounds enclose the transformed local box: rotation can enlarge these bounds, but collision faces remain aligned to the world axes. A tilted rendered surface therefore does not create a tilted collision surface. There is no angular velocity, friction model, constraint/joint system, triangle-mesh collision, or collision-event authoring interface.

Adaptive substeps reduce tunneling at ordinary speeds, but they are bounded and are not continuous collision detection. Very fast projectiles and thin geometry require swept tests or a stronger physics backend. Keep colliders and transforms simple when validating a game mechanic. Parent-child body pairs are not solved against one another.

`PlayerController` sets horizontal velocity for an entity with a dynamic body and permits a jump when its transient grounded flag is true. Without a dynamic body, it translates the entity directly. Attach it to a camera for a simple free movement prototype, or arrange a controlled body and camera hierarchy for a game-specific controller. It is not a complete character motor with slopes, stairs, crouching, or camera collision.

## Native C++ game code

### The registration hook

The core defines `Behaviour`, `BehaviourRegistry`, and `RuntimeInput` in [`Runtime.h`](../src/engine/runtime/Runtime.h). The application hook is declared in [`GameModule.h`](../src/engine/runtime/GameModule.h):

```cpp
void myth::engine::registerGameBehaviours(
    myth::engine::BehaviourRegistry& registry);
```

`MYTHBREAKER_GAME_SOURCE` selects a C++ source implementing this function. By default it is [`examples/native/ExampleBehaviours.cpp`](../examples/native/ExampleBehaviours.cpp), which registers `Orbit` and `PulseLight`. A custom source replaces that module; core-provided types remain registered.

Here is a complete game module:

```cpp
#include "engine/runtime/GameModule.h"
#include <cmath>
#include <memory>
#include <stdexcept>

namespace myth::engine {
namespace {
class Turntable final : public Behaviour {
    float m_degreesPerSecond = 45.0f;
public:
    void onCreate(Scene&, EntityId, const nlohmann::json& properties) override {
        m_degreesPerSecond = properties.value("degreesPerSecond", 45.0f);
        if (!std::isfinite(m_degreesPerSecond))
            throw std::runtime_error("Turntable speed must be finite");
    }

    void onFixedUpdate(Scene& scene, EntityId id, float dt,
                       const RuntimeInput&) override {
        if (auto* entity = scene.find(id))
            entity->transform.rotation.y += m_degreesPerSecond * dt;
    }
};
}

void registerGameBehaviours(BehaviourRegistry& registry) {
    registry.registerType("Turntable", [] {
        return std::make_unique<Turntable>();
    });
}
}
```

Save it as `MyGame.cpp` and configure the existing build using its absolute path:

```sh
cmake -S . -B out/build -DMYTHBREAKER_GAME_SOURCE=/absolute/path/to/MyGame.cpp
cmake --build out/build --parallel
```

On Windows, quote paths containing spaces and add `--config Release`. Reopen the rebuilt editor, add **Native behaviour**, choose **Turntable**, and add a Number property named `degreesPerSecond`. Set it to `45`, then Play. If absent, the behaviour uses its default. The same compiled type is available to the runtime and CLI simulation and is included in the packaged player.

There is no native-code hot reload. Rebuild and restart after C++ changes. Scene property edits do not require recompilation, but properties are read by this example when the play instance is created.

### Lifecycle and safe scene access

`onCreate` receives the component's JSON properties. `onFixedUpdate` runs at the engine's fixed timestep while the entity is enabled. `onDestroy` runs when an active instance is torn down. Runtime reconciliation creates or removes behaviour instances as entity components and enabled state change.

Store **entity IDs**, not long-lived pointers or references into `Scene::entities()`: creating or deleting entities can reallocate the backing container. Reacquire an entity with `scene.find(id)` inside callbacks. Use `createEntity`, `destroyEntity`, `duplicateEntity`, and `reparent` to change topology; do not rewrite IDs or splice the entity vector yourself.

Runtime lifecycle methods such as `play`, `stop`, `setScene`, `pause`, `resume`, `stepOnce`, and `update` belong to the host. Do not call them recursively from behaviour factories or callbacks. The runtime detects these calls and reports a failure; callbacks may still mutate scene entities through the scene API.

A scene currently has at most one Native behaviour component per entity. Compose related logic in a behaviour, or distribute it across entities. Properties can contain arbitrary JSON through code or a scene file; the editor provides controls for numbers, booleans, strings, and three-number vectors. Nested structures are displayed as JSON text rather than a general schema editor.

Missing types and callback exceptions are reported, and failed behaviours are disabled instead of terminating the editor. Validate property types and ranges in `onCreate`. A successful JSON scene load cannot verify a game's custom property contract.

### Supplied behaviour types

| Type | Properties and behavior |
| --- | --- |
| `Spin` | `degreesPerSecond`: vector, default `[0, 30, 0]`; rotates local Euler angles. |
| `Oscillate` | `axis`: default `[0, 1, 0]`, `amplitude`: `1`, `frequency`: `1` Hz, `phase`: `0` radians. |
| `PlayerController` | `speed`: `5`, `jumpSpeed`: `6`, `lookSensitivity`: `0.12`; consumes movement, look, and jump input. |
| `Orbit` | Example module: `radius`: `2`, `degreesPerSecond`: `45`; moves around a local center. |
| `PulseLight` | Example module: `baseIntensity`: `2`, `amplitude`: `1`, `frequency`: `0.5` Hz; changes an attached light. |

The editor inserts the standard properties for the three core types. For custom/example module types, **Add property** creates overrides; the C++ defaults apply when a key is absent.

### Fixed-step timing

`Runtime::FixedStep` is `1/60` second. Frame time accumulates into fixed updates, with at most 15 steps per rendered frame and a bounded frame delta. Pause stops updates and Step runs exactly one fixed update. Behaviour callbacks run before physics, followed by animation clock advancement.

This produces repeatable fixed-step tests for a given setup. It is not a promise of bit-identical floating-point results across CPUs or a networking lockstep implementation. Hitches are bounded to avoid unbounded catch-up work; build game timing around the runtime clock and delta rather than render-frame counts.

## Running, validating, and exporting

### CLI commands

```sh
./out/build/bin/MythbreakerCLI create ../MyGame "My Game"
./out/build/bin/MythbreakerCLI validate ../MyGame
./out/build/bin/MythbreakerCLI simulate ../MyGame 600
./out/build/bin/MythbreakerCLI import ../MyGame /path/to/Model.glb
./out/build/bin/MythbreakerCLI export ../MyGame ../MyGame-project-copy
./out/build/bin/MythbreakerCLI package ../MyGame ../MyGame-release
```

| Command | Result |
| --- | --- |
| `create` | Creates a manifest, asset directory, scene directory, and starter scene. Destination must be new or empty. |
| `validate` | Loads the startup scene and checks its asset references/dependencies. It does not render every scene or validate every feature of a model. |
| `simulate` | Runs the startup scene for the requested fixed steps; default 600, equivalent to ten simulated seconds. No display, audio output, or disk scene changes. |
| `import` | Copies an asset bundle and prints its relative path. |
| `export` | Copies a portable project, without a player. |
| `package` | Copies the project plus the built player, shaders, adjacent runtime libraries, and launcher. |

Failures return a nonzero process exit code. Projects can be supplied as a directory or manifest. `simulate` links the configured native game module, so it is useful for checking behaviour registration and runtime failures without a desktop.

### Desktop options

Both desktop applications accept these options; run `--help` for usage:

| Option | Purpose |
| --- | --- |
| `--project <path>` | Open a specific project directory or manifest. |
| `--shaders <directory>` | Override the compiled shader directory. |
| `--width <n> --height <n>` | Initial window dimensions, between 320×240 and 7680×4320. |
| `--frames <n>` | Exit after this many rendered frames; zero means unlimited. |
| `--screenshot <file.ppm>` | Save the final scene viewport as RGB PPM. Editor panels are excluded. |
| `--no-audio` | Disable audio initialization. |
| `--audio-null` | Decode/mix without an output device. |
| `--play` | Start the editor in Play. The standalone runtime always plays. |

The default project lookup checks `project/` beside the executable, then `projects/Sandbox/`. Engine shaders are also resolved beside the executable, so normal launch does not depend on the shell's current directory.

The starter project is copied into a fresh desktop build output. Relinking preserves an existing copy, including authored changes. Keep your actual project outside build output anyway, because deleting a build directory should never delete your game.

### Standalone export

Save your scene and confirm the startup scene. In the editor, choose **File → Export standalone**, or use CLI `package`. Choose a destination outside the source project that is new or empty. Export uses the saved project files; the editor saves pending edits to the current scene before packaging.

A package includes:

| Entry | Purpose |
| --- | --- |
| `MythbreakerRuntime` or `.exe` | Compiled player, including the configured native game module. |
| `project/` | Saved project manifest, scenes, and assets. |
| `shaders/` | Compiled engine shaders. |
| `Launch.sh` or `Launch.cmd` | Starts the player with the packaged project. |
| Adjacent runtime libraries | `.dll`, `.so`, or `.dylib` files found beside the original executable. |
| Notices | Engine/dependency notices when present beside the build or in the source tree. |

Run `./Launch.sh` on Linux or `Launch.cmd` on Windows. The runtime and shaders must have been built before packaging; a headless-only build cannot produce a player package by itself. Export copies the entire project, so remove unintended files from the source project before distributing it.

Packaging targets the same OS and architecture as the compiled runtime. It does not cross-compile, discover every transitive system library, install graphics drivers, or produce an application installer. A Vulkan 1.2 capable driver and required system runtime libraries are still needed. Test the package on a clean target machine. For Linux distribution across different systems, account for the compiler/libc and loader compatibility of your build.

Do not use project export as a replacement for backups or source control. Existing nonempty destinations are rejected, and staging is used so a failed export can be cleaned up without replacing a working package.

## Build options and checks

[CMakeLists.txt](../CMakeLists.txt) defines the build:

| Option | Default | Effect |
| --- | --- | --- |
| `MYTHBREAKER_BUILD_DESKTOP` | `ON` | Build editor, runtime, graphics, audio, and desktop-related tests. |
| `MYTHBREAKER_BUILD_TESTS` | `ON` | Build and register CTest suites. |
| `MYTHBREAKER_BUILD_LEGACY_DEMO` | `OFF` | Build the preserved RPG executable. It still requires Vulkan and GLFW. |
| `MYTHBREAKER_GAME_SOURCE` | Example behaviour module | Source implementing the native game registration hook. |

`desktop` and `headless` [Ninja presets](../CMakePresets.json) are available. Configure/build/test presets require CMake 3.21 or newer; direct `cmake -S ... -B ...` commands support 3.20. A Windows Ninja build needs a configured compiler environment, such as a Visual Studio developer terminal.

CMake compiles engine shaders with `glslc` or `glslangValidator` and copies them beside the applications. An old manually compiled shader directory is not needed. Build in a fresh `out/` directory rather than reusing a cache made on another machine or with another generator.

The [Engine builds workflow](../.github/workflows/engine.yml) configures Linux desktop/Vulkan smoke checks, Windows MSVC builds and tests, and a headless CLI workflow. Successful desktop jobs upload engine bundles as GitHub Actions artifacts. Extract the Windows artifact and run `MythbreakerEditor.exe`. The Linux artifact contains `Mythbreaker-linux-x64.tar.gz`; extract that archive with `tar -xzf Mythbreaker-linux-x64.tar.gz` to retain executable permissions, then run `Mythbreaker-linux/MythbreakerEditor`. Consult each workflow run for its actual result.

Run checks with:

```sh
ctest --test-dir out/build --output-on-failure
./out/build/bin/MythbreakerCLI validate examples/Sandbox
./out/build/bin/MythbreakerCLI simulate examples/Sandbox 600
```

Core tests exercise data, project operations, simulation lifecycle, and physics. Asset tests exercise CPU import and animation. Editor workflow tests construct real Dear ImGui frames and exercise scene editing, history, persistence, animation state, and close protection with a mocked renderer. The audio lifecycle suite decodes and mixes a real WAV through a silent device and exercises pause/resume, explicit stop, unassigned sources, and missing-file recovery. These tests do not substitute for a real graphics-driver run, audio listening, or a package test on another machine.

For desktop smoke testing:

```sh
./out/build/bin/MythbreakerEditor --project examples/Sandbox --frames 120 --audio-null --screenshot viewport.ppm
./out/build/bin/MythbreakerRuntime --project examples/Sandbox --frames 120 --no-audio
```

A display server and Vulkan driver are still required. `--frames` makes a graphical run finite; it is not a headless renderer mode.

## Troubleshooting

| Symptom | Check or action |
| --- | --- |
| CMake cannot find Vulkan | Install Vulkan development headers/loader or the SDK; use the headless build if only CLI/core tests are needed. |
| `glslc` / `glslangValidator` missing | Put an SDK shader compiler on `PATH` or set the Vulkan SDK environment, then reconfigure. |
| Linux GLFW configuration fails | Install the X11 development dependencies required by GLFW, or deliberately configure an available backend. |
| Cannot initialize desktop display | Run in a desktop session. Use `MythbreakerCLI` for server/CI tasks that do not need rendering. |
| Vulkan unavailable or no suitable device | Install a compatible graphics driver; confirm Vulkan 1.2 support. A virtual desktop still needs a working Vulkan implementation. |
| Missing compiled shader files | Build the desktop targets and keep `shaders/` beside the executable, or pass `--shaders`. |
| Blank/new scene | Add visible geometry, a light, and a camera as needed. Check enabled/visible flags, scale, and clip planes; select a mesh and press F in authoring view. |
| Game camera cannot be enabled | Add an enabled camera and mark it primary. An enabled parent is required too. |
| Camera does not move in Play | Attach `PlayerController`, enable Game camera, and hold right mouse. The standalone runtime uses Tab to capture/release the pointer. |
| Object falls through the floor | Add a static Box physics component to the floor, verify half extents/world scale, and reduce extreme velocities. Rendered geometry alone is not collision. |
| Imported model is missing | Read Console. Check dependencies, supported format/extensions, and project-relative paths. Use Refresh after repairing files. |
| Animation stays still | Check the mesh has supported clips, Animation is attached, clip index is valid, Playing is enabled, speed is nonzero, and the runtime is not paused. Morph-only animation is unsupported. |
| Behaviour absent or unknown | Build the intended `MYTHBREAKER_GAME_SOURCE`, restart all applications, and match the registered type name exactly. |
| Audio is silent | Check source format/path, enabled state, volume, Play on start, spatial distance, and the audio-device error message. Test WAV/MP3/FLAC; use `--audio-null` only to inspect decoding/mixing without sound. |
| Save rejected | Use a valid name, nonzero transform scale, valid component values, and a path inside the project. Correct the error and retry; unsaved scene data remains in memory. |
| Export starts a different scene | Save and choose **Use current scene at startup**; verify the export dialog's startup scene. |
| Package says player is missing | Build `MythbreakerRuntime` beside the editor/CLI. Keep its compiled shaders beside it. |
| Destination not empty | Choose a new release directory; export intentionally avoids replacing an existing package. |
| Build cache references another path/compiler | Configure a new build directory with the desired generator. |

The Console exposes authoring, import, and runtime errors. CLI and desktop startup failures also print to standard error. Report the exact error, build configuration, platform, and a minimal reproducing scene when investigating a problem.

## Current limits

Mythbreaker provides an editable project-to-player workflow and reusable native engine components. The following are not implemented as part of that workflow:

- Prefabs, scene composition/streaming, terrain authoring, a game UI editor, visual scripting, or native-code hot reload.
- A complete material pipeline, normal-map authoring, transparent materials, point-light shadows, advanced post-processing, or GPU skinning.
- Animation blending/state machines, morph targets, retargeting, IK, or animation-driven collision.
- Angular rigid-body dynamics, mesh/convex collision, joints, continuous collision detection, or a production character controller.
- Asset cooking/compression, encrypted bundles, automatic platform deployment, installer generation, or guaranteed system-dependency closure.
- Multiplayer, navigation/pathfinding, a general save-game system, production profiling tools, or a large-world performance guarantee.

The original RPG prototype's gameplay systems remain in the repository and can be built as `MythbreakerRPG` with `MYTHBREAKER_BUILD_LEGACY_DEMO=ON`. Its combat, quests, procedural terrain, inventory, weather, and legacy rendering features are not automatically available as new scene components. Port the specific systems your game needs into the runtime or native game module and test them through the same editor/player lifecycle.
