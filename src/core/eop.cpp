#include "core/model.h"
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
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
            throw FileError("corrupt_eop", "Truncated EOP container");
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
struct Mapping {
    std::set<uint8_t> notes;
    Hand hand = Hand::Unknown;
    int velocity = -1;
    bool control = false;
};
using Map = std::map<uint8_t, Mapping>;
void merge(Map &to, const Map &from) {
    for (const auto &[key, map] : from) {
        auto &target = to[key];
        target.notes.insert(map.notes.begin(), map.notes.end());
        target.control = target.control || map.control;
    }
}
Map section(const Reader &r, qsizetype begin, qsizetype length, int stride) {
    r.range(begin, length);
    if (length < 28 || (length - 28) % stride || (length - 28) / stride > 256)
        throw FileError("unsupported_layout", "Invalid mapping section size");
    Map result;
    for (qsizetype p = begin + 28; p < begin + length; p += stride) {
        const auto key = r.u8(p + 4);
        if (result.contains(key))
            throw FileError("unsupported_layout", "Duplicate key in mapping section");
        Mapping map;
        if (r.u16(p + 32) == 0x90) {
            const auto note = r.u16(p + 34);
            if (note > 127)
                throw FileError("corrupt_eop", "Invalid mapped pitch");
            map.notes.insert(uint8_t(note));
        } else
            map.control = true;
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
            throw FileError("unsupported_layout", "Invalid mapping layers");
        const qsizetype length = r.u32(begin);
        begin += 4;
        if (length > end - begin)
            throw FileError("unsupported_layout", "Mapping exceeds its container");
        merge(result, section(r, begin, length, stride));
        begin += length;
    }
    if (!layers)
        throw FileError("unsupported_layout", "Empty mapping container");
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
        throw FileError("invalid_eop", "Invalid EOP file size");
    Reader r{raw};
    for (qsizetype i = 0; i < r.data.size(); ++i) {
        const auto n = int(i % 32);
        const auto mask = uint8_t(0x71 + 2 * (n / 8) + (n % 8) / 4 + n % 4);
        r.data[i] = char(uint8_t(raw[i]) ^ mask);
    }
    if (r.data.first(13) != "EveryonePiano")
        throw FileError("invalid_eop", "Signature is not EveryonePiano");
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
            throw FileError("unsupported_layout", "Invalid v200 block count");
        const qsizetype base = Header + (blocks - 1) * 0x184;
        eventStart = base + 28 + 255 * 12 + 28;
        r.range(base, eventStart - base);
        const auto left = r.u32(base + 12), right = r.u32(base + 16);
        if (left > 127 || right > 127)
            throw FileError("corrupt_eop", "Invalid v200 dynamics");
        for (int key = 1; key <= 255; ++key) {
            const qsizetype p = base + 28 + (key - 1) * 12;
            if (r.u32(p) != uint32_t(key))
                throw FileError("unsupported_layout", "Invalid v200 mapping order");
            Mapping m;
            const auto action = r.u32(p + 4);
            if (action == 1 || action == 0x10001) {
                const auto note = r.u32(p + 8);
                if (note > 127)
                    throw FileError("corrupt_eop", "Invalid v200 pitch");
                m.notes.insert(uint8_t(note));
                m.hand = action == 1 ? Hand::Right : Hand::Left;
                m.velocity = int(action == 1 ? right : left);
            } else
                m.control = true;
            mappings.emplace(uint8_t(key), std::move(m));
        }
        if ((r.data.size() - eventStart) % 16 || r.data.size() - eventStart < 16)
            throw FileError("unsupported_layout", "Invalid v200 event pool");
        while (eventStart + count * 16 < r.data.size() && r.u8(eventStart + count * 16 + 8) != 0)
            ++count;
        if (eventStart + count * 16 == r.data.size() || !r.zero(eventStart + count * 16 + 8, r.data.size()))
            throw FileError("corrupt_eop", "Missing v200 terminator or invalid padding");
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
                throw FileError("unsupported_layout", "Invalid zero-section lengths");
            eventStart = r.data.size() - eventSize;
            const qsizetype start = eventStart - (mapSize - 4);
            if (start < Header + 4)
                throw FileError("unsupported_layout", "Invalid zero-section placement");
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
                throw FileError("unsupported_layout", "Ambiguous or unknown zero-section mapping");
        } else
            throw FileError("unsupported_layout", "Unknown v301 container variant");
        if (eventStart > r.data.size() - 16 || (r.data.size() - eventStart - 16) % 16)
            throw FileError("unsupported_layout", "Invalid event container length");
        count = (r.data.size() - eventStart - 16) / 16; // verified container has one trailing record
    }
    if (count > qsizetype(MaxEvents))
        throw FileError("too_many_events", "EOP event limit exceeded");
    double previous = -1;
    bool controlWarning = false;
    for (qsizetype i = 0; i < count; ++i) {
        const qsizetype p = eventStart + i * 16;
        const double ms = r.real(p);
        if (!std::isfinite(ms) || ms < 0 || ms < previous || ms > double(MaxSongUs) / 1000)
            throw FileError("corrupt_eop", "Invalid or decreasing timestamp");
        previous = ms;
        const auto status = r.u8(p + 8), key = r.u8(p + 9), velocity = r.u8(p + 10);
        if ((status != 0x80 && status != 0x90) || velocity > 127 || !r.zero(p + 11, p + 16))
            throw FileError("corrupt_eop", "Invalid event record");
        const auto found = mappings.find(key);
        if (found == mappings.end())
            throw FileError("unsupported_layout",
                            QString("Unmapped scan code 0x%1").arg(key, 2, 16, QChar('0')));
        const auto &m = found->second;
        if (m.control)
            controlWarning = true;
        for (auto pitch : m.notes) {
            Event e;
            e.timeUs = int64_t(std::llround(ms * 1000));
            e.kind = status == 0x90 ? Kind::NoteOn : Kind::NoteOff;
            // EOP's one-byte extended Set-1 codes use bit 7; the application
            // represents E0 separately in bit 8 to match Windows Raw Input.
            e.key = key >= 128 ? uint16_t((key & 0x7f) | 0x100) : key;
            e.note = pitch;
            e.hand = m.hand;
            e.velocity = uint8_t(e.kind == Kind::NoteOn && m.velocity >= 0 ? m.velocity : velocity);
            song.events.push_back(e);
            if (song.events.size() > MaxEvents)
                throw FileError("too_many_events", "Expanded EOP event limit exceeded");
        }
    }
    validateSong(song);
    if (controlWarning)
        song.warnings.append(
            QStringLiteral("文件包含尚未解释的控制键事件；这些事件未执行，延音或其他效果可能与原软件不同。"));
    return song;
}
} // namespace piano
