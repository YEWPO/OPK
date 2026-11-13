#include "core/model.h"
#include <QCoreApplication>
#include <QFile>
#include <algorithm>
namespace piano {
QString noteName(int note) {
    static const char *names[] = {"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};
    if (note < 0 || note > 127)
        return QStringLiteral("—");
    return QString::fromUtf8(names[note % 12]) + QString::number(note / 12 - 1);
}
QByteArray readBounded(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw FileError("io_error", file.errorString());
    if (file.size() > MaxFileBytes)
        throw FileError("file_too_large",
                        QCoreApplication::translate("OpenKeys", "Maximum file size is 64 MiB"));
    auto bytes = file.read(MaxFileBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaxFileBytes)
        throw FileError("io_error", QCoreApplication::translate("OpenKeys", "Could not read complete file"));
    return bytes;
}
const PerformanceConfig *Song::configurationAt(int64_t timeUs) const {
    const auto it =
        std::upper_bound(configurations.begin(), configurations.end(), timeUs,
                         [](int64_t time, const PerformanceConfig &config) { return time < config.timeUs; });
    return it == configurations.begin() ? nullptr : &*std::prev(it);
}
void validateSong(const Song &song) {
    int64_t configTime = 0;
    if (song.configurations.size() > MaxEvents)
        throw FileError("too_many_events",
                        QCoreApplication::translate("OpenKeys", "Configuration limit exceeded"));
    for (const auto &c : song.configurations) {
        if (c.timeUs < configTime || c.timeUs > MaxSongUs || c.transpose < 0 || c.transpose > 11)
            throw FileError("corrupt_events",
                            QCoreApplication::translate("OpenKeys", "Invalid performance configuration"));
        configTime = c.timeUs;
        for (size_t h = 0; h < 2; ++h)
            if ((c.octave[h] && (*c.octave[h] < -3 || *c.octave[h] > 3)) ||
                (c.velocity[h] && (*c.velocity[h] < 0 || *c.velocity[h] > 127)))
                throw FileError("corrupt_events", QCoreApplication::translate(
                                                      "OpenKeys", "Invalid performance configuration value"));
    }
    if (song.events.size() > MaxEvents)
        throw FileError("too_many_events",
                        QCoreApplication::translate("OpenKeys", "Maximum is 1,000,000 events"));
    std::array<std::array<int, 128>, 3> held{};
    std::array<bool, 3> pedal{};
    int64_t previous = 0;
    for (const auto &e : song.events) {
        if (e.timeUs < previous || e.timeUs > MaxSongUs || e.note > 127 || e.velocity > 127 ||
            int(e.hand) > 2 || e.key > 511)
            throw FileError("corrupt_events",
                            QCoreApplication::translate("OpenKeys", "Invalid event time or value"));
        previous = e.timeUs;
        const auto hand = size_t(e.hand);
        switch (e.kind) {
        case Kind::NoteOn:
            if (!e.velocity)
                throw FileError("corrupt_events",
                                QCoreApplication::translate("OpenKeys", "Note-on velocity must be positive"));
            ++held[hand][e.note];
            break;
        case Kind::NoteOff:
            if (--held[hand][e.note] < 0)
                throw FileError("corrupt_events",
                                QCoreApplication::translate("OpenKeys", "Unmatched note-off"));
            break;
        case Kind::Sustain:
            pedal[hand] = e.velocity >= 64;
            break;
        default:
            throw FileError("corrupt_events",
                            QCoreApplication::translate("OpenKeys", "Panic is not a recording event"));
        }
    }
    for (const auto &h : held)
        for (int count : h)
            if (count)
                throw FileError("corrupt_events",
                                QCoreApplication::translate("OpenKeys", "Missing note-off"));
    for (bool active : pedal)
        if (active)
            throw FileError("corrupt_events",
                            QCoreApplication::translate("OpenKeys", "Missing pedal release"));
}
} // namespace piano
