#include "audio/soundfont_check.h"
#include "audio/engine.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace piano {
namespace {
constexpr unsigned BlockFrames = 512;
struct Measurements {
    double energy = 0, peak = 0;
    uint64_t samples = 0;
    int maxVoices = 0;
    void add(const float *data, unsigned frames, int voices) {
        maxVoices = std::max(maxVoices, voices);
        for (unsigned i = 0; i < frames * 2; ++i) {
            if (!std::isfinite(data[i]))
                throw FileError("invalid_audio", "Sampler output contains a non-finite value");
            energy += double(data[i]) * data[i];
            peak = std::max(peak, std::abs(double(data[i])));
        }
        samples += uint64_t(frames) * 2;
    }
    double rms() const {
        return samples ? std::sqrt(energy / double(samples)) : 0.;
    }
    QJsonObject json() const {
        return {{"rms", rms()}, {"peak", peak}, {"samples", qint64(samples)}, {"maxActiveVoices", maxVoices}};
    }
};
class WaveWriter {
  public:
    WaveWriter(const QString &path, int rate) : file_(path), rate_(rate) {
        if (!file_.open(QIODevice::WriteOnly))
            throw FileError("io_error", file_.errorString());
        if (file_.write(QByteArray(44, '\0')) != 44)
            throw FileError("io_error", file_.errorString());
    }
    void append(const float *data, unsigned frames) {
        const uint64_t bytes = uint64_t(frames) * 4;
        if (size_ + bytes > std::numeric_limits<uint32_t>::max() - 36)
            throw FileError("wav_too_large", "WAV exceeds the RIFF size limit");
        QByteArray pcm(qsizetype(bytes), '\0');
        for (unsigned i = 0; i < frames * 2; ++i) {
            const auto sample = qint16(std::clamp(std::lround(double(data[i]) * 32768.), -32768L, 32767L));
            qToLittleEndian<quint16>(quint16(sample), pcm.data() + i * 2);
        }
        if (file_.write(pcm) != pcm.size())
            throw FileError("io_error", file_.errorString());
        size_ += bytes;
    }
    void commit() {
        QByteArray header(44, '\0');
        header.replace(0, 4, "RIFF");
        qToLittleEndian<quint32>(quint32(size_ + 36), header.data() + 4);
        header.replace(8, 8, "WAVEfmt ");
        qToLittleEndian<quint32>(16, header.data() + 16);
        qToLittleEndian<quint16>(1, header.data() + 20); // PCM16
        qToLittleEndian<quint16>(2, header.data() + 22);
        qToLittleEndian<quint32>(quint32(rate_), header.data() + 24);
        qToLittleEndian<quint32>(quint32(rate_ * 4), header.data() + 28);
        qToLittleEndian<quint16>(4, header.data() + 32);
        qToLittleEndian<quint16>(16, header.data() + 34);
        header.replace(36, 4, "data");
        qToLittleEndian<quint32>(quint32(size_), header.data() + 40);
        if (!file_.seek(0) || file_.write(header) != header.size() || !file_.commit())
            throw FileError("io_error", file_.errorString());
    }

  private:
    QSaveFile file_;
    int rate_;
    uint64_t size_ = 0;
};
Measurements render(AudioEngine &engine, int64_t frames, WaveWriter *wave = nullptr,
                    const std::function<void(int64_t, const float *, unsigned)> &observe = {}) {
    std::array<float, BlockFrames * 2> buffer{};
    Measurements result;
    for (int64_t cursor = 0; cursor < frames;) {
        const auto block = unsigned(std::min<int64_t>(BlockFrames, frames - cursor));
        engine.render(buffer.data(), block);
        result.add(buffer.data(), block, engine.status().activeVoices);
        if (wave)
            wave->append(buffer.data(), block);
        if (observe)
            observe(cursor, buffer.data(), block);
        cursor += block;
    }
    return result;
}
void clear(AudioEngine &engine) {
    engine.panic();
    render(engine, 64);
}
QString sourceHash(const QString &path) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly))
        throw FileError("io_error", source.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&source))
        throw FileError("io_error", "Could not hash the complete soundfont");
    return QString::fromLatin1(hash.result().toHex());
}
void saveReport(const QString &path, const QJsonObject &report) {
    const auto bytes = QJsonDocument(report).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw FileError("io_error", file.errorString());
}
} // namespace

