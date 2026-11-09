#include "audio/engine.h"
#include <fluidsynth.h>
#include <portaudio.h>
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

namespace piano {
struct Soundfont {
    double sampleRate = 48000;
    fluid_settings_t *settings = nullptr;
    fluid_synth_t *synth = nullptr;
    ~Soundfont() {
        if (synth)
            delete_fluid_synth(synth);
        if (settings)
            delete_fluid_settings(settings);
    }
};
namespace {
enum class Command { Event, Play, Pause, Stop, LiveOff, Speed, Volume };
struct Message {
    Command command = Command::Event;
    Event event;
    double value = 0;
};
constexpr size_t QueueSize = 4096;
} // namespace
struct AudioEngine::Impl {
    std::shared_ptr<Soundfont> font;
    PaStream *stream = nullptr;
    bool initialized = false;
    AudioConfig config;
    std::array<Message, QueueSize> queue{};
    std::atomic<size_t> write{0}, read{0};
    std::atomic<bool> emergency{false}, playingView{false};
    std::atomic<int64_t> positionView{0};
    std::atomic<uint64_t> underruns{0}, overflows{0};
    std::atomic<uint64_t> nonFiniteSamples{0}, clippedSamples{0}, renderFailures{0};
    std::atomic<int> activeVoices{0};
    std::array<std::array<int, 128>, 6> counts{};
    std::array<std::array<uint8_t, 128>, 6> velocities{};
    std::array<std::array<bool, 128>, 6> sustained{};
    std::array<uint8_t, 6> pedals{};
    std::vector<Event> timeline;
    size_t cursor = 0;
    double position = 0, speed = 1, gain = .8, rate = 48000, latency = 0;
    bool playing = false, muted = false;
    QString initError;
    bool push(Message msg) {
        const size_t w = write.load(std::memory_order_relaxed), next = (w + 1) % QueueSize;
        if (next == read.load(std::memory_order_acquire)) {
            overflows.fetch_add(1, std::memory_order_relaxed);
            emergency.store(true, std::memory_order_release);
            return false;
        }
        queue[w] = msg;
        write.store(next, std::memory_order_release);
        return true;
    }
    void silence(int source, bool clear) {
        for (int c = source * 3; c < source * 3 + 3; ++c) {
            if (font) {
                fluid_synth_cc(font->synth, c, 64, 0);
                fluid_synth_all_sounds_off(font->synth, c);
            }
            if (clear) {
                counts[size_t(c)].fill(0);
                velocities[size_t(c)].fill(0);
                sustained[size_t(c)].fill(false);
                pedals[size_t(c)] = 0;
            }
        }
    }
    void reset() {
        silence(0, true);
        silence(1, true);
        playing = false;
        muted = true;
        cursor = 0;
        position = 0;
        playingView.store(false);
        positionView.store(0);
        read.store(write.load(std::memory_order_acquire), std::memory_order_release);
        emergency.store(false);
    }
    void dispatch(const Event &event) {
        const int c = std::clamp(int(event.source), 0, 1) * 3 + std::clamp(int(event.hand), 0, 2);
        const size_t channel = size_t(c), note = size_t(event.note);
        if (note >= 128)
            return;
        switch (event.kind) {
        case Kind::NoteOn:
            muted = false;
            sustained[channel][note] = false;
            ++counts[channel][note];
            velocities[channel][note] = event.velocity;
            if (font)
                fluid_synth_noteon(font->synth, c, event.note, event.velocity);
            break;
        case Kind::NoteOff:
            if (counts[channel][note] > 0 && --counts[channel][note] == 0) {
                sustained[channel][note] = pedals[channel] >= 64;
                if (font)
                    fluid_synth_noteoff(font->synth, c, event.note);
            }
            break;
        case Kind::Sustain:
            pedals[channel] = event.velocity;
            if (event.velocity < 64)
                sustained[channel].fill(false);
            if (font)
                fluid_synth_cc(font->synth, c, 64, event.velocity);
            break;
        case Kind::Panic:
            silence(event.source, true);
            break;
        }
    }
    void commands() {
        if (emergency.exchange(false, std::memory_order_acq_rel)) {
            reset();
            return;
        }
        size_t r = read.load(std::memory_order_relaxed);
        const size_t end = write.load(std::memory_order_acquire);
        while (r != end) {
            const auto msg = queue[r];
            r = (r + 1) % QueueSize;
            switch (msg.command) {
            case Command::Event:
                dispatch(msg.event);
                break;
            case Command::LiveOff:
                silence(0, true);
                break;
            case Command::Play:
                if (!playing && !timeline.empty()) {
                    if (cursor == timeline.size()) {
                        cursor = 0;
                        position = 0;
                        silence(1, true);
                    }
                    for (int c = 3; c < 6; ++c) {
                        if (font)
                            fluid_synth_cc(font->synth, c, 64, pedals[size_t(c)]);
                        for (int n = 0; n < 128; ++n)
                            if ((counts[size_t(c)][size_t(n)] || sustained[size_t(c)][size_t(n)]) && font) {
                                fluid_synth_noteon(font->synth, c, n, velocities[size_t(c)][size_t(n)]);
                                if (!counts[size_t(c)][size_t(n)])
                                    fluid_synth_noteoff(font->synth, c, n);
                            }
                    }
                    playing = true;
                }
                break;
            case Command::Pause:
                playing = false;
                silence(1, false);
                break;
            case Command::Stop:
                playing = false;
                silence(1, true);
                position = 0;
                cursor = 0;
                break;
            case Command::Speed:
                speed = std::clamp(msg.value, .25, 2.);
                break;
            case Command::Volume:
                gain = std::clamp(msg.value, 0., 1.);
                break;
            }
        }
        read.store(r, std::memory_order_release);
    }
    void process(float *out, unsigned frames) {
        unsigned done = 0;
        unsigned playbackEvents = 0;
        uint64_t invalid = 0, clipped = 0;
        while (done < frames) {
            commands();
            unsigned block = std::min(64u, frames - done);
            if (playing) {
                while (cursor < timeline.size() && double(timeline[cursor].timeUs) <= position + .0001) {
                    if (++playbackEvents > 8192) {
                        overflows.fetch_add(1, std::memory_order_relaxed);
                        reset();
                        break;
                    }
                    dispatch(timeline[cursor++]);
                }
                if (cursor == timeline.size()) {
                    playing = false;
                    // Validated songs already pair every note and release the
                    // pedal. Let the sampler's final release tails decay.
                } else {
                    const double until =
                        (double(timeline[cursor].timeUs) - position) * rate / (1000000. * speed);
                    if (until < double(block))
                        block = std::max(1u, unsigned(std::ceil(until)));
                }
            }
            auto *output = out + done * 2;
            if (font) {
                if (fluid_synth_write_float(font->synth, int(block), output, 0, 2, output, 1, 2) !=
                    FLUID_OK) {
                    renderFailures.fetch_add(1, std::memory_order_relaxed);
                    std::fill_n(output, block * 2, 0.f);
                }
            } else
                std::fill_n(output, block * 2, 0.f);
            for (unsigned i = 0; i < block * 2; ++i) {
                if (!std::isfinite(output[i]))
                    ++invalid;
                else if (!muted && std::abs(double(output[i]) * gain) > 1.)
                    ++clipped;
                output[i] = muted || !std::isfinite(output[i])
                                ? 0.f
                                : std::clamp(float(double(output[i]) * gain), -1.f, 1.f);
            }
            if (playing)
                position += double(block) * 1000000. * speed / rate;
            done += block;
        }
        positionView.store(int64_t(std::llround(position)), std::memory_order_relaxed);
        playingView.store(playing, std::memory_order_relaxed);
        nonFiniteSamples.fetch_add(invalid, std::memory_order_relaxed);
        clippedSamples.fetch_add(clipped, std::memory_order_relaxed);
        activeVoices.store(font ? fluid_synth_get_active_voice_count(font->synth) : 0,
                           std::memory_order_relaxed);
    }
    static int callback(const void *, void *output, unsigned long frames, const PaStreamCallbackTimeInfo *,
                        PaStreamCallbackFlags flags, void *context) {
        auto &self = *static_cast<Impl *>(context);
        if (flags & paOutputUnderflow)
            self.underruns.fetch_add(1, std::memory_order_relaxed);
        self.process(static_cast<float *>(output), unsigned(frames));
        return paContinue;
    }
};
AudioEngine::AudioEngine() : p_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() {
    shutdown();
    if (p_->initialized)
        Pa_Terminate();
}
SoundfontLoad AudioEngine::loadSoundfont(const QString &path, double sampleRate) {
    // FluidSynth's own device drivers must not probe audio hardware when we only
    // want its sampler. PortAudio is owned and initialized by this application.
    static std::once_flag drivers;
    std::call_once(drivers, [] {
        const char *enabled[] = {nullptr};
        fluid_audio_driver_register(enabled);
    });
    if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 96000)
        return {{}, QStringLiteral("不支持的采样率")};
    auto font = std::make_shared<Soundfont>();
    font->sampleRate = sampleRate;
    font->settings = new_fluid_settings();
    if (!font->settings)
        return {{}, QStringLiteral("无法分配音源设置")};
    fluid_settings_setnum(font->settings, "synth.sample-rate", sampleRate);
    fluid_settings_setnum(font->settings, "synth.gain", .5);
    fluid_settings_setint(font->settings, "synth.polyphony", 256);
    fluid_settings_setint(font->settings, "synth.threadsafe-api", 0); // callback is sole owner while running
    font->synth = new_fluid_synth(font->settings);
    if (!font->synth)
        return {{}, QStringLiteral("无法初始化采样引擎")};
    const int id = fluid_synth_sfload(font->synth, path.toUtf8().constData(), 0);
    if (id < 0)
        return {{}, QStringLiteral("无法载入 SF2 音源，请检查文件是否完整：%1").arg(path)};
    fluid_preset_t *preset = nullptr;
    auto *sf = fluid_synth_get_sfont_by_id(font->synth, id);
    if (sf) {
        fluid_sfont_iteration_start(sf);
        preset = fluid_sfont_iteration_next(sf);
    }
    if (!preset)
        return {{}, QStringLiteral("该音源没有可播放的预设")};
    const int bank = fluid_preset_get_banknum(preset), program = fluid_preset_get_num(preset);
    for (int c = 0; c < 6; ++c)
        fluid_synth_program_select(font->synth, c, id, bank, program);
    return {std::move(font), {}};
}
std::vector<AudioDevice> AudioEngine::devices() {
    if (!p_->initialized) {
        const auto err = Pa_Initialize();
        if (err != paNoError) {
            p_->initError = QString::fromUtf8(Pa_GetErrorText(err));
            return {};
        }
        p_->initialized = true;
    }
    std::vector<AudioDevice> result;
    const int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const auto *info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels < 2)
            continue;
