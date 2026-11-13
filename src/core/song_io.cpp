#include "core/model.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
namespace piano {
namespace {
int64_t integer(const QJsonObject &o, const char *field, int64_t lo, int64_t hi) {
    const auto v = o.value(QLatin1String(field));
    const double n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < double(lo) || n > double(hi))
        throw FileError("invalid_recording",
                        QCoreApplication::translate("OpenKeys", "Invalid integer: %1").arg(field));
    return int64_t(n);
}
} // namespace
Song readRecording(const QByteArray &data) {
    if (data.size() > MaxFileBytes)
        throw FileError("file_too_large",
                        QCoreApplication::translate("OpenKeys", "Recording exceeds 64 MiB"));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw FileError("invalid_recording", QCoreApplication::translate("OpenKeys", "Expected JSON object"));
    const auto o = doc.object();
    if (o.value("format") != "openkeys-recording" || integer(o, "version", 1, 1) != 1)
        throw FileError("unsupported_recording",
                        QCoreApplication::translate("OpenKeys", "Unknown recording format/version"));
    if (!o.value("events").isArray())
        throw FileError("invalid_recording", QCoreApplication::translate("OpenKeys", "Missing events array"));
    Song song;
    song.title = o.value("title").toString();
    song.author = o.value("author").toString();
    const auto entries = o.value("events").toArray();
    if (size_t(entries.size()) > MaxEvents)
        throw FileError("too_many_events", QCoreApplication::translate("OpenKeys", "Event limit exceeded"));
    song.events.reserve(size_t(entries.size()));
    for (const auto &value : entries) {
        if (!value.isObject())
            throw FileError("invalid_recording",
                            QCoreApplication::translate("OpenKeys", "Event must be an object"));
        const auto item = value.toObject();
        Event e;
        e.timeUs = integer(item, "timeUs", 0, MaxSongUs);
        const auto type = item.value("type").toString();
        if (type == "noteOn")
            e.kind = Kind::NoteOn;
        else if (type == "noteOff")
            e.kind = Kind::NoteOff;
        else if (type == "sustain")
            e.kind = Kind::Sustain;
        else
            throw FileError("invalid_recording",
                            QCoreApplication::translate("OpenKeys", "Unknown event type"));
        e.note = uint8_t(integer(item, "note", 0, 127));
        e.velocity = uint8_t(integer(item, "velocity", 0, 127));
        e.hand = Hand(integer(item, "hand", 0, 2));
        e.key = uint16_t(integer(item, "key", 0, 511));
        song.events.push_back(e);
    }
    if (o.contains("configurations")) {
        if (!o.value("configurations").isArray())
            throw FileError("invalid_recording",
                            QCoreApplication::translate("OpenKeys", "Expected configurations array"));
        const auto configs = o.value("configurations").toArray();
        if (size_t(configs.size()) > MaxEvents)
            throw FileError("too_many_events",
                            QCoreApplication::translate("OpenKeys", "Configuration limit exceeded"));
        for (const auto &value : configs) {
            if (!value.isObject())
                throw FileError("invalid_recording",
                                QCoreApplication::translate("OpenKeys", "Expected configuration object"));
            const auto item = value.toObject();
            PerformanceConfig config;
            config.timeUs = integer(item, "timeUs", 0, MaxSongUs);
            config.transpose = int(integer(item, "transpose", 0, 11));
            for (size_t h = 0; h < 2; ++h) {
                const auto octave = h == 0 ? "leftOctave" : "rightOctave";
                const auto velocity = h == 0 ? "leftVelocity" : "rightVelocity";
                if (item.contains(octave))
                    config.octave[h] = int(integer(item, octave, -3, 3));
                if (item.contains(velocity))
                    config.velocity[h] = int(integer(item, velocity, 0, 127));
            }
            song.configurations.push_back(config);
        }
    }
    validateSong(song);
    return song;
}
void saveRecording(const QString &path, const Song &song) {
    validateSong(song);
    QJsonArray entries;
    for (const auto &e : song.events)
        entries.append(QJsonObject{{"timeUs", qint64(e.timeUs)},
                                   {"type", e.kind == Kind::NoteOn    ? "noteOn"
                                            : e.kind == Kind::NoteOff ? "noteOff"
                                                                      : "sustain"},
                                   {"note", e.note},
                                   {"velocity", e.velocity},
                                   {"hand", int(e.hand)},
                                   {"key", e.key}});
    QJsonArray configs;
    for (const auto &config : song.configurations) {
        QJsonObject item{{"timeUs", qint64(config.timeUs)}, {"transpose", config.transpose}};
        for (size_t h = 0; h < 2; ++h) {
            if (config.octave[h])
                item[h == 0 ? "leftOctave" : "rightOctave"] = *config.octave[h];
            if (config.velocity[h])
                item[h == 0 ? "leftVelocity" : "rightVelocity"] = *config.velocity[h];
        }
        configs.append(item);
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"format", "openkeys-recording"},
                                                       {"version", 1},
                                                       {"title", song.title},
                                                       {"author", song.author},
                                                       {"events", entries},
                                                       {"configurations", configs}})
                                 .toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFileBytes)
        throw FileError("file_too_large",
                        QCoreApplication::translate("OpenKeys", "Recording exceeds 64 MiB"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw FileError("io_error", file.errorString());
}
Song readSong(const QString &path) {
    auto bytes = readBounded(path);
    return path.endsWith(".eop", Qt::CaseInsensitive) ? parseEop(bytes) : readRecording(bytes);
}
} // namespace piano