QJsonObject checkSoundfont(const QString &path, const QString &outputDirectory, const Song &demo) {
    validateSong(demo);
    if (demo.events.empty() || demo.durationUs() > 60 * 1000000)
        throw FileError("invalid_demo", "Diagnostic demo must be non-empty and at most 60 seconds");
    const QFileInfo font(path);
    if (!font.isFile())
        throw FileError("io_error", "Soundfont file is missing or not a regular file");
    const QString hash = sourceHash(path);
    QDir out(outputDirectory);
    if (!out.mkpath("."))
        throw FileError("io_error", "Could not create diagnostic output directory");
    const QStringList outputs{"report.json", "demo-44100.wav", "demo-48000.wav", "sustain-44100.wav",
                              "sustain-48000.wav"};
    for (const auto &output : outputs)
        if (QFileInfo(out.filePath(output)).canonicalFilePath() == font.canonicalFilePath())
            throw FileError("invalid_output", "Output would overwrite the source soundfont");

    QElapsedTimer total;
    total.start();
    QJsonArray rates;
    bool passed = true;
    QJsonObject report{{"format", "openkeys-soundfont-check"},
                       {"version", 1},
                       {"passed", false},
                       {"state", "running"},
                       {"soundfont", font.absoluteFilePath()},
                       {"soundfontSha256", hash},
                       {"soundfontBytes", font.size()},
                       {"physicalAudioDeviceTested", false}};
    saveReport(out.filePath("report.json"), report);
    try {
        for (int rate : {44100, 48000}) {
            QElapsedTimer loading;
            loading.start();
            auto loaded = AudioEngine::loadSoundfont(path, rate);
            if (!loaded.font)
                throw FileError("soundfont_load_failed", loaded.error);
            const qint64 loadMs = loading.elapsed();
            AudioEngine engine;
            engine.setSoundfont(std::move(loaded.font));
            QJsonArray notes;
            QJsonArray errors;
            for (int note = 21; note <= 108; ++note) {
                for (int velocity : {32, 80, 120}) {
                    clear(engine);
                    engine.send(Event{0, Kind::NoteOn, uint8_t(note), uint8_t(velocity), Hand::Right});
                    const auto samples = render(engine, rate * 8 / 100);
                    auto entry = samples.json();
                    entry["note"] = note;
                    entry["velocity"] = velocity;
                    const bool sounds = samples.maxVoices > 0 && samples.rms() > 1.e-9;
                    entry["passed"] = sounds;
                    notes.append(entry);
                    if (!sounds)
                        errors.append(
                            QString("Silent or unmapped note %1, velocity %2").arg(note).arg(velocity));
                    engine.send(Event{0, Kind::NoteOff, uint8_t(note), 0, Hand::Right});
                }
            }
            clear(engine);
            Song pedal;
            pedal.events = {{0, Kind::Sustain, 0, 127, Hand::Right}};
            for (int note : {60, 64, 67})
                pedal.events.push_back({0, Kind::NoteOn, uint8_t(note), 80, Hand::Right});
            for (int note : {60, 64, 67})
                pedal.events.push_back({250000, Kind::NoteOff, uint8_t(note), 0, Hand::Right});
            pedal.events.push_back({650000, Kind::Sustain, 0, 0, Hand::Right});
            engine.setSong(pedal);
            engine.play();
            WaveWriter sustainWave(out.filePath(QString("sustain-%1.wav").arg(rate)), rate);
            Measurements held, ending;
            const auto sustain = render(engine, int64_t(rate) * 665 / 100, &sustainWave,
                                        [&](int64_t frame, const float *data, unsigned count) {
                                            for (unsigned i = 0; i < count; ++i) {
                                                const auto at = frame + i;
                                                if (at >= rate * 35 / 100 && at < rate * 55 / 100)
                                                    held.add(data + i * 2, 1, 0);
                                                if (at >= int64_t(rate) * 645 / 100)
                                                    ending.add(data + i * 2, 1, 0);
                                            }
                                        });
            sustainWave.commit();
            if (held.rms() < 1.e-9)
                errors.append("Sustain produced no sound after key release");
            if (ending.rms() > std::max(1.e-5, held.rms() * .05))
                errors.append("Release tail did not decay sufficiently after pedal release");
            clear(engine);
            engine.setSong(demo);
            engine.play();
            WaveWriter demoWave(out.filePath(QString("demo-%1.wav").arg(rate)), rate);
            const auto demoFrames =
                int64_t(std::ceil(double(demo.durationUs()) * rate / 1000000.)) + rate * 6;
            const auto demonstration = render(engine, demoFrames, &demoWave);
            demoWave.commit();
            if (demonstration.rms() < 1.e-9)
                errors.append("Demo is silent");
            const auto status = engine.status();
            if (status.nonFiniteSamples || status.renderFailures || status.overflows)
                errors.append("Sampler reported invalid samples, rendering errors or event overflow");
            const bool ratePassed = errors.isEmpty();
            passed = passed && ratePassed;
            rates.append(QJsonObject{{"sampleRate", rate},
                                     {"passed", ratePassed},
                                     {"loadMs", loadMs},
                                     {"notes", notes},
                                     {"sustain", sustain.json()},
                                     {"sustainHeldRms", held.rms()},
                                     {"releaseEndingRms", ending.rms()},
                                     {"demo", demonstration.json()},
                                     {"nonFiniteSamples", qint64(status.nonFiniteSamples)},
                                     {"renderFailures", qint64(status.renderFailures)},
                                     {"clippedSamples", qint64(status.clippedSamples)},
                                     {"errors", errors}});
        }
    } catch (const std::exception &error) {
        report["state"] = "failed";
        report["error"] = QString::fromUtf8(error.what());
        report["rates"] = rates;
        report["elapsedMs"] = total.elapsed();
        saveReport(out.filePath("report.json"), report);
        throw;
    }
    report["state"] = "complete";
    report["passed"] = passed;
    report["rates"] = rates;
    report["elapsedMs"] = total.elapsed();
    saveReport(out.filePath("report.json"), report);
    return report;
}
} // namespace piano
