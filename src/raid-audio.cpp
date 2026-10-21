#include "raid-audio.hpp"

#include <QByteArray>
#include <QFile>
#include <obs.h>
#include <util/platform.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fx {
namespace {
quint16 little16(const char *value)
{
    return static_cast<quint16>(static_cast<unsigned char>(value[0]))
        | static_cast<quint16>(static_cast<unsigned char>(value[1]) << 8);
}

quint32 little32(const char *value)
{
    return static_cast<quint32>(static_cast<unsigned char>(value[0]))
        | (static_cast<quint32>(static_cast<unsigned char>(value[1])) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(value[2])) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(value[3])) << 24);
}

struct WaveData {
    QByteArray samples;
    quint16 channels = 0;
    quint32 samplesPerSecond = 0;
};

bool loadWave(const QString &path, WaveData &result)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || std::memcmp(bytes.constData(), "RIFF", 4) != 0
        || std::memcmp(bytes.constData() + 8, "WAVE", 4) != 0) return false;
    QByteArray format;
    QByteArray samples;
    qsizetype offset = 12;
    while (offset + 8 <= bytes.size()) {
        const char *chunk = bytes.constData() + offset;
        const quint32 size = little32(chunk + 4);
        const qsizetype dataOffset = offset + 8;
        if (size > static_cast<quint32>(bytes.size() - dataOffset)) return false;
        if (std::memcmp(chunk, "fmt ", 4) == 0) format = bytes.mid(dataOffset, static_cast<qsizetype>(size));
        else if (std::memcmp(chunk, "data", 4) == 0) samples = bytes.mid(dataOffset, static_cast<qsizetype>(size));
        offset = dataOffset + static_cast<qsizetype>(size) + (size & 1u);
    }
    if (format.size() < 16 || samples.isEmpty()) return false;
    const quint16 formatTag = little16(format.constData());
    result.channels = little16(format.constData() + 2);
    result.samplesPerSecond = little32(format.constData() + 4);
    const quint16 blockAlign = little16(format.constData() + 12);
    const quint16 bitsPerSample = little16(format.constData() + 14);
    if (formatTag != 1 || bitsPerSample != 16 || result.samplesPerSecond != 44100
        || (result.channels != 1 && result.channels != 2)
        || blockAlign != result.channels * sizeof(qint16)) return false;
    result.samples = samples;
    return result.samples.size() % blockAlign == 0;
}
}

struct RaidAudio::Impl {
    static constexpr uint32_t sampleRate = 44100;
    static constexpr uint32_t framesPerBlock = 441;

    struct Voice {
        QByteArray samples;
        quint16 channels = 0;
        qsizetype cursorFrames = 0;
        QString group;
        double gain = 1.0;
        int priority = 0;
        quint64 order = 0;
        bool loop = false;
    };

    std::mutex mutex;
    std::condition_variable wake;
    bool enabled = true;
    bool shuttingDown = false;
    int volume = 70;
    quint64 nextOrder = 0;
    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<obs_weak_source_t *> outputs;
    std::thread worker;

