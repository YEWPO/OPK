#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace piano {
enum class Kind : uint8_t { NoteOn, NoteOff, Sustain, Panic };
enum class Hand : uint8_t { Left, Right, Unknown };
// Time is relative to a song/recording, in microseconds. Key is a Set-1 scan code
// with bit 8 indicating E0; zero means unavailable. Imported pitches are absolute.
struct Event {
    int64_t timeUs = 0;
    Kind kind = Kind::NoteOn;
    uint8_t note = 60;
    uint8_t velocity = 80;
    Hand hand = Hand::Unknown;
    uint16_t key = 0;
    uint8_t source = 0; // 0 live, 1 playback; not serialized
};
struct Song {
    QString title;
    QString author;
    std::vector<Event> events;
    QStringList warnings;
    int64_t durationUs() const {
        return events.empty() ? 0 : events.back().timeUs;
    }
};
class FileError : public std::runtime_error {
  public:
    QString code;
    QString detail;
    FileError(QString category, QString description)
        : std::runtime_error((category + ": " + description).toStdString()), code(std::move(category)),
          detail(std::move(description)) {}
};
constexpr int64_t MaxSongUs = 24LL * 60 * 60 * 1000000;
constexpr qsizetype MaxFileBytes = 64 * 1024 * 1024;
constexpr size_t MaxEvents = 1000000;
QString noteName(int note);
void validateSong(const Song &song);
Song readSong(const QString &path);
Song readRecording(const QByteArray &data);
void saveRecording(const QString &path, const Song &song);
Song parseEop(const QByteArray &raw);
QByteArray readBounded(const QString &path);
} // namespace piano