#ifdef _WIN32
        const auto *host = Pa_GetHostApiInfo(info->hostApi);
        if (!host || host->type != paWASAPI)
            continue;
#endif
        result.push_back({i, QString::fromUtf8(info->name), info->defaultSampleRate});
    }
    return result;
}
void AudioEngine::refreshDevices() {
    shutdown();
    if (p_->initialized) {
        Pa_Terminate();
        p_->initialized = false;
    }
}
double AudioEngine::preferredSampleRate(const AudioConfig &config) {
    const auto available = devices();
    if (available.empty())
        return 48000;
    const int desired = config.device == -1 ? Pa_GetDefaultOutputDevice() : config.device;
    for (const auto &d : available)
        if (d.id == desired)
            return std::clamp(d.sampleRate, 8000., 96000.);
    return std::clamp(available.front().sampleRate, 8000., 96000.);
}
QString AudioEngine::start(const AudioConfig &config) {
    shutdown();
    p_->config = config;
    const auto available = devices();
    if (available.empty())
        return QStringLiteral("没有可用的立体声音频设备。%1").arg(p_->initError);
    int id = config.device;
    if (id == -1) {
        id = Pa_GetDefaultOutputDevice();
        if (std::none_of(available.begin(), available.end(), [id](const auto &d) { return d.id == id; }))
            id = available.front().id;
    }
    if (std::none_of(available.begin(), available.end(), [id](const auto &d) { return d.id == id; }))
        return QStringLiteral("所选设备已不可用，请重新选择");
    const auto *info = Pa_GetDeviceInfo(id);
    p_->rate = std::clamp(info->defaultSampleRate, 8000., 96000.);
    if (p_->font && std::abs(p_->font->sampleRate - p_->rate) > .5)
        return QStringLiteral("设备采样率已变化，需要重新载入音源");
    PaStreamParameters output{};
    output.device = id;
    output.channelCount = 2;
    output.sampleFormat = paFloat32;
    output.suggestedLatency = info->defaultLowOutputLatency;
    QString notice;
#ifdef _WIN32
    PaWasapiStreamInfo wasapi{};
    wasapi.size = sizeof(wasapi);
    wasapi.hostApiType = paWASAPI;
    wasapi.version = 1;
    wasapi.flags = config.exclusive ? paWinWasapiExclusive : 0;
    output.hostApiSpecificStreamInfo = &wasapi;
#else
    if (config.exclusive)
        notice = QStringLiteral("此平台使用系统音频模式。 ");
#endif
    auto open = [&]() {
        return Pa_OpenStream(&p_->stream, nullptr, &output, p_->rate,
                             unsigned(std::clamp(config.frames, 64, 2048)), paNoFlag, &Impl::callback,
                             p_.get());
    };
    PaError err = open();
#ifdef _WIN32
    if (err != paNoError && config.exclusive) {
        const auto reason = QString::fromUtf8(Pa_GetErrorText(err));
        wasapi.flags = 0;
        p_->stream = nullptr;
        err = open();
        notice = QStringLiteral("独占模式不可用（%1），已退回共享模式。 ").arg(reason);
    }
#endif
    if (err != paNoError) {
        p_->stream = nullptr;
        return QStringLiteral("无法打开音频设备：%1").arg(QString::fromUtf8(Pa_GetErrorText(err)));
    }
    const auto *streamInfo = Pa_GetStreamInfo(p_->stream);
    if (streamInfo)
        p_->latency = streamInfo->outputLatency * 1000.;
    err = Pa_StartStream(p_->stream);
    if (err != paNoError) {
        shutdown();
        return QStringLiteral("无法启动音频设备：%1").arg(QString::fromUtf8(Pa_GetErrorText(err)));
    }
    return notice;
}
void AudioEngine::shutdown() {
    if (p_->stream) {
        Pa_StopStream(p_->stream);
        Pa_CloseStream(p_->stream);
        p_->stream = nullptr;
    }
    p_->reset();
}
void AudioEngine::setSoundfont(std::shared_ptr<Soundfont> font) {
    shutdown();
    p_->font = std::move(font);
    if (p_->font)
        p_->rate = p_->font->sampleRate;
}
void AudioEngine::setSong(const Song &song) {
    validateSong(song);
    const bool active = p_->stream && Pa_IsStreamActive(p_->stream) == 1;
    if (active)
        Pa_StopStream(p_->stream);
    p_->reset();
    p_->timeline = song.events;
    for (auto &e : p_->timeline)
        e.source = 1;
    if (active)
        Pa_StartStream(p_->stream);
}
bool AudioEngine::send(const Event &event) {
    if (event.source > 1 || int(event.hand) > 2 || event.note > 127 || event.velocity > 127 ||
        int(event.kind) > int(Kind::Panic) || (event.kind == Kind::NoteOn && event.velocity == 0))
        return false;
    return p_->push({Command::Event, event, 0});
}
void AudioEngine::play() {
    p_->push({Command::Play, {}, 0});
}
void AudioEngine::pause() {
    p_->push({Command::Pause, {}, 0});
}
void AudioEngine::stop() {
    p_->push({Command::Stop, {}, 0});
}
void AudioEngine::releaseLive() {
    p_->push({Command::LiveOff, {}, 0});
}
void AudioEngine::panic() {
    p_->emergency.store(true, std::memory_order_release);
}
void AudioEngine::setSpeed(double s) {
    if (std::isfinite(s))
        p_->push({Command::Speed, {}, s});
}
void AudioEngine::setVolume(double v) {
    if (std::isfinite(v))
        p_->push({Command::Volume, {}, v});
}
AudioStatus AudioEngine::status() const {
    return {bool(p_->font),
            p_->playingView.load(),
            p_->stream && Pa_IsStreamActive(p_->stream) == 1,
            p_->positionView.load(),
            p_->rate,
            p_->latency,
            p_->underruns.load(),
            p_->overflows.load(),
            p_->nonFiniteSamples.load(),
            p_->clippedSamples.load(),
            p_->renderFailures.load(),
            p_->activeVoices.load()};
}
void AudioEngine::render(float *data, unsigned frames) {
    p_->process(data, frames);
}
} // namespace piano
