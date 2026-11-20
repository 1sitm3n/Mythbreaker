#pragma once
#include "Paths.h"
#include "engine/audio/AudioMixer.h"
#include "engine/rendering/SceneRenderer.h"
#include <GLFW/glfw3.h>
#include <charconv>
#include <iostream>
#include <stdexcept>

namespace myth::engine {
struct DesktopOptions {
    std::filesystem::path project, shaders, screenshot;
    unsigned frames = 0;
    int width = 1440, height = 900;
    bool help = false, noAudio = false, audioNull = false, play = false;
};

inline DesktopOptions parseDesktopOptions(int argc, char** argv) {
    DesktopOptions result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value for " + argument);
            return argv[i];
        };
        auto number = [&]() -> unsigned {
            const auto text = value();
            unsigned n = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
            if (error != std::errc{} || end != text.data() + text.size()) throw std::runtime_error("Invalid number for " + argument);
            return n;
        };
        if (argument == "--project") result.project = value();
        else if (argument == "--shaders") result.shaders = value();
        else if (argument == "--screenshot") result.screenshot = value();
        else if (argument == "--frames") result.frames = number();
        else if (argument == "--width") result.width = static_cast<int>(number());
        else if (argument == "--height") result.height = static_cast<int>(number());
        else if (argument == "--no-audio") result.noAudio = true;
        else if (argument == "--audio-null") result.audioNull = true;
        else if (argument == "--play") result.play = true;
        else if (argument == "--help" || argument == "-h") result.help = true;
        else throw std::runtime_error("Unknown argument: " + argument);
    }
    if (result.width < 320 || result.height < 240 || result.width > 7680 || result.height > 4320)
        throw std::runtime_error("Window size must be between 320x240 and 7680x4320.");
    return result;
}

inline void desktopUsage(const char* application) {
    std::cout << application << " [--project <directory-or-manifest>] [--shaders <directory>]\n"
              << "  --width <pixels> --height <pixels>\n"
              << "  --frames <count>       Exit automatically after rendered frames (smoke testing).\n"
              << "  --screenshot <file>    Capture the final scene viewport as RGB PPM.\n"
              << "  --no-audio             Disable audio initialization.\n"
              << "  --audio-null           Decode/mix audio without a physical audio device.\n"
              << "  --play                 Start the editor in play mode.\n";
}

class DesktopHost {
public:
    SceneRenderer renderer;
    AudioMixer audio;
    GLFWwindow* window = nullptr;
    bool audioAvailable = false;

    DesktopHost(const std::string& title, const DesktopOptions& options, const std::filesystem::path& executableDir) {
        try {
            glfwSetErrorCallback([](int code, const char* description) {
                std::cerr << "GLFW " << code << ": " << description << '\n';
            });
            if (!glfwInit()) throw std::runtime_error("Could not initialize a desktop display. Use MythbreakerCLI for headless projects/tests.");
            m_glfwInitialized = true;
            if (!glfwVulkanSupported()) throw std::runtime_error("Vulkan is unavailable. Install a Vulkan-capable graphics driver.");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            window = glfwCreateWindow(options.width, options.height, title.c_str(), nullptr, nullptr);
            if (!window) throw std::runtime_error("Could not create the application window.");
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            m_imguiInitialized = true;
            ImGui::GetIO().IniFilename = nullptr;
            renderer.initialize(window, findShaderDirectory(executableDir, options.shaders));
            if (!options.noAudio) audioAvailable = audio.initialize(options.audioNull);
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~DesktopHost() { cleanup(); }
    DesktopHost(const DesktopHost&) = delete;
    DesktopHost& operator=(const DesktopHost&) = delete;
private:
    void cleanup() noexcept {
        audio.shutdown();
        renderer.shutdown();
        if (m_imguiInitialized) { ImGui::DestroyContext(); m_imguiInitialized = false; }
        if (window) { glfwDestroyWindow(window); window = nullptr; }
        if (m_glfwInitialized) { glfwTerminate(); m_glfwInitialized = false; }
    }
    bool m_glfwInitialized = false;
    bool m_imguiInitialized = false;
};
}
