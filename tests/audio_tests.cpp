#include "engine/audio/AudioMixer.h"
#include "engine/runtime/Project.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace myth::engine;
namespace fs = std::filesystem;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void makeWave(const fs::path& path) {
    std::ofstream file(path, std::ios::binary);
    auto word = [&](unsigned value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    constexpr unsigned samples = 48000;
    file.write("RIFF", 4); word(36 + samples * 2, 4); file.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(1, 2); word(48000, 4); word(96000, 4); word(2, 2); word(16, 2);
    file.write("data", 4); word(samples * 2, 4);
    for (unsigned i = 0; i < samples; ++i) {
        const auto sample = static_cast<short>(std::sin(i * 440.0 * 6.283185307 / 48000.0) * 1000);
        word(static_cast<unsigned short>(sample), 2);
    }
    require(static_cast<bool>(file), "Could not create WAV fixture");
}

int main() {
    const auto root = fs::temp_directory_path() / ("mythbreaker-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto project = Project::create(root, "Audio lifecycle test");
        makeWave(root / "Assets/tone.wav");
        Scene scene;
        const auto id = scene.createEntity("Tone").id;
        scene.find(id)->audio = AudioSourceComponent{"Assets/tone.wav", 0.25f, true, true, true};
        AudioMixer audio;
        require(audio.initialize(true), "Silent audio mixer could not initialize");
        auto update = [&](RuntimeState state) { audio.update(scene, root, state, {0,0,0}, {0,0,-1}); };
        update(RuntimeState::Edit);
        require(!audio.isPlaying(id), "Authoring must be silent");
        update(RuntimeState::Playing);
        require(audio.isPlaying(id), "playOnStart must decode and start WAV");
        update(RuntimeState::Paused);
        require(!audio.isPlaying(id), "Pause must stop advancing voices");
        update(RuntimeState::Playing);
        require(audio.isPlaying(id), "Resume must continue paused voices");
        audio.stop(id); update(RuntimeState::Playing);
        require(!audio.isPlaying(id), "Explicit stop must survive subsequent updates");
        audio.play(scene, id, root);
        require(audio.isPlaying(id), "Explicit play must restart a stopped source");
        scene.find(id)->audio->source.clear(); update(RuntimeState::Playing);
        require(!audio.isPlaying(id), "Clearing a component path must remove its old voice");
        scene.find(id)->audio->source = "Assets/missing.wav"; update(RuntimeState::Playing);
        require(!audio.lastError().empty(), "Missing audio must produce a useful error");
        makeWave(root / "Assets/missing.wav");
        audio.play(scene, id, root);
        require(audio.isPlaying(id), "Explicit play must retry a repaired asset");
        update(RuntimeState::Edit);
        require(!audio.isPlaying(id), "Stop play mode must discard all runtime voices");
        audio.shutdown(); audio.shutdown();
        fs::remove_all(root);
        std::cout << "PASS: WAV decode/mix, play, pause, resume, explicit stop, source clear, missing asset recovery and shutdown\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored; fs::remove_all(root, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
