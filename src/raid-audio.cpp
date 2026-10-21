#include "raid-audio.hpp"

#include <QByteArray>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

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
    WAVEFORMATEX format{};
    QByteArray samples;
};

bool loadWave(const QString &path, double gain, WaveData &result)
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
    result.format.wFormatTag = little16(format.constData());
    result.format.nChannels = little16(format.constData() + 2);
    result.format.nSamplesPerSec = little32(format.constData() + 4);
    result.format.nAvgBytesPerSec = little32(format.constData() + 8);
    result.format.nBlockAlign = little16(format.constData() + 12);
    result.format.wBitsPerSample = little16(format.constData() + 14);
    result.format.cbSize = 0;
    if (result.format.wFormatTag != WAVE_FORMAT_PCM || result.format.wBitsPerSample != 16
        || (result.format.nChannels != 1 && result.format.nChannels != 2)) return false;
    result.samples = samples;
    gain = std::clamp(gain, 0.0, 1.0);
    auto *values = reinterpret_cast<qint16 *>(result.samples.data());
    const qsizetype count = result.samples.size() / static_cast<qsizetype>(sizeof(qint16));
    for (qsizetype index = 0; index < count; ++index)
        values[index] = static_cast<qint16>(std::clamp(values[index] * gain, -32768.0, 32767.0));
    return true;
}
}

struct RaidAudio::Impl {
    struct Voice {
        HWAVEOUT output = nullptr;
        WAVEHDR header{};
        QByteArray samples;
        QString group;
        int priority = 0;
        quint64 order = 0;
    };
    bool enabled = true;
    int volume = 70;
    quint64 nextOrder = 0;
    std::vector<std::unique_ptr<Voice>> voices;

    void release(Voice &voice)
    {
        if (!voice.output) return;
        // Reset is synchronous and returns all queued buffers to the caller.
        // Always do this before unpreparing memory so plugin shutdown remains
        // safe even when OBS closes during a cue.
        waveOutReset(voice.output);
        waveOutUnprepareHeader(voice.output, &voice.header, sizeof(voice.header));
        waveOutClose(voice.output);
        voice.output = nullptr;
    }
};

RaidAudio::RaidAudio() : impl_(std::make_unique<Impl>()) {}
RaidAudio::~RaidAudio() { stop(); }

void RaidAudio::setEnabled(bool enabled)
{
    impl_->enabled = enabled;
    if (!enabled) stop();
}

void RaidAudio::setVolume(int percent) { impl_->volume = std::clamp(percent, 0, 100); }

void RaidAudio::play(const QString &path, const QString &group, double gain, int priority)
{
    cleanup();
    if (!impl_->enabled || impl_->volume <= 0) return;
    if (group == "stinger") stop();
    const auto sameGroup = std::find_if(impl_->voices.begin(), impl_->voices.end(), [&group](const auto &voice) {
        return voice->group == group;
    });
    if (sameGroup != impl_->voices.end()) {
        if ((*sameGroup)->priority >= priority) return;
        impl_->release(**sameGroup);
        impl_->voices.erase(sameGroup);
    }
    if (impl_->voices.size() >= 3) {
        const auto quietest = std::min_element(impl_->voices.begin(), impl_->voices.end(), [](const auto &left, const auto &right) {
            return left->priority != right->priority ? left->priority < right->priority : left->order < right->order;
        });
        if (quietest == impl_->voices.end() || (*quietest)->priority >= priority) return;
        impl_->release(**quietest);
        impl_->voices.erase(quietest);
    }
    WaveData wave;
    const double master = impl_->volume / 100.0;
    const double overlapHeadroom = 1.0 / std::sqrt(1.0 + impl_->voices.size() * 0.7);
    if (!loadWave(path, master * gain * overlapHeadroom, wave)) return;
    auto voice = std::make_unique<Impl::Voice>();
    voice->samples = std::move(wave.samples);
    voice->group = group;
    voice->priority = priority;
    voice->order = ++impl_->nextOrder;
    if (waveOutOpen(&voice->output, WAVE_MAPPER, &wave.format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    voice->header.lpData = voice->samples.data();
    voice->header.dwBufferLength = static_cast<DWORD>(voice->samples.size());
    if (waveOutPrepareHeader(voice->output, &voice->header, sizeof(voice->header)) != MMSYSERR_NOERROR) {
        waveOutClose(voice->output); return;
    }
    if (waveOutWrite(voice->output, &voice->header, sizeof(voice->header)) != MMSYSERR_NOERROR) {
        impl_->release(*voice); return;
    }
    impl_->voices.push_back(std::move(voice));
}

void RaidAudio::cleanup()
{
    auto &voices = impl_->voices;
    for (auto it = voices.begin(); it != voices.end();) {
        if (((*it)->header.dwFlags & WHDR_DONE) != 0) {
            impl_->release(**it);
            it = voices.erase(it);
        } else ++it;
    }
}

void RaidAudio::stop()
{
    for (auto &voice : impl_->voices) impl_->release(*voice);
    impl_->voices.clear();
}

} // namespace fx
