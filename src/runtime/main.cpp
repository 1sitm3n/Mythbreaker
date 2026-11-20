#include "engine/platform/DesktopHost.h"
#include "engine/runtime/Project.h"
#include "engine/runtime/GameModule.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>

using namespace myth::engine;

static RenderCamera sceneCamera(const Scene& scene, float aspect) {
    const SceneEntity* selected = nullptr;
    for (const auto& entity : scene.entities()) {
        if (!entity.camera || !scene.isEnabled(entity.id)) continue;
        if (!selected || entity.camera->primary) selected = &entity;
        if (entity.camera->primary) break;
    }
    RenderCamera result;
    if (selected) {
        const auto world = scene.worldMatrix(selected->id);
        const auto& camera = *selected->camera;
        result.position = glm::vec3(world[3]);
        // Match the editor's game camera: inherited scale must not distort view.
        const auto forward = glm::normalize(-glm::vec3(world[2]));
        const auto up = glm::normalize(glm::vec3(world[1]));
        result.view = glm::lookAtRH(result.position, result.position + forward, up);
        result.projection = glm::perspectiveRH_ZO(glm::radians(camera.fov), aspect, camera.nearClip, camera.farClip);
    } else {
        result.position = {8, 6, 10};
        result.view = glm::lookAtRH(result.position, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
        result.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
    }
    result.projection[1][1] *= -1.0f;
    return result;
}

int main(int argc, char** argv) {
    try {
        const auto options = parseDesktopOptions(argc, argv);
        if (options.help) { desktopUsage("MythbreakerRuntime"); return 0; }
        const auto executableDir = executableDirectory(argv[0]);
        const auto project = Project::load(options.project.empty() ? findDefaultProject(executableDir) : options.project);
        Runtime runtime(project.loadStartScene());
        registerGameBehaviours(runtime.behaviours());
        DesktopHost host(project.config.name + " | Mythbreaker", options, executableDir);
        // Preserve short key presses until the host polls them, even on a slow
        // software renderer or when a release arrives in the same event batch.
        glfwSetInputMode(host.window, GLFW_STICKY_KEYS, GLFW_TRUE);
        if (!options.noAudio && !host.audioAvailable) std::cerr << host.audio.lastError() << '\n';
        runtime.play();
        bool captured = true, tabWasDown = false, jumpWasDown = false;
        glfwSetInputMode(host.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        double previousX = 0, previousY = 0;
        glfwGetCursorPos(host.window, &previousX, &previousY);
        auto previous = std::chrono::steady_clock::now();
        unsigned frames = 0;
        while (!glfwWindowShouldClose(host.window)) {
            glfwPollEvents();
            if (glfwGetKey(host.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
            const bool tab = glfwGetKey(host.window, GLFW_KEY_TAB) == GLFW_PRESS;
            if (tab && !tabWasDown) {
                captured = !captured;
                glfwSetInputMode(host.window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
                glfwGetCursorPos(host.window, &previousX, &previousY);
            }
            tabWasDown = tab;
            RuntimeInput input;
            double x = 0, y = 0;
            glfwGetCursorPos(host.window, &x, &y);
            if (captured && glfwGetWindowAttrib(host.window, GLFW_FOCUSED)) {
                input.lookDelta = {static_cast<float>(x - previousX), static_cast<float>(y - previousY)};
                const glm::mat4 cameraWorld = glm::inverse(sceneCamera(runtime.scene(), 1.0f).view);
                glm::vec3 forward = -glm::vec3(cameraWorld[2]);
                forward.y = 0;
                if (glm::length(forward) > 0.0001f) forward = glm::normalize(forward); else forward = {0, 0, -1};
                const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                if (glfwGetKey(host.window, GLFW_KEY_W) == GLFW_PRESS) input.movement += forward;
                if (glfwGetKey(host.window, GLFW_KEY_S) == GLFW_PRESS) input.movement -= forward;
                if (glfwGetKey(host.window, GLFW_KEY_D) == GLFW_PRESS) input.movement += right;
                if (glfwGetKey(host.window, GLFW_KEY_A) == GLFW_PRESS) input.movement -= right;
                if (glfwGetKey(host.window, GLFW_KEY_E) == GLFW_PRESS) input.movement.y += 1;
                if (glfwGetKey(host.window, GLFW_KEY_Q) == GLFW_PRESS) input.movement.y -= 1;
                if (glm::length(input.movement) > 1.0f) input.movement = glm::normalize(input.movement);
                const bool jump = glfwGetKey(host.window, GLFW_KEY_SPACE) == GLFW_PRESS;
                input.jump = jump && !jumpWasDown;
                jumpWasDown = jump;
            }
            previousX = x; previousY = y;
            const auto now = std::chrono::steady_clock::now();
            runtime.update(std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0f, 0.25f), input);
            previous = now;
            int width = 0, height = 0;
            glfwGetFramebufferSize(host.window, &width, &height);
            if (width <= 0 || height <= 0) { glfwWaitEventsTimeout(0.01); continue; }
            host.renderer.setViewportSize(static_cast<unsigned>(width), static_cast<unsigned>(height));
            if (!host.renderer.beginFrame()) continue;
            // The shared GLFW backend queues ImGui input events. Consume them
            // in the player too, even though it draws no editor panels.
            host.renderer.beginGuiFrame();
            ImGui::NewFrame();
            ImGui::EndFrame();
            const auto camera = sceneCamera(runtime.scene(), static_cast<float>(width) / height);
            host.audio.update(runtime.scene(), project.root, runtime.state(), camera.position, -glm::vec3(glm::inverse(camera.view)[2]));
            host.renderer.render(runtime.scene(), camera, project.root, InvalidEntity, nullptr);
            ++frames;
            if (options.frames && frames >= options.frames) break;
        }
        host.renderer.waitIdle();
        if (!options.screenshot.empty() && frames) host.renderer.captureScreenshot(options.screenshot);
        if (!runtime.errors().empty()) throw std::runtime_error(runtime.errors().front());
        std::cout << "Runtime closed cleanly. Frames=" << frames << " SimulationSteps=" << runtime.simulationSteps() << '\n';
        runtime.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MythbreakerRuntime: " << error.what() << '\n';
        return 1;
    }
}
