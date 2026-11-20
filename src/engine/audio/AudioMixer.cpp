#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include "AudioMixer.h"
#include "engine/runtime/Project.h"
#include <algorithm>
#include <map>
#include <set>

namespace myth::engine {
struct AudioMixer::Impl {
    struct Voice {
        ma_sound sound{};
        bool initialized = false;
        bool started = false;
        bool paused = false;
        std::string source;
        ~Voice() { if (initialized) ma_sound_uninit(&sound); }
    };
    ma_engine engine{};
    bool initialized = false;
    bool silent = false;
    RuntimeState previousState = RuntimeState::Edit;
    std::filesystem::path projectRoot;
    std::map<EntityId, std::unique_ptr<Voice>> voices;
    std::map<EntityId, std::string> failedSources;
    std::set<EntityId> stopped;
    std::string error;

    void reset() { voices.clear(); failedSources.clear(); stopped.clear(); }

    Voice* ensure(const SceneEntity& entity, const std::filesystem::path& root) {
        if (!initialized || !entity.audio || entity.audio->source.empty()) return nullptr;
        const auto& source = entity.audio->source;
        auto current = voices.find(entity.id);
        if (current != voices.end() && current->second->source == source) return current->second.get();
        voices.erase(entity.id);
        auto failed = failedSources.find(entity.id);
        if (failed != failedSources.end() && failed->second == source) return nullptr;
        try {
            Project project;
            project.root = root;
            const auto path = project.resolve(source);
            auto voice = std::make_unique<Voice>();
            const auto result = ma_sound_init_from_file(&engine, path.string().c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &voice->sound);
            if (result != MA_SUCCESS) throw std::runtime_error("Could not decode audio " + source + ": " + ma_result_description(result));
            voice->initialized = true;
            voice->source = source;
            auto* pointer = voice.get();
            voices.emplace(entity.id, std::move(voice));
            failedSources.erase(entity.id);
            return pointer;
        } catch (const std::exception& exception) {
            error = exception.what();
            failedSources[entity.id] = source;
            return nullptr;
        }
    }
};

AudioMixer::AudioMixer() : m_impl(std::make_unique<Impl>()) {}
AudioMixer::~AudioMixer() { shutdown(); }
bool AudioMixer::initialize(bool silentDevice) {
    if (m_impl->initialized) return true;
    auto config = ma_engine_config_init();
    config.listenerCount = 1;
    config.noDevice = silentDevice ? MA_TRUE : MA_FALSE;
    if (silentDevice) { config.channels = 2; config.sampleRate = 48000; }
    const auto result = ma_engine_init(&config, &m_impl->engine);
    if (result != MA_SUCCESS) {
        m_impl->error = std::string("Audio device unavailable: ") + ma_result_description(result);
        return false;
    }
    m_impl->silent = silentDevice;
    m_impl->initialized = true;
    return true;
}
void AudioMixer::shutdown() noexcept {
    if (!m_impl) return;
    m_impl->voices.clear();
    m_impl->failedSources.clear();
    if (m_impl->initialized) ma_engine_uninit(&m_impl->engine);
    m_impl->initialized = false;
}
void AudioMixer::stop(EntityId id) {
    m_impl->stopped.insert(id);
    auto it = m_impl->voices.find(id);
    if (it != m_impl->voices.end()) {
        ma_sound_stop(&it->second->sound);
        it->second->started = true;
        it->second->paused = false;
    }
}
void AudioMixer::stopAll() {
    for (const auto& [id, voice] : m_impl->voices) { (void)voice; stop(id); }
}
const std::string& AudioMixer::lastError() const { return m_impl->error; }
bool AudioMixer::isPlaying(EntityId id) const {
    const auto it = m_impl->voices.find(id);
    return it != m_impl->voices.end() && ma_sound_is_playing(&it->second->sound);
}
void AudioMixer::play(const Scene& scene, EntityId id, const std::filesystem::path& projectRoot) {
    const auto* entity = scene.find(id);
    if (!entity) return;
    m_impl->stopped.erase(id);
    m_impl->failedSources.erase(id); // Explicit play retries an asset repaired on disk.
    if (auto* voice = m_impl->ensure(*entity, projectRoot)) {
        ma_sound_seek_to_pcm_frame(&voice->sound, 0);
        ma_sound_start(&voice->sound);
        voice->started = true;
        voice->paused = false;
    }
}
void AudioMixer::update(const Scene& scene, const std::filesystem::path& projectRoot, RuntimeState state,
                        const glm::vec3& listenerPosition, const glm::vec3& listenerForward) {
    if (!m_impl->initialized) return;
    if (state == RuntimeState::Edit || projectRoot != m_impl->projectRoot || m_impl->previousState == RuntimeState::Edit) {
        m_impl->reset();
        m_impl->projectRoot = projectRoot;
    }
    m_impl->previousState = state;
    if (state == RuntimeState::Edit) return;
    ma_engine_listener_set_position(&m_impl->engine, 0, listenerPosition.x, listenerPosition.y, listenerPosition.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, listenerForward.x, listenerForward.y, listenerForward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, 0, 1, 0);
    std::set<EntityId> alive;
    for (const auto& entity : scene.entities()) {
        if (!entity.audio || entity.audio->source.empty() || !scene.isEnabled(entity.id)) continue;
        alive.insert(entity.id);
        auto* voice = m_impl->ensure(entity, projectRoot);
        if (!voice) continue;
        const auto& audio = *entity.audio;
        const glm::vec3 position(scene.worldMatrix(entity.id)[3]);
        ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
        ma_sound_set_volume(&voice->sound, std::clamp(audio.volume, 0.0f, 1.0f));
        ma_sound_set_looping(&voice->sound, audio.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(&voice->sound, audio.spatial ? MA_TRUE : MA_FALSE);
        ma_sound_set_min_distance(&voice->sound, 1.0f);
        ma_sound_set_max_distance(&voice->sound, 100.0f);
        if (state == RuntimeState::Paused) {
            if (ma_sound_is_playing(&voice->sound)) { ma_sound_stop(&voice->sound); voice->paused = true; }
        } else if (!m_impl->stopped.contains(entity.id) && (voice->paused || (!voice->started && audio.playOnStart))) {
            ma_sound_start(&voice->sound);
            voice->started = true;
            voice->paused = false;
        }
    }
    for (auto it = m_impl->voices.begin(); it != m_impl->voices.end();) {
        if (!alive.contains(it->first)) it = m_impl->voices.erase(it); else ++it;
    }
    if (m_impl->silent) {
        float samples[1600];
        ma_engine_read_pcm_frames(&m_impl->engine, samples, 800, nullptr);
    }
}
}
