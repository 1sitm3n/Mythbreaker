#pragma once
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

// Forward declare miniaudio types
struct ma_engine;
struct ma_sound;

namespace myth {

class AudioSystem {
public:
    static AudioSystem& instance();
    
    bool init();
    void shutdown();
    
    // Sound loading
    bool loadSound(const std::string& name, const std::string& filepath);
    
    // Playback
    void playSound(const std::string& name, float volume = 1.0f);
    void playSoundAtPosition(const std::string& name, const glm::vec3& position, float volume = 1.0f);
    void playMusic(const std::string& name, float volume = 0.5f, bool loop = true);
    void stopMusic();
    
    // Control
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setSFXVolume(float volume);
    
    // Listener (camera) position for 3D audio
    void setListenerPosition(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    
    // Update (call each frame for 3D audio)
    void update();
    
private:
    AudioSystem() = default;
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    
    ma_engine* m_engine = nullptr;
    std::unordered_map<std::string, std::string> m_soundPaths;
    
    ma_sound* m_currentMusic = nullptr;
    std::string m_currentMusicName;
    
    float m_masterVolume = 1.0f;
    float m_musicVolume = 0.5f;
    float m_sfxVolume = 1.0f;
    
    bool m_initialized = false;
};

} // namespace myth