    Impl() : worker([this] { run(); }) {}

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> guard(mutex);
            shuttingDown = true;
            voices.clear();
        }
        wake.notify_all();
        if (worker.joinable()) worker.join();
        for (auto *output : outputs) obs_weak_source_release(output);
    }

    obs_source_t *activeSourceLocked()
    {
        for (auto *weak : outputs) {
            if (auto *source = obs_weak_source_get_source(weak)) {
                if (obs_source_active(source)) return source;
                obs_source_release(source);
            }
        }
        return nullptr;
    }

    void run()
    {
        using clock = std::chrono::steady_clock;
        auto nextBlock = clock::now();
        std::array<qint16, framesPerBlock * 2> mixed{};
        while (true) {
            obs_source_t *output = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return shuttingDown || !voices.empty(); });
                if (shuttingDown) break;

                mixed.fill(0);
                std::array<double, framesPerBlock * 2> accumulator{};
                const double master = volume / 100.0;
                const double headroom = 1.0 / std::sqrt(1.0 + std::max<size_t>(0, voices.size() - 1) * 0.7);
                for (auto &voice : voices) {
                    const auto *samples = reinterpret_cast<const qint16 *>(voice->samples.constData());
                    const qsizetype totalFrames = voice->samples.size()
                        / static_cast<qsizetype>(voice->channels * sizeof(qint16));
                    const double scale = master * voice->gain * headroom;
                    for (qsizetype frame = 0; frame < framesPerBlock; ++frame) {
                        if (voice->cursorFrames >= totalFrames) {
                            if (!voice->loop) break;
                            voice->cursorFrames = 0;
                        }
                        const qsizetype index = voice->cursorFrames * voice->channels;
                        const qint16 left = samples[index];
                        const qint16 right = voice->channels == 2 ? samples[index + 1] : left;
                        accumulator[frame * 2] += left * scale;
                        accumulator[frame * 2 + 1] += right * scale;
                        ++voice->cursorFrames;
                    }
                }
                for (size_t index = 0; index < mixed.size(); ++index)
                    mixed[index] = static_cast<qint16>(std::clamp(accumulator[index], -32768.0, 32767.0));
                std::erase_if(voices, [](const auto &voice) {
                    const qsizetype totalFrames = voice->samples.size()
                        / static_cast<qsizetype>(voice->channels * sizeof(qint16));
                    return !voice->loop && voice->cursorFrames >= totalFrames;
                });
                output = activeSourceLocked();
            }

            if (output) {
                obs_source_audio audio{};
                audio.data[0] = reinterpret_cast<const uint8_t *>(mixed.data());
                audio.frames = framesPerBlock;
                audio.speakers = SPEAKERS_STEREO;
                audio.format = AUDIO_FORMAT_16BIT;
                audio.samples_per_sec = sampleRate;
                audio.timestamp = os_gettime_ns();
                obs_source_output_audio(output, &audio);
                obs_source_release(output);
            }
            nextBlock += std::chrono::milliseconds(10);
            if (nextBlock < clock::now()) nextBlock = clock::now();
            std::this_thread::sleep_until(nextBlock);
        }
    }
};

RaidAudio::RaidAudio() : impl_(std::make_unique<Impl>()) {}
RaidAudio::~RaidAudio() = default;

void RaidAudio::setEnabled(bool enabled)
{
    std::lock_guard<std::mutex> guard(impl_->mutex);
    impl_->enabled = enabled;
    if (!enabled) impl_->voices.clear();
}

void RaidAudio::setVolume(int percent)
{
    std::lock_guard<std::mutex> guard(impl_->mutex);
    impl_->volume = std::clamp(percent, 0, 100);
}

void RaidAudio::addOutputSource(obs_source_t *source)
{
    if (!source) return;
    std::lock_guard<std::mutex> guard(impl_->mutex);
    for (auto *weak : impl_->outputs)
        if (obs_weak_source_references_source(weak, source)) return;
    impl_->outputs.push_back(obs_source_get_weak_source(source));
}

void RaidAudio::removeOutputSource(obs_source_t *source)
{
    if (!source) return;
    std::lock_guard<std::mutex> guard(impl_->mutex);
    for (auto iterator = impl_->outputs.begin(); iterator != impl_->outputs.end(); ++iterator) {
        if (!obs_weak_source_references_source(*iterator, source)) continue;
        obs_weak_source_release(*iterator);
        impl_->outputs.erase(iterator);
        return;
    }
}

void RaidAudio::play(const QString &path, const QString &group, double gain, int priority, bool loop)
{
    WaveData wave;
    if (!loadWave(path, wave)) return;
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (!impl_->enabled || impl_->volume <= 0) return;
    if (group == "stinger") impl_->voices.clear();
    const auto sameGroup = std::find_if(impl_->voices.begin(), impl_->voices.end(), [&group](const auto &voice) {
        return voice->group == group;
    });
    if (sameGroup != impl_->voices.end()) {
        if ((*sameGroup)->priority >= priority) return;
        impl_->voices.erase(sameGroup);
    }
    if (impl_->voices.size() >= 4) {
        const auto quietest = std::min_element(impl_->voices.begin(), impl_->voices.end(), [](const auto &left, const auto &right) {
            return left->priority != right->priority ? left->priority < right->priority : left->order < right->order;
        });
        if (quietest == impl_->voices.end() || (*quietest)->priority >= priority) return;
        impl_->voices.erase(quietest);
    }
    auto voice = std::make_unique<Impl::Voice>();
    voice->samples = std::move(wave.samples);
    voice->channels = wave.channels;
    voice->group = group;
    voice->gain = std::clamp(gain, 0.0, 1.0);
    voice->priority = priority;
    voice->order = ++impl_->nextOrder;
    voice->loop = loop;
    impl_->voices.push_back(std::move(voice));
    impl_->wake.notify_one();
}

void RaidAudio::cleanup() {}

void RaidAudio::stop()
{
    std::lock_guard<std::mutex> guard(impl_->mutex);
    impl_->voices.clear();
}

} // namespace fx
