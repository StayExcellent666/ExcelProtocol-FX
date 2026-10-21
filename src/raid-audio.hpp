#pragma once

#include <QString>
#include <memory>

namespace fx {

class RaidAudio {
public:
    RaidAudio();
    ~RaidAudio();
    RaidAudio(const RaidAudio &) = delete;
    RaidAudio &operator=(const RaidAudio &) = delete;

    void setEnabled(bool enabled);
    void setVolume(int percent);
    void play(const QString &path);
    void cleanup();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fx
