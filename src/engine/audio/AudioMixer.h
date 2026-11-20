#pragma once
#include "engine/runtime/Runtime.h"
#include <filesystem>
#include <memory>
#include <string>

namespace myth::engine {
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;
    bool initialize(bool silentDevice = false);
    void shutdown() noexcept;
    void update(const Scene&, const std::filesystem::path& projectRoot, RuntimeState,
                const glm::vec3& listenerPosition, const glm::vec3& listenerForward);
    void play(const Scene&, EntityId, const std::filesystem::path& projectRoot);
    void stop(EntityId);
    void stopAll();
    bool isPlaying(EntityId) const;
    const std::string& lastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
