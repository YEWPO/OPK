#pragma once
#include <QByteArray>
#include <QtEndian>
#include <bit>
#include <cmath>
namespace fixture {
inline void u16(QByteArray &b, qsizetype p, uint16_t n) {
    qToLittleEndian(n, b.data() + p);
}
inline void u32(QByteArray &b, qsizetype p, uint32_t n) {
    qToLittleEndian(n, b.data() + p);
}
inline void real(QByteArray &b, qsizetype p, double n) {
    qToLittleEndian(std::bit_cast<uint64_t>(n), b.data() + p);
}
inline QByteArray encode(QByteArray data, bool v200 = false) {
    for (qsizetype i = 0; i < data.size(); ++i) {
        int n = int(i % 32);
        data[i] = char(uint8_t(data[i]) ^ uint8_t(0x71 + 2 * (n / 8) + (n % 8) / 4 + n % 4));
    }
    if (v200)
        data.append('\0');
    return data;
}
inline QByteArray decodedEop(int version, bool layered = false, bool zeroSection = false) {
    constexpr int header = 0x1b8;
    const int stride = version == 301 ? 52 : 42;
    const int sectionSize = 28 + stride;
    const int mapSize =
        version == 200 ? 28 + 255 * 12 + 28 : 4 + sectionSize + (layered ? 4 + sectionSize : 0);
    const int start = header + mapSize;
    QByteArray b(start + 48, '\0');
    b.replace(0, 13, "EveryonePiano");
    u32(b, 0x10, uint32_t(version));
    u32(b, 0x1c, 120);
    b.replace(0x134, 12, "Fixture song");
    if (version == 200) {
        u32(b, 0x20, 1);
        u32(b, header + 12, 70);
        u32(b, header + 16, 100);
        for (int i = 1; i <= 255; ++i) {
            int p = header + 28 + (i - 1) * 12;
            u32(b, p, uint32_t(i));
            if (i == 30) {
                u32(b, p + 4, 0x10001);
                u32(b, p + 8, 60);
            }
        }
    } else {
        u32(b, 0x28, uint32_t(mapSize));
        u32(b, 0x2c, 48);
        if (!zeroSection)
            u32(b, header, uint32_t(sectionSize));
        int p = header + 4 + 28;
        b[p + 4] = 30;
        u16(b, p + 32, 0x90);
        u16(b, p + 34, 60);
        if (layered) {
            u32(b, header + 4 + sectionSize, uint32_t(sectionSize));
            p = header + 4 + sectionSize + 4 + 28;
            b[p + 4] = 30;
            u16(b, p + 32, 0x90);
            u16(b, p + 34, 64);
        }
    }
    real(b, start, 0);
    b[start + 8] = char(0x90);
    b[start + 9] = 30;
    b[start + 10] = version == 200 ? 0 : 90;
    real(b, start + 16, 500);
    b[start + 24] = char(0x80);
    b[start + 25] = 30;
    return b;
}
inline QByteArray changingKeyEop() {
    auto b = decodedEop(301);
    constexpr int header = 0x1bc, record = header + 28, oldStart = record + 52;
    // Remap the transpose-up action to scan code 31, not F1.
    b.insert(oldStart, QByteArray(52, '\0'));
    u32(b, 0x1b8, 28 + 104);
    u32(b, 0x28, 4 + 28 + 104);
    u32(b, header, 9);
    u32(b, header + 4, uint32_t(-1));
    u32(b, header + 12, 40);
    u32(b, header + 16, 60);
    u32(b, record + 8, 1);
    b[oldStart + 4] = 31;
    u16(b, oldStart + 32, 0x190);
    const int start = oldStart + 52;
    b.resize(start);
    const auto event = [&](double time, int status, int key, int velocity) {
        QByteArray e(16, '\0');
        real(e, 0, time);
        e[8] = char(status);
        e[9] = char(key);
        e[10] = char(velocity);
        b.append(e);
    };
    event(0, 0x90, 30, 90);
    event(100, 0x90, 31, 0);
    event(100, 0x90, 31, 0);
    event(150, 0x80, 31, 0);
    event(150, 0x80, 31, 0);
    event(200, 0x80, 30, 0); // Must release Eb, despite current F configuration.
    event(300, 0x90, 30, 90);
    event(500, 0x80, 30, 0);
    b.append(QByteArray(16, '\0'));
    u32(b, 0x2c, 9 * 16);
    return encode(b);
}
inline QByteArray twoHandEop(int version = 301, bool zeroSection = false, uint32_t firstHand = 1,
                             uint32_t secondHand = 0) {
    const int stride = version == 301 ? 52 : 42;
    constexpr int header = 0x1bc, record = header + 28;
    const int sectionSize = 28 + 2 * stride, start = header + sectionSize;
    // Original synthetic mapping and timing, using the hand field from the local sample.
    QByteArray b(start, '\0');
    b.replace(0, 13, "EveryonePiano");
    u32(b, 0x10, version);
    u32(b, 0x1b8, zeroSection ? 0 : sectionSize);
    u32(b, 0x28, 4 + sectionSize);
    u32(b, 0x2c, 5 * 16);
    b[record + 4] = 30;
    u32(b, record + 8, firstHand);
    u16(b, record + 32, 0x90);
    u16(b, record + 34, 60);
    b[record + stride + 4] = 79;
    u32(b, record + stride + 8, secondHand);
    u16(b, record + stride + 32, 0x90);
    u16(b, record + stride + 34, 64);
    const auto event = [&](double time, int status, int key, int velocity) {
        QByteArray e(16, '\0');
        real(e, 0, time);
        e[8] = char(status);
        e[9] = char(key);
        e[10] = char(velocity);
        b.append(e);
    };
    event(0, 0x90, 30, 90);
    event(100, 0x90, 79, 80);
    event(200, 0x80, 30, 0);
    event(500, 0x80, 79, 0);
    b.append(QByteArray(16, '\0'));
    return encode(b);
}
inline QByteArray chunk(const char *id, QByteArray content) {
    if (content.size() % 2)
        content += '\0';
    QByteArray b(id, 4);
    QByteArray size(4, '\0');
    u32(size, 0, uint32_t(content.size()));
    b += size;
    b += content;
    return b;
}
inline QByteArray list(const char *type, const QByteArray &content) {
    return chunk("LIST", QByteArray(type, 4) + content);
}
inline QByteArray testSoundfont() {
    // Original generated sine fixture, exclusively for exercising the sampler.
    constexpr int count = 22050;
    QByteArray pcm((count + 46) * 2, '\0');
    for (int i = 0; i < count; ++i) {
        const double sample =
            std::sin(2 * 3.141592653589793 * 440 * i / 22050.) * .35 * std::exp(-double(i) / 22050.);
        u16(pcm, i * 2, uint16_t(int16_t(sample * 32767)));
    }
    QByteArray phdr(76, '\0');
    phdr.replace(0, 7, "Fixture");
    u16(phdr, 24, 0);
    phdr.replace(38, 3, "EOP");
    u16(phdr, 38 + 24, 1);
    QByteArray pbag(8, '\0');
    u16(pbag, 4, 1);
    QByteArray pgen(8, '\0');
    u16(pgen, 0, 41);
    QByteArray inst(44, '\0');
    inst.replace(0, 7, "Fixture");
    inst.replace(22, 3, "EOI");
    u16(inst, 42, 1);
    QByteArray ibag(8, '\0');
    u16(ibag, 4, 3);
    QByteArray igen(16, '\0');
    u16(igen, 0, 58);
    u16(igen, 2, 69);
    u16(igen, 4, 38);
    u16(igen, 6, uint16_t(int16_t(-2400)));
    u16(igen, 8, 53);
    QByteArray shdr(92, '\0');
    shdr.replace(0, 7, "Fixture");
    u32(shdr, 24, count);
    u32(shdr, 28, 10);
    u32(shdr, 32, count - 10);
    u32(shdr, 36, 22050);
    shdr[40] = 69;
    u16(shdr, 44, 1);
    shdr.replace(46, 3, "EOS");
    u32(shdr, 46 + 20, count + 46);
    u32(shdr, 46 + 24, count + 46);
    const auto pdta = chunk("phdr", phdr) + chunk("pbag", pbag) + chunk("pmod", QByteArray(10, '\0')) +
                      chunk("pgen", pgen) + chunk("inst", inst) + chunk("ibag", ibag) +
                      chunk("imod", QByteArray(10, '\0')) + chunk("igen", igen) + chunk("shdr", shdr);
    QByteArray version(4, '\0');
    u16(version, 0, 2);
    u16(version, 2, 1);
    return chunk("RIFF", QByteArray("sfbk", 4) +
                             list("INFO", chunk("ifil", version) + chunk("isng", QByteArray("EMU8000\0", 8)) +
                                              chunk("INAM", QByteArray("Generated test fixture\0", 23))) +
                             list("sdta", chunk("smpl", pcm)) + list("pdta", pdta));
}
} // namespace fixture
