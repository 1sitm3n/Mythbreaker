#include "engine/rendering/MeshAsset.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using namespace myth::engine::detail;
using Json = nlohmann::json;

namespace {
int assertions = 0;
void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.0001f; }
template <class Callable> void rejects(Callable&& callable, const char* message) {
    bool threw = false;
    try { callable(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}

struct TemporaryProject {
    fs::path root;
    TemporaryProject() {
        root = fs::temp_directory_path() / ("mythbreaker-import-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "project");
    }
    ~TemporaryProject() { std::error_code ignored; fs::remove_all(root, ignored); }
};

struct Fixture {
    std::vector<unsigned char> bytes;
    Json json;
    Fixture() {
        // Interleaved POSITION + NORMAL stream; indices are a second view.
        const float vertices[] = {0,0,0, 0,0,1, 1,0,0, 0,0,1, 0,1,0, 0,0,1};
        const std::uint16_t indices[] = {0,1,2};
        bytes.resize(sizeof(vertices) + sizeof(indices));
        std::memcpy(bytes.data(), vertices, sizeof(vertices));
        std::memcpy(bytes.data() + sizeof(vertices), indices, sizeof(indices));
        json = {
            {"asset", {{"version", "2.0"}}},
            {"buffers", {{{"uri", "mesh.bin"}, {"byteLength", bytes.size()}}}},
            {"bufferViews", {
                {{"buffer",0},{"byteOffset",0},{"byteLength",sizeof(vertices)},{"byteStride",24}},
                {{"buffer",0},{"byteOffset",sizeof(vertices)},{"byteLength",sizeof(indices)}}}},
            {"accessors", {
                {{"bufferView",0},{"byteOffset",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{1,1,0}}},
                {{"bufferView",0},{"byteOffset",12},{"componentType",5126},{"count",3},{"type","VEC3"}},
                {{"bufferView",1},{"componentType",5123},{"count",3},{"type","SCALAR"}}}},
            {"meshes", {{{"primitives", {{{"attributes",{{"POSITION",0},{"NORMAL",1}}},{"indices",2},{"mode",4}}}}}}},
            {"nodes", {{{"mesh",0},{"translation",{3,4,5}},{"scale",{2,2,2}}}}},
            {"scenes", {{{"nodes",{0}}}}}, {"scene",0}
        };
    }
    fs::path save(const fs::path& root) {
        json["buffers"][0]["byteLength"] = bytes.size();
        std::ofstream(root / "mesh.bin", std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        std::ofstream(root / "mesh.gltf") << json.dump(2);
        return root / "mesh.gltf";
    }
};

MeshAsset skinFixture() {
    MeshAsset asset;
    asset.nodes.resize(3);
    asset.nodes[1].parent = 0;
    asset.nodes[2].parent = 0; asset.nodes[2].translation = glm::vec3(0,1,0);
    MeshPart part;
    part.nodeIndex = 0;
    part.localVertices.resize(3);
    part.localVertices[0].position = glm::vec3(0,0,0);
    part.localVertices[1].position = glm::vec3(1,0,0);
    part.localVertices[2].position = glm::vec3(0,1,0);
    part.vertices = part.localVertices;
    part.indices = {0,1,2};
    part.joints = {1,2};
    part.inverseBindMatrices = {glm::mat4(1),glm::translate(glm::mat4(1),glm::vec3(0,-1,0))};
    part.jointIndices = {glm::ivec4(0),glm::ivec4(0,1,0,0),glm::ivec4(1,0,0,0)};
    part.jointWeights = {glm::vec4(1,0,0,0),glm::vec4(.5f,.5f,0,0),glm::vec4(1,0,0,0)};
    asset.parts.push_back(part);
    AnimationClip clip;
    clip.name = "Move joint"; clip.duration = 1;
    AnimationChannel channel;
    channel.node = 1; channel.times = {0,1}; channel.values = {glm::vec4(0),glm::vec4(2,0,0,0)};
    clip.channels.push_back(channel); asset.animations.push_back(clip);
    return asset;
}
}

int main(int argc, char** argv) {
    try {
        for (const auto* name : {"builtin:cube", "builtin:plane", "builtin:sphere"}) {
            const auto mesh = builtinMesh(name);
            require(mesh.parts.size() == 1, "Builtins must contain one part");
            require(!mesh.parts[0].indices.empty() && mesh.parts[0].indices.size() % 3 == 0, "Builtin triangles are incomplete");
            for (const auto index : mesh.parts[0].indices) require(index < mesh.parts[0].vertices.size(), "Builtin triangle index is invalid");
        }
        const auto cube = builtinMesh("builtin:cube");
        require(close(cube.minimum.x, -.5f) && close(cube.maximum.z, .5f), "Cube bounds changed");
        rejects([] { builtinMesh("builtin:missing"); }, "Unknown builtin must be rejected");

        TemporaryProject temporary;
        const auto project = temporary.root / "project";
        Fixture fixture;
        auto path = fixture.save(project);
        auto mesh = loadMesh(path, project);
        require(mesh.parts.size() == 1 && mesh.parts[0].indices.size() == 3, "glTF triangle failed to import");
        require(close(mesh.minimum.x, 3) && close(mesh.minimum.y, 4) && close(mesh.minimum.z, 5), "Node translation was not applied");
        require(close(mesh.maximum.x, 5) && close(mesh.maximum.y, 6), "Node scale was not applied");
        require(close(mesh.parts[0].vertices[1].normal.z, 1), "Interleaved normal accessor was misread");

        // A sparse replacement changes vertex #1 from x=1 to x=2 before the node transform.
        fixture.bytes.resize(80, 0);
        fixture.bytes[78] = 1;
        const float replacement[] = {2,0,0};
        fixture.bytes.resize(92);
        std::memcpy(fixture.bytes.data() + 80, replacement, sizeof(replacement));
        fixture.json["bufferViews"].push_back({{"buffer",0},{"byteOffset",78},{"byteLength",1}});
        fixture.json["bufferViews"].push_back({{"buffer",0},{"byteOffset",80},{"byteLength",12}});
        fixture.json["accessors"][0]["sparse"] = {{"count",1},{"indices",{{"bufferView",2},{"componentType",5121}}},{"values",{{"bufferView",3}}}};
        mesh = loadMesh(fixture.save(project), project);
        require(close(mesh.maximum.x, 7), "Sparse accessor replacement was ignored");

        fixture = Fixture{};
        const std::uint16_t invalidIndex = 99;
        std::memcpy(fixture.bytes.data() + 72, &invalidIndex, sizeof(invalidIndex));
        path = fixture.save(project);
        rejects([&] { loadMesh(path, project); }, "Out-of-range triangle indices must be rejected");

        fixture = Fixture{};
        const float invalidNormal = std::numeric_limits<float>::infinity();
        std::memcpy(fixture.bytes.data() + 12, &invalidNormal, sizeof(invalidNormal));
        path = fixture.save(project);
        rejects([&] { loadMesh(path, project); }, "Non-finite mesh attributes must be rejected");

        fixture = Fixture{};
        path = fixture.save(project);
        fs::copy_file(project / "mesh.bin", temporary.root / "outside.bin");
        fixture.json["buffers"][0]["uri"] = "../outside.bin";
        path = fixture.save(project);
        rejects([&] { loadMesh(path, project); }, "External glTF dependencies must stay inside the project");
        require(!insideRoot(temporary.root / "outside.bin", project), "Project containment accepted a sibling file");

        // Skinning evaluates the full hierarchy, applies inverse bind matrices,
        // and retains independent result storage for independently timed instances.
        auto animated = skinFixture();
        auto first = evaluateAnimation(animated,0,0,false);
        auto second = evaluateAnimation(animated,0,.5f,false);
        require(close(first.vertices[0].position.x,0), "First instance pose was overwritten");
        require(close(second.vertices[0].position.x,1), "Linear joint translation was not evaluated");
        require(close(second.vertices[1].position.x,1.5f), "Weighted two-joint skinning is incorrect");
        require(close(second.vertices[2].position.y,1), "Inverse bind transform was not applied");
        require(close(evaluateAnimation(animated,0,1.5f,true).vertices[0].position.x,1), "Looping animation did not wrap time");
        require(close(evaluateAnimation(animated,0,2,false).vertices[0].position.x,2), "Non-looping animation did not hold its final pose");
        animated.animations[0].channels[0].interpolation = Interpolation::Step;
        require(close(evaluateAnimation(animated,0,.75f,false).vertices[0].position.x,0), "STEP interpolation blended between keys");
        require(close(evaluateAnimation(animated,0,1,false).vertices[0].position.x,2), "STEP interpolation lost its final key");

        auto& cubic = animated.animations[0].channels[0];
        cubic.interpolation = Interpolation::CubicSpline;
        cubic.times = {0,2}; animated.animations[0].duration = 2;
        cubic.values = {glm::vec4(0),glm::vec4(0),glm::vec4(4,0,0,0),glm::vec4(0),glm::vec4(2,0,0,0),glm::vec4(0)};
        require(close(evaluateAnimation(animated,0,1,false).vertices[0].position.x,2), "Cubic spline tangents were not scaled by the key interval");

        animated = skinFixture();
        animated.parts[0].jointWeights[1] = glm::vec4(1,0,0,0);
        auto& rotation = animated.animations[0].channels[0];
        rotation.path = AnimationPath::Rotation;
        rotation.values = {glm::vec4(0,0,0,1),glm::vec4(0,0,1,0)};
        auto rotated = evaluateAnimation(animated,0,.5f,false);
        require(close(rotated.vertices[1].position.x,0) && close(rotated.vertices[1].position.y,1), "Quaternion LINEAR interpolation did not use slerp");
        rotation.interpolation = Interpolation::CubicSpline;
        rotation.values = {glm::vec4(0),glm::vec4(0,0,0,1),glm::vec4(0),glm::vec4(0),glm::vec4(0,0,1,0),glm::vec4(0)};
        rotated = evaluateAnimation(animated,0,.5f,false);
        require(close(rotated.vertices[1].position.x,0) && close(rotated.vertices[1].position.y,1), "Cubic quaternion results were not normalized");
        rejects([&] { evaluateAnimation(animated,7,0,true); }, "Invalid clip index was accepted");
        animated.nodes[0].parent = 2;
        rejects([&] { evaluateAnimation(animated,0,0,true); }, "Cyclic animated hierarchy was accepted");

        if (argc > 1) {
            const fs::path sourceRoot = argv[1];
            const auto duck = loadMesh(sourceRoot / "assets/models/Duck.glb", sourceRoot);
            require(!duck.parts.empty() && duck.parts.front().albedo != nullptr, "Embedded GLB albedo texture was lost");
            require(duck.maximum.x - duck.minimum.x < 2.0f, "GLB node scale was not baked into imported vertices");
            const auto fox = loadMesh(sourceRoot / "assets/models/Fox.glb", sourceRoot);
            require(fox.animations.size() == 3, "Fox animation clips were not imported");
            const auto rest = evaluateAnimation(fox,0,0,true), motion = evaluateAnimation(fox,0,.4f,true);
            require(rest.vertices.size() == motion.vertices.size() && !rest.vertices.empty(), "Fox skinning lost vertices");
            bool moved = false;
            for (std::size_t i = 0; i < rest.vertices.size(); ++i) if (glm::length(rest.vertices[i].position - motion.vertices[i].position) > .001f) { moved = true; break; }
            require(moved, "Imported Fox animation produced no motion");
        }
        std::cout << "Asset import tests passed: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asset import test failed: " << error.what() << '\n';
        return 1;
    }
}
