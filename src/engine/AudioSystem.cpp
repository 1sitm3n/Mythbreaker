#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio.h"
#include "AudioSystem.h"
#include "Logger.h"
#include <filesystem>

namespace myth {

AudioSystem& AudioSystem::instance() {
    static AudioSystem inst;
    return inst;
}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::init() {
    if (m_initialized) return true;
    
    m_engine = new ma_engine();
    
    ma_engine_config config = ma_engine_config_init();
    config.listenerCount = 1;
    
    ma_result result = ma_engine_init(&config, m_engine);
    if (result != MA_SUCCESS) {
        Logger::error("Failed to initialize audio engine: " + std::to_string(result));
        delete m_engine;
        m_engine = nullptr;
        return false;
    }
    
    m_initialized = true;
    Logger::info("Audio system initialized");
    return true;
}

void AudioSystem::shutdown() {
    if (!m_initialized) return;
    
    if (m_currentMusic) {
        ma_sound_stop(m_currentMusic);
        ma_sound_uninit(m_currentMusic);
        delete m_currentMusic;
        m_currentMusic = nullptr;
    }
    
    if (m_engine) {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }
    
    m_soundPaths.clear();
    m_initialized = false;
    Logger::info("Audio system shutdown");
}

bool AudioSystem::loadSound(const std::string& name, const std::string& filepath) {
    if (!m_initialized) return false;
    
    if (!std::filesystem::exists(filepath)) {
        Logger::warn("Sound file not found: " + filepath);
        return false;
    }
    
    m_soundPaths[name] = filepath;
    Logger::info("Registered sound: " + name);
    return true;
}

void AudioSystem::playSound(const std::string& name, float volume) {
    if (!m_initialized) return;
    
    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) {
        Logger::warn("Sound not found: " + name);
        return;
    }
    
    ma_result result = ma_engine_play_sound(m_engine, it->second.c_str(), nullptr);
    if (result == MA_SUCCESS) {
        Logger::info("Playing sound: " + name);
    } else {
        Logger::warn("Failed to play sound: " + name + " (error: " + std::to_string(result) + ")");
    }
}

void AudioSystem::playSoundAtPosition(const std::string& name, const glm::vec3& position, float volume) {
    if (!m_initialized) return;
    
    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) return;
    
    // For positional audio, we need to create a sound with spatialization
    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(m_engine, it->second.c_str(), 
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr, sound);
    
    if (result == MA_SUCCESS) {
        ma_sound_set_position(sound, position.x, position.y, position.z);
        ma_sound_set_volume(sound, volume * m_sfxVolume * m_masterVolume);
        ma_sound_set_spatialization_enabled(sound, MA_TRUE);
        ma_sound_set_attenuation_model(sound, ma_attenuation_model_linear);
        ma_sound_set_min_distance(sound, 1.0f);
        ma_sound_set_max_distance(sound, 50.0f);
        ma_sound_start(sound);
        // Note: In a real implementation, we'd track these and clean them up
        // For now, miniaudio handles cleanup when sound finishes
    } else {
        delete sound;
    }
}

void AudioSystem::playMusic(const std::string& name, float volume, bool loop) {
    if (!m_initialized) return;
    
    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) return;
    
    // Stop current music
    stopMusic();
    
    m_currentMusic = new ma_sound();
    ma_result result = ma_sound_init_from_file(m_engine, it->second.c_str(),
        MA_SOUND_FLAG_STREAM, nullptr, nullptr, m_currentMusic);
    
    if (result == MA_SUCCESS) {
        ma_sound_set_looping(m_currentMusic, loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_volume(m_currentMusic, volume * m_musicVolume * m_masterVolume);
        ma_sound_start(m_currentMusic);
        m_currentMusicName = name;
        Logger::info("Playing music: " + name);
    } else {
        Logger::error("Failed to play music: " + name);
        delete m_currentMusic;
        m_currentMusic = nullptr;
    }
}

void AudioSystem::stopMusic() {
    if (m_currentMusic) {
        ma_sound_stop(m_currentMusic);
        ma_sound_uninit(m_currentMusic);
        delete m_currentMusic;
        m_currentMusic = nullptr;
        m_currentMusicName.clear();
    }
}

void AudioSystem::setMasterVolume(float volume) {
    m_masterVolume = std::max(0.0f, std::min(1.0f, volume));
    if (m_engine) {
        ma_engine_set_volume(m_engine, m_masterVolume);
    }
}

void AudioSystem::setMusicVolume(float volume) {
    m_musicVolume = std::max(0.0f, std::min(1.0f, volume));
    if (m_currentMusic) {
        ma_sound_set_volume(m_currentMusic, m_musicVolume * m_masterVolume);
    }
}

void AudioSystem::setSFXVolume(float volume) {
    m_sfxVolume = std::max(0.0f, std::min(1.0f, volume));
}

void AudioSystem::setListenerPosition(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_engine) return;
    
    ma_engine_listener_set_position(m_engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(m_engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(m_engine, 0, up.x, up.y, up.z);
}

void AudioSystem::update() {
    // miniaudio handles most updates internally
    // This is here for future expansion (e.g., managing sound pools)
}

} // namespace myth


