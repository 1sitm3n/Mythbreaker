#include "Editor.h"
#include "engine/platform/DesktopHost.h"
#include "engine/runtime/GameModule.h"
#include <algorithm>
#include <chrono>

using namespace myth::engine;

int main(int argc, char** argv) {
    try {
        const auto options = parseDesktopOptions(argc, argv);
        if (options.help) { desktopUsage("MythbreakerEditor"); return 0; }
        const auto executableDir = executableDirectory(argv[0]);
        DesktopHost host("Mythbreaker Engine | Editor", options, executableDir);
        myth::editor::Editor editor(host.window);
        editor.initialize(options.project.empty() ? findDefaultProject(executableDir) : options.project, executableDir);
        registerGameBehaviours(editor.runtime().behaviours());
        if (!options.noAudio && !host.audioAvailable) editor.log(host.audio.lastError(), true);
        if (options.play) editor.runtime().play();
        auto previous = std::chrono::steady_clock::now();
        unsigned frames = 0;
        std::string audioError;
        while (!editor.quitRequested()) {
            glfwPollEvents();
            if (glfwWindowShouldClose(host.window)) {
                if (editor.requestClose()) break;
                glfwSetWindowShouldClose(host.window, GLFW_FALSE);
            }
            const auto now = std::chrono::steady_clock::now();
            const float delta = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0f, 0.25f);
            previous = now;
            if (!host.renderer.beginFrame()) { glfwWaitEventsTimeout(0.01); continue; }
            host.renderer.beginGuiFrame();
            ImGui::NewFrame();
            editor.draw(host.renderer, delta);
            ImGui::Render();
            const auto camera = editor.camera();
            const glm::mat4 inverseView = glm::inverse(camera.view);
            host.audio.update(editor.scene(), editor.projectRoot(), editor.runtime().state(), camera.position, -glm::vec3(inverseView[2]));
            if (!host.audio.lastError().empty() && audioError != host.audio.lastError()) {
                audioError = host.audio.lastError();
                editor.log(audioError, true);
            }
            host.renderer.render(editor.scene(), camera, editor.projectRoot(), editor.selectedEntity(), ImGui::GetDrawData());
            ++frames;
            if (options.frames && frames >= options.frames) break;
        }
        host.renderer.waitIdle();
        if (!options.screenshot.empty() && frames) host.renderer.captureScreenshot(options.screenshot);
        const auto& statistics = host.renderer.stats();
        std::cout << "Editor closed cleanly. Frames=" << frames << " Draws=" << statistics.drawCalls
                  << " Visible=" << statistics.visibleObjects << " Culled=" << statistics.culledObjects << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MythbreakerEditor: " << error.what() << '\n';
        return 1;
    }
}
