# Engine conversion validation

Verified on 20 November 2026 against the engine conversion branch. These are
observed checks, separate from the GitHub Actions workflow configured in this
repository. Consult a workflow run for its own CI result.

## Automated checks

The full Linux desktop build completed with GCC 14.2, CMake/Ninja, Vulkan headers
and loader 1.4.309, and shaders targeting Vulkan 1.2. All four CTest suites passed:

| Suite | Observed result |
| --- | --- |
| Core | Seven groups, 137 assertions. Includes hierarchy/persistence, project containment, glTF/GLB/OBJ dependencies, concurrent imports, export, physics, behaviours, animation clocks and lifecycle reentrancy. |
| Editor workflow | Passed real ImGui/editor actions with mocked renderer/window operations: component editing, save/load, undo/redo, animation, play/pause/stop and close protection. |
| Asset import / animation | 3,917 assertions passed, including actual Duck/Fox assets, independent poses, weighted skinning and LINEAR/STEP/CUBICSPLINE interpolation. |
| Audio lifecycle | Passed actual WAV decoding/mixing with miniaudio's silent device, pause/resume, stop, source removal and missing-file recovery. |

The core also passed AddressSanitizer and UndefinedBehaviorSanitizer with the
same 137 assertions. LeakSanitizer was disabled because it is unsupported by the
execution environment; this is not a leak-check result.

A headless core/CLI build completed independently of Vulkan. Project creation,
validation and 600 fixed simulation steps completed successfully. The complete
native game-module example in the engine guide passed a C++20 syntax check.

## Live graphics checks

The native applications ran in an X11 display using Mesa llvmpipe software Vulkan
(LLVM 19.1.7) with Khronos validation enabled. This is a correctness smoke test,
not a representative GPU performance benchmark.

- Editor: rendered 124 frames, resized from 1440 × 900 to 1280 × 800 and exited
  cleanly with Ctrl+Q. The capture in `editor.png` shows the actual application.
- Player: rendered 81 frames and 177 fixed simulation steps, survived the same
  resize, and exited cleanly with Escape.
- Packaged player: launched through its generated shell launcher from a separate
  working directory, rendered 79 frames / 183 simulation steps, resized and
  exited cleanly.
- No Vulkan validation errors or warnings appeared in these runs. Visual checks
  confirmed textured independent Fox poses, material lighting, directional
  shadows, and the editor's corrected color transfer.

Packaging was also checked with an explicitly selected custom manifest while a
different default manifest existed. The package launched the selected project,
included assets and notices, and refused to overwrite a populated destination.

## Windows and the preserved prototype

All seven Windows x64 executables (editor, player, CLI and four test programs)
compiled and linked using MinGW-w64 GCC 14.2. Their PE import tables and the three
compiler runtime DLLs were inspected with no missing non-system dependency in
the assembled package. Archive integrity was checked.

**Windows binaries were not executed on Windows during this session.** The
configured Windows CI job builds with MSVC and runs the CPU/audio/editor tests;
its result must be checked separately. Native Windows GPU execution remains a
platform validation task.

The optional `MythbreakerRPG` legacy target also compiled and linked on Linux,
including its shaders. It was not launched or treated as part of the new engine's
runtime validation.
