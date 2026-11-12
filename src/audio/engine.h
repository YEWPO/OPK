#pragma once
#include "core/model.h"
#include <memory>
namespace piano {
struct Soundfont;
struct SoundfontLoad {
    std::shared_ptr<Soundfont> font;
    QString error;
};
struct AudioDevice {
    int id;
    QString name;
    double sampleRate;
};
struct AudioConfig {
    int device = -1;
    int frames = 256;
    bool exclusive = false;
};
struct AudioStatus {
    bool ready = false, playing = false, active = false;
    int64_t positionUs = 0;
    double sampleRate = 48000, reportedLatencyMs = 0;
    uint64_t underruns = 0, overflows = 0;
    uint64_t nonFiniteSamples = 0, clippedSamples = 0, renderFailures = 0;
    int activeVoices = 0;
    uint64_t processedPlayRequest = 0;
};
class AudioEngine {
  public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;
    static SoundfontLoad loadSoundfont(const QString &path,
                                       double sampleRate = 48000); // call on worker thread
    std::vector<AudioDevice> devices();
    void refreshDevices(); // explicitly stops playback and rebuilds device discovery
    double preferredSampleRate(const AudioConfig &config);
    QString start(const AudioConfig &config); // negotiated fallback described in returned text
    void shutdown();
    void setSoundfont(std::shared_ptr<Soundfont> font); // stream stopped during swap
    void setSong(const Song &song);                     // copies/prepares outside callback, resets playback
    bool send(const Event &event);
    uint64_t play();               // request ID; zero if the queue rejected the request
    void seek(int64_t positionUs); // synchronous, preserves playing/paused state
    void pause();
    void stop();
    void releaseLive();
    void panic();
    void setSpeed(double speed);
    void setVolume(double gain);
    AudioStatus status() const;
    // Same callback path, exposed for deterministic tests and offline diagnostics.
    // Must not be called while a device stream is running.
    void render(float *interleaved, unsigned frames);

  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace piano
