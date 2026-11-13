#include "core/model.h"
#include <QCoreApplication>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <deque>
#include <map>
#include <set>

namespace piano {
namespace {
// Independently implemented from the documented EOP container format.
// See docs/eop-compatibility.md for sources and deliberately unsupported semantics.
constexpr qsizetype Header = 0x1b8;
struct Reader {
    QByteArray data;
    void range(qsizetype p, qsizetype n) const {
        if (p < 0 || n < 0 || p > data.size() || n > data.size() - p)
            throw FileError("corrupt_eop",
                            QCoreApplication::translate("OpenKeys", "Truncated EOP container"));
    }
    uint8_t u8(qsizetype p) const {
        range(p, 1);
        return uint8_t(data[p]);
    }
    uint16_t u16(qsizetype p) const {
        range(p, 2);
        return qFromLittleEndian<uint16_t>(data.constData() + p);
    }
    uint32_t u32(qsizetype p) const {
        range(p, 4);
        return qFromLittleEndian<uint32_t>(data.constData() + p);
    }
    double real(qsizetype p) const {
        range(p, 8);
        return std::bit_cast<double>(qFromLittleEndian<uint64_t>(data.constData() + p));
    }
    bool zero(qsizetype p, qsizetype end) const {
        range(p, end - p);
        for (; p < end; ++p)
            if (data[p])
                return false;
        return true;
    }
};
struct MappedNote {
    uint8_t pitch;
    Hand hand;
    auto operator<=>(const MappedNote &) const = default;
};
struct Mapping {
    std::set<MappedNote> notes;
    int velocity = -1;
    bool control = false;
    int transposeDelta = 0;
};
using Map = std::map<uint8_t, Mapping>;
void merge(Map &to, const Map &from) {
    for (const auto &[key, map] : from) {
        auto &target = to[key];
        target.notes.insert(map.notes.begin(), map.notes.end());
        target.control = target.control || map.control;
        if (map.transposeDelta) {
            if (target.transposeDelta && target.transposeDelta != map.transposeDelta)
                throw FileError("unsupported_layout",
                                QCoreApplication::translate("OpenKeys", "Conflicting transpose actions"));
            target.transposeDelta = map.transposeDelta;
        }
    }
}
Map section(const Reader &r, qsizetype begin, qsizetype length, int stride) {
    r.range(begin, length);
    if (length < 28 || (length - 28) % stride || (length - 28) / stride > 256)
        throw FileError("unsupported_layout",
                        QCoreApplication::translate("OpenKeys", "Invalid mapping section size"));
    // Compatibility assumption: the first signed header word selects a pitch
    // class via its negative, normalized upward within one octave. See docs.
    const auto offset = std::bit_cast<int32_t>(r.u32(begin));
    const int shift = int((-int64_t(offset) % 12 + 12) % 12);
    Map result;
    for (qsizetype p = begin + 28; p < begin + length; p += stride) {
        const auto key = r.u8(p + 4);
        if (result.contains(key))
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Duplicate key in mapping section"));
        Mapping map;
        if (r.u16(p + 32) == 0x90) {
            const auto note = r.u16(p + 34);
            if (note > 127)
                throw FileError("corrupt_eop",
                                QCoreApplication::translate("OpenKeys", "Invalid mapped pitch"));
            const int pitch = int(note) + shift;
            if (pitch < 0 || pitch > 127)
                throw FileError("corrupt_eop",
                                QCoreApplication::translate(
                                    "OpenKeys", "EOP mapping offset produces an out-of-range pitch"));
            // Checked against the supplied v301 sample; v201 uses the same record prefix.
            const auto hand = r.u32(p + 8);
            map.notes.insert({uint8_t(pitch), hand == 1   ? Hand::Left
                                              : hand == 0 ? Hand::Right
                                                          : Hand::Unknown});
        } else {
            map.control = true;
            // Action identifiers are stored in the mapping, independently of the physical key.
            const auto action = r.u16(p + 32);
            if (action == 0x190 || action == 0x290)
                map.transposeDelta = action == 0x190 ? 1 : -1;
        }
        result.emplace(key, std::move(map));
    }
    return result;
}
Map prefixed(const Reader &r, qsizetype begin, qsizetype end, int stride) {
    r.range(begin, end - begin);
    Map result;
    int layers = 0;
    while (begin < end) {
        if (++layers > 32 || end - begin < 4)
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Invalid mapping layers"));
        const qsizetype length = r.u32(begin);
        begin += 4;
        if (length > end - begin)
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Mapping exceeds its container"));
        merge(result, section(r, begin, length, stride));
        begin += length;
    }
    if (!layers)
        throw FileError("unsupported_layout",
                        QCoreApplication::translate("OpenKeys", "Empty mapping container"));
    return result;
}
QString metadata(const Reader &r, qsizetype start) {
    auto bytes = r.data.mid(start, 64);
    const auto zero = bytes.indexOf('\0');
    if (zero >= 0)
        bytes.truncate(zero);
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (!utf8.hasError())
        return text;
    QStringDecoder gbk("GB18030");
    return gbk.isValid() ? gbk.decode(bytes) : QString::fromUtf8(bytes);
}
} // namespace
Song parseEop(const QByteArray &raw) {
    if (raw.size() < Header + 4 || raw.size() > MaxFileBytes)
        throw FileError("invalid_eop", QCoreApplication::translate("OpenKeys", "Invalid EOP file size"));
    Reader r{raw};
    for (qsizetype i = 0; i < r.data.size(); ++i) {
        const auto n = int(i % 32);
        const auto mask = uint8_t(0x71 + 2 * (n / 8) + (n % 8) / 4 + n % 4);
        r.data[i] = char(uint8_t(raw[i]) ^ mask);
    }
    if (r.data.first(13) != "EveryonePiano")
        throw FileError("invalid_eop",
                        QCoreApplication::translate("OpenKeys", "Signature is not EveryonePiano"));
    const auto version = r.u32(0x10);
    if (version != 200 && version != 201 && version != 301)
        throw FileError("unsupported_version", QString::number(version));
    Song song;
    song.title = metadata(r, 0x134);
    song.author = metadata(r, 0x174);
    Map mappings;
    qsizetype eventStart = 0, count = 0;
    if (version == 200) {
        if (raw.back() == 0)
            r.data.chop(1);
        const auto blocks = r.u32(0x20);
        if (blocks < 1 || blocks > 4)
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Invalid v200 block count"));
        const qsizetype base = Header + (blocks - 1) * 0x184;
        eventStart = base + 28 + 255 * 12 + 28;
        r.range(base, eventStart - base);
        const auto left = r.u32(base + 12), right = r.u32(base + 16);
        if (left > 100 || right > 100)
            throw FileError("corrupt_eop", QCoreApplication::translate("OpenKeys", "Invalid v200 dynamics"));
        for (int key = 1; key <= 255; ++key) {
            const qsizetype p = base + 28 + (key - 1) * 12;
            if (r.u32(p) != uint32_t(key))
                throw FileError("unsupported_layout",
                                QCoreApplication::translate("OpenKeys", "Invalid v200 mapping order"));
            Mapping m;
            const auto action = r.u32(p + 4);
            if (action == 1 || action == 0x10001) {
                const auto note = r.u32(p + 8);
                if (note > 127)
                    throw FileError("corrupt_eop",
                                    QCoreApplication::translate("OpenKeys", "Invalid v200 pitch"));
                m.notes.insert({uint8_t(note), action == 1 ? Hand::Right : Hand::Left});
                m.velocity = int(((action == 1 ? right : left) * 127 + 50) / 100);
            } else
                m.control = true;
            mappings.emplace(uint8_t(key), std::move(m));
        }
        if ((r.data.size() - eventStart) % 16 || r.data.size() - eventStart < 16)
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Invalid v200 event pool"));
        while (eventStart + count * 16 < r.data.size() && r.u8(eventStart + count * 16 + 8) != 0)
            ++count;
        if (eventStart + count * 16 == r.data.size() || !r.zero(eventStart + count * 16 + 8, r.data.size()))
            throw FileError("corrupt_eop", QCoreApplication::translate(
                                               "OpenKeys", "Missing v200 terminator or invalid padding"));
    } else {
        const int stride = version == 201 ? 42 : 52;
        const auto first = r.u32(Header);
        if (first) {
            qsizetype size = r.u32(0x28);
            if (!size)
                size = 4 + qsizetype(first);
            r.range(Header, size);
            eventStart = Header + size;
            mappings = prefixed(r, Header, eventStart, stride);
        } else if (version == 201) {
            const qsizetype mapSize = r.u32(0x28), eventSize = r.u32(0x2c);
            if (mapSize < 4 || eventSize < 16 || eventSize % 16 || eventSize > r.data.size())
                throw FileError("unsupported_layout",
                                QCoreApplication::translate("OpenKeys", "Invalid zero-section lengths"));
            eventStart = r.data.size() - eventSize;
            const qsizetype start = eventStart - (mapSize - 4);
            if (start < Header + 4)
                throw FileError("unsupported_layout",
                                QCoreApplication::translate("OpenKeys", "Invalid zero-section placement"));
            int candidates = 0;
            for (qsizetype length = 28; length <= eventStart - start && length <= 28 + 256 * 42;
                 length += 42) {
                try {
                    auto candidate = section(r, start, length, 42);
                    if (start + length < eventStart)
                        merge(candidate, prefixed(r, start + length, eventStart, 42));
                    mappings = std::move(candidate);
                    ++candidates;
                } catch (const FileError &) {
                }
            }
            if (candidates != 1)
                throw FileError(
                    "unsupported_layout",
                    QCoreApplication::translate("OpenKeys", "Ambiguous or unknown zero-section mapping"));
        } else
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Unknown v301 container variant"));
        if (eventStart > r.data.size() - 16 || (r.data.size() - eventStart - 16) % 16)
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Invalid event container length"));
        count = (r.data.size() - eventStart - 16) / 16; // verified container has one trailing record
    }
    if (count > qsizetype(MaxEvents))
        throw FileError("too_many_events",
                        QCoreApplication::translate("OpenKeys", "EOP event limit exceeded"));
    if (version != 200) {
        PerformanceConfig config;
        const qsizetype header = Header + 4;
        const auto offset = std::bit_cast<int32_t>(r.u32(header));
        config.transpose = int((-int64_t(offset) % 12 + 12) % 12);
        // The v301 sample carries per-hand octave and dynamics in these header words.
        if (version == 301) {
            for (size_t h = 0; h < 2; ++h) {
                const int octave = std::bit_cast<int32_t>(r.u32(header + 4 + qsizetype(h) * 4));
                const auto velocity = r.u32(header + 12 + qsizetype(h) * 4);
                if (octave >= -3 && octave <= 3)
                    config.octave[h] = octave;
                if (velocity <= 100)
                    config.velocity[h] = int((velocity * 127 + 50) / 100);
            }
        }
        song.configurations.push_back(config);
    }
    int transposeDelta = 0;
    std::map<uint8_t, std::deque<std::vector<Event>>> active;
    double previous = -1;
    bool controlWarning = false;
    for (qsizetype i = 0; i < count; ++i) {
        const qsizetype p = eventStart + i * 16;
        const double ms = r.real(p);
        if (!std::isfinite(ms) || ms < 0 || ms < previous || ms > double(MaxSongUs) / 1000)
            throw FileError("corrupt_eop",
                            QCoreApplication::translate("OpenKeys", "Invalid or decreasing timestamp"));
        previous = ms;
        const auto status = r.u8(p + 8), key = r.u8(p + 9), velocity = r.u8(p + 10);
        if ((status != 0x80 && status != 0x90) || velocity > 127 || !r.zero(p + 11, p + 16))
            throw FileError("corrupt_eop", QCoreApplication::translate("OpenKeys", "Invalid event record"));
        const auto found = mappings.find(key);
        if (found == mappings.end())
            throw FileError("unsupported_layout",
                            QCoreApplication::translate("OpenKeys", "Unmapped scan code 0x%1")
                                .arg(key, 2, 16, QChar('0')));
        const auto &m = found->second;
        if (m.control && m.transposeDelta && !song.configurations.empty()) {
            if (status == 0x90) {
                auto config = song.configurations.back();
                config.timeUs = int64_t(std::llround(ms * 1000));
                config.transpose = (config.transpose + m.transposeDelta + 12) % 12;
                transposeDelta = config.transpose - song.configurations.front().transpose;
                song.configurations.push_back(config);
            }
        } else if (m.control)
            controlWarning = true;
        if (status == 0x80 && !m.notes.empty()) {
            auto &held = active[key];
            if (held.empty())
                throw FileError("corrupt_eop",
                                QCoreApplication::translate("OpenKeys", "Unmatched key release"));
            for (auto e : held.front()) {
                e.timeUs = int64_t(std::llround(ms * 1000));
                e.kind = Kind::NoteOff;
                e.velocity = velocity;
                song.events.push_back(e);
            }
            held.pop_front();
            continue;
        }
        std::vector<Event> pressed;
        for (const auto &note : m.notes) {
            Event e;
            e.timeUs = int64_t(std::llround(ms * 1000));
            e.kind = status == 0x90 ? Kind::NoteOn : Kind::NoteOff;
            // EOP's one-byte extended Set-1 codes use bit 7; the application
            // represents E0 separately in bit 8 to match Windows Raw Input.
            e.key = key >= 128 ? uint16_t((key & 0x7f) | 0x100) : key;
            const int shifted = int(note.pitch) + transposeDelta;
            if (shifted < 0 || shifted > 127)
                throw FileError("corrupt_eop", QCoreApplication::translate(
                                                   "OpenKeys", "Transpose produces an out-of-range pitch"));
            e.note = uint8_t(shifted);
            e.hand = note.hand;
            e.velocity = uint8_t(e.kind == Kind::NoteOn && m.velocity >= 0 ? m.velocity : velocity);
            if (e.kind == Kind::NoteOn && m.velocity == 0)
                continue;
            song.events.push_back(e);
            pressed.push_back(e);
            if (song.events.size() > MaxEvents)
                throw FileError("too_many_events",
                                QCoreApplication::translate("OpenKeys", "Expanded EOP event limit exceeded"));
        }
        if (status == 0x90 && !m.notes.empty())
            active[key].push_back(std::move(pressed));
    }
    validateSong(song);
    if (controlWarning)
        song.warnings.append(QCoreApplication::translate(
            "OpenKeys", "This file contains unsupported control events. They were skipped; sustain or other "
                        "effects may differ from the original performance."));
    return song;
}
} // namespace piano
