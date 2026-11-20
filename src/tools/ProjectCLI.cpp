#include "engine/runtime/Project.h"
#include "engine/runtime/Runtime.h"
#include "engine/runtime/GameModule.h"
#include "engine/platform/Package.h"
#include "engine/platform/Paths.h"
#include <charconv>
#include <iostream>
#include <stdexcept>

using namespace myth::engine;
namespace fs = std::filesystem;

static void usage() {
    std::cout << "Mythbreaker Engine 0.2\n"
              << "  MythbreakerCLI create <directory> [project-name]\n"
              << "  MythbreakerCLI validate <project>\n"
              << "  MythbreakerCLI simulate <project> [fixed-steps]\n"
              << "  MythbreakerCLI import <project> <asset-file>\n"
              << "  MythbreakerCLI export <project> <directory>\n"
              << "  MythbreakerCLI package <project> <directory>\n"
              << "Projects may be a directory or a Mythbreaker.project.json manifest.\n";
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") { usage(); return 0; }
        const std::string command = argv[1];
        if (argc < 3) throw std::runtime_error("Missing project path. Use --help for usage.");
        if (command == "create") {
            const auto project = Project::create(argv[2], argc > 3 ? argv[3] : "Mythbreaker Project");
            std::cout << "Created " << project.manifestPath.string() << '\n';
            return 0;
        }
        const auto project = Project::load(argv[2]);
        if (command == "validate") {
            const auto scene = project.loadStartScene();
            project.validateAssets(scene);
            std::cout << "Valid project: " << project.config.name << " | " << scene.entities().size() << " entities | " << project.listAssets().size() << " assets\n";
        } else if (command == "simulate") {
            unsigned steps = 600;
            if (argc > 3) {
                const std::string arg = argv[3];
                auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), steps);
                if (error != std::errc{} || end != arg.data() + arg.size() || steps > 1000000)
                    throw std::runtime_error("Fixed step count must be an integer from 0 to 1000000.");
            }
            Runtime runtime(project.loadStartScene());
            registerGameBehaviours(runtime.behaviours());
            runtime.play();
            for (unsigned i = 0; i < steps; ++i) runtime.update(Runtime::FixedStep);
            if (!runtime.errors().empty()) throw std::runtime_error(runtime.errors().front());
            runtime.scene().validate();
            std::cout << "Simulated " << runtime.simulationSteps() << " fixed steps / " << runtime.simulationTime() << " seconds / " << runtime.scene().entities().size() << " entities\n";
            runtime.stop();
        } else if (command == "import") {
            if (argc < 4) throw std::runtime_error("Missing asset source file.");
            std::cout << "Imported " << project.importAsset(argv[3]) << '\n';
        } else if (command == "export" || command == "package") {
            if (argc < 4) throw std::runtime_error("Missing destination directory.");
            if (command == "package") packageProject(project, argv[3], executableDirectory(argv[0]));
            else project.exportTo(argv[3]);
            std::cout << "Exported " << fs::absolute(argv[3]).string() << '\n';
        } else {
            throw std::runtime_error("Unknown command: " + command);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mythbreaker: " << error.what() << '\n';
        return 1;
    }
}
