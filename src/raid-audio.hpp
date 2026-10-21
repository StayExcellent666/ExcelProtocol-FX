#pragma once

#include <QString>
#include <memory>

typedef struct obs_source obs_source_t;

namespace fx {

class RaidAudio {
public:
    RaidAudio();
    ~RaidAudio();
    RaidAudio(const RaidAudio &) = delete;
    RaidAudio &operator=(const RaidAudio &) = delete;

    void setEnabled(bool enabled);
    void setVolume(int percent);
    void addOutputSource(obs_source_t *source);
    void removeOutputSource(obs_source_t *source);
    void play(const QString &path, const QString &group, double gain, int priority);
    void cleanup();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fx
