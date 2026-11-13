#include "core/keyboard.h"
#include "core/language.h"
#include "core/model.h"
#include "fixtures.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <limits>
#include <set>
using namespace piano;
class CoreTests : public QObject {
    Q_OBJECT
    Language language_;
  private slots:
    void initTestCase() {
        QVERIFY(language_.load("zh_CN"));
    }
    void physicalKeyboardLayout() {
        const auto &keys = physicalKeys();
        std::set<uint16_t> codes;
        for (size_t i = 0; i < keys.size(); ++i) {
            const auto &key = keys[i];
            QVERIFY(codes.insert(key.code).second);
            QVERIFY(key.width > 0 && key.height > 0);
            const QRectF area(key.column, key.row, key.width, key.height);
            for (size_t j = i + 1; j < keys.size(); ++j) {
                const auto &other = keys[j];
                QVERIFY(!area.intersects(QRectF(other.column, other.row, other.width, other.height)));
            }
        }
        QCOMPARE(keyName(347), QStringLiteral("左 Win"));
        QCOMPARE(keyName(348), QStringLiteral("右 Win"));
        QCOMPARE(keyName(349), QStringLiteral("菜单"));
        QCOMPARE(scanFromQt(Qt::Key_unknown, 0), uint16_t(0));
        QCOMPARE(scanFromQt(Qt::Key_Return, 0), uint16_t(28));
        QCOMPARE(scanFromQt(Qt::Key_Enter, 0), uint16_t(284));
#ifdef Q_OS_LINUX
        QCOMPARE(scanFromQt(Qt::Key_Meta, 133), uint16_t(347));
        QCOMPARE(scanFromQt(Qt::Key_Meta, 134), uint16_t(348));
        QCOMPARE(scanFromQt(Qt::Key_Menu, 135), uint16_t(349));
        QCOMPARE(scanFromQt(Qt::Key_Plus, 86), uint16_t(78));
        QCOMPARE(scanFromQt(Qt::Key_Enter, 104), uint16_t(284));
#endif
#ifdef Q_OS_WIN
        for (auto code : {347, 348, 349, 78, 284, 28})
            QCOMPARE(scanFromQt(Qt::Key_unknown, code), uint16_t(code));
#endif
        auto map = Keymap::defaults();
        for (auto code : {0, 347, 348, 349})
            QVERIFY(!map.bindings.contains(code));
        for (auto code : {347, 348, 349})
            map.bindings[code] = {uint16_t(code), 60, Hand::Left, false};
        QTemporaryDir dir;
        const auto path = dir.filePath("system-keys.json");
        map.save(path);
        const auto copy = Keymap::read(path);
        for (auto code : {347, 348, 349})
            QCOMPARE(copy.bindings.at(code).note, 60);
    }
    void mergedKeyboardSources_data() {
        QTest::addColumn<int>("code");
        QTest::newRow("note") << 30;
        QTest::newRow("sustain") << 57;
        QTest::newRow("soft") << 42;
        QTest::newRow("hard") << 54;
    }
    void mergedKeyboardSources() {
        QFETCH(int, code);
        for (const auto first : {KeySource::Physical, KeySource::Mouse}) {
            const auto second = first == KeySource::Physical ? KeySource::Mouse : KeySource::Physical;
            for (const bool releaseFirst : {true, false}) {
                KeyHolds holds;
                KeyboardState state;
                Song recording;
                const auto change = [&](KeySource source, bool down) {
                    if (!holds.change(uint16_t(code), down, source))
                        return;
                    const auto events = state.change(uint16_t(code), down);
                    recording.events.insert(recording.events.end(), events.begin(), events.end());
                };
                change(first, true);
                const auto count = recording.events.size();
                change(first, true); // hardware repeat is not a second holder
                change(second, true);
                QCOMPARE(recording.events.size(), count);
                state.keymap.transpose = 7;
                state.keymap.octave[0] = 1;
                change(releaseFirst ? first : second, false);
                QCOMPARE(recording.events.size(), count);
                if (code == 42)
                    QVERIFY(state.soft);
                else if (code == 54)
                    QVERIFY(state.hard);
                else
                    QVERIFY(state.held(uint16_t(code)));
                change(releaseFirst ? second : first, false);
                QCOMPARE(recording.events.size(), count * 2);
                QVERIFY(state.releaseAll().empty());
                QVERIFY(!state.soft && !state.hard);
                validateSong(recording); // Includes matching original pitches and pedal releases.
                change(second, false);
                QCOMPARE(recording.events.size(), count * 2);
                holds.change(uint16_t(code), true, first);
                holds.change(uint16_t(code), true, second);
                holds.clear();
                QVERIFY(!holds.change(uint16_t(code), false, second));
                QVERIFY(holds.change(uint16_t(code), true, first));
            }
        }
    }
    void keyboardSnapshot() {
        KeyboardState state;
        auto on = state.change(30, true);
        QCOMPARE(on.size(), size_t(1));
        const auto pitch = on[0].note;
        QVERIFY(state.change(30, true).empty());
        state.keymap.transpose = 7;
        state.keymap.octave[0] = 1;
        auto off = state.change(30, false);
        QCOMPARE(off.size(), size_t(1));
        QCOMPARE(off[0].note, pitch);
        QCOMPARE(off[0].kind, Kind::NoteOff);
        QVERIFY(state.change(30, false).empty());
        state.change(30, true);
        state.change(31, true);
        QCOMPARE(state.releaseAll().size(), size_t(2));
        QVERIFY(state.releaseAll().empty());
    }
    void pedalAndVelocity() {
        KeyboardState state;
        state.keymap.bindings[58] = {58, 0, Hand::Unknown, true};
        auto first = state.change(57, true);
        QCOMPARE(first.size(), size_t(3));
        QVERIFY(state.change(58, true).empty());
        QVERIFY(state.change(57, false).empty());
        auto last = state.change(58, false);
        QCOMPARE(last.size(), size_t(3));
        for (const auto &e : last)
            QCOMPARE(e.velocity, 0);
        state.change(42, true);
        auto soft = state.change(30, true);
        state.change(30, false);
        state.change(42, false);
        state.change(54, true);
        auto hard = state.change(30, true);
        QVERIFY(soft[0].velocity < hard[0].velocity);
        state.keymap.bindings[31] = {31, 127, Hand::Right, false};
        state.keymap.transpose = 1;
        QVERIFY(state.change(31, true).empty());
    }
    void zeroVelocity() {
        KeyboardState state;
        const auto held = state.change(30, true);
        QVERIFY(!held.empty());
        state.keymap.velocity[0] = 0;
        for (bool hard : {false, true}) {
            state.change(54, hard);
            QVERIFY(state.change(31, true).empty());
            QVERIFY(state.held(31));
            QVERIFY(state.change(31, false).empty());
        }
        const auto released = state.change(30, false);
        QCOMPARE(released.size(), size_t(1));
        QCOMPARE(released[0].kind, Kind::NoteOff);
        QCOMPARE(released[0].note, held[0].note);
        QCOMPARE(state.change(57, true).size(), size_t(3));
        QCOMPARE(state.change(57, false).size(), size_t(3));
        QVERIFY(state.releaseAll().empty());
        QTemporaryDir dir;
        const auto path = dir.filePath("silent.json");
        state.keymap.save(path);
        QCOMPARE(Keymap::read(path).velocity[0], 0);
    }
    void keymapPersistence() {
        QTemporaryDir dir;
        auto path = dir.filePath("keys.json");
        auto map = Keymap::defaults();
        map.transpose = -3;
        map.bindings[16] = {16, 61, Hand::Right, false};
        map.save(path);
        auto copy = Keymap::read(path);
        QCOMPARE(copy.transpose, -3);
        QCOMPARE(copy.bindings.at(16).note, 61);
        QCOMPARE(copy.bindings.at(16).hand, Hand::Right);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"format\":\"openkeys-keymap\",\"version\":1.1}");
        file.close();
        QVERIFY_EXCEPTION_THROWN(Keymap::read(path), FileError);
    }
    void recordingRoundTrip() {
        QTemporaryDir dir;
        Song song;
        song.title = QStringLiteral("我的演奏");
        song.author = "Test";
        song.events = {{0, Kind::Sustain, 0, 127, Hand::Left, 57},
                       {12345, Kind::NoteOn, 60, 100, Hand::Left, 30},
                       {400000, Kind::NoteOff, 60, 0, Hand::Left, 30},
                       {500000, Kind::Sustain, 0, 0, Hand::Left, 57}};
        const auto path = dir.filePath("music.eopiano.json");
        saveRecording(path, song);
        const auto copy = readSong(path);
        QCOMPARE(copy.title, song.title);
        QCOMPARE(copy.events.size(), song.events.size());
        QCOMPARE(copy.events[1].timeUs, 12345);
        QCOMPARE(copy.events[1].key, 30);
        QCOMPARE(copy.durationUs(), 500000);
        auto bad = song;
        bad.events.pop_back();
        QVERIFY_EXCEPTION_THROWN(saveRecording(path, bad), FileError);
        QCOMPARE(readSong(path).events.size(), size_t(4));
    }
    void malformedRecordings() {
        for (const QByteArray input :
             {QByteArray("[]"), QByteArray("{}"), QByteArray("null"),
              QByteArray("{\"format\":\"openkeys-recording\",\"version\":1,\"events\":[{}]}")})
            QVERIFY_EXCEPTION_THROWN(readRecording(input), FileError);
        Song s;
        s.events = {{0, Kind::NoteOff, 60, 0, Hand::Left}};
        QVERIFY_EXCEPTION_THROWN(validateSong(s), FileError);
        s.events = {{-1, Kind::NoteOn, 60, 80, Hand::Left}, {20, Kind::NoteOff, 60, 0, Hand::Left}};
        QVERIFY_EXCEPTION_THROWN(validateSong(s), FileError);
    }
    void eopPercentageDynamics() {
        for (int percent : {0, 80, 100, 101}) {
            auto raw = fixture::decodedEop(301);
            fixture::u32(raw, 0x1bc + 12, percent);
            fixture::u32(raw, 0x1bc + 16, percent);
            auto song = parseEop(fixture::encode(raw));
            QCOMPARE(song.events[0].velocity, 90); // Per-event MIDI value is unchanged.
            if (percent > 100) {
                QVERIFY(!song.configurations[0].velocity[0]);
            } else {
                QCOMPARE(song.configurations[0].velocity[0].value(), (percent * 127 + 50) / 100);
                QTemporaryDir dir;
                const auto path = dir.filePath("percent.eopiano.json");
                saveRecording(path, song);
                QCOMPARE(readSong(path).configurations[0].velocity[0], song.configurations[0].velocity[0]);
            }
            raw = fixture::decodedEop(200);
            fixture::u32(raw, 0x1b8 + 12, percent);
            if (percent > 100) {
                QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(raw, true)), FileError);
            } else {
                song = parseEop(fixture::encode(raw, true));
                if (percent == 0)
                    QVERIFY(song.events.empty());
                else
                    QCOMPARE(song.events[0].velocity, (percent * 127 + 50) / 100);
            }
        }
    }
    void eopChangingKey() {
        const auto song = parseEop(fixture::changingKeyEop());
        QCOMPARE(song.events.size(), size_t(4));
        QCOMPARE(song.events[0].note, 63);
        QCOMPARE(song.events[1].note, 63);
        QCOMPARE(song.events[2].note, 65);
        QCOMPARE(song.events[3].note, 65);
        for (const auto &event : song.events)
            QCOMPARE(event.hand, Hand::Left); // Transpose and release retain the mapping's hand.
        QCOMPARE(song.configurations.size(), size_t(3));
        QCOMPARE(song.configurationAt(0)->transpose, 3);
        QCOMPARE(song.configurations[1].transpose, 4);
        QCOMPARE(song.configurationAt(100000)->transpose, 5);
        QCOMPARE(song.configurationAt(0)->octave[0].value(), -1);
        QCOMPARE(song.configurationAt(0)->velocity[0].value(), 51);
        QVERIFY(song.warnings.empty());
        QTemporaryDir dir;
        const auto path = dir.filePath("changed.eopiano.json");
        saveRecording(path, song);
        const auto copy = readSong(path);
        QCOMPARE(copy.configurationAt(100000)->transpose, 5);
        QCOMPARE(copy.events[2].note, 65);
        QCOMPARE(copy.configurationAt(0)->velocity[1].value(), 76);
        auto invalid = song;
        invalid.configurations.back().timeUs = -1;
        QVERIFY_EXCEPTION_THROWN(validateSong(invalid), FileError);
    }
    void eopVersions_data() {
        QTest::addColumn<int>("version");
        QTest::newRow("v200") << 200;
        QTest::newRow("v201") << 201;
        QTest::newRow("v301") << 301;
    }
    void eopVersions() {
        QFETCH(int, version);
        const auto song = parseEop(fixture::encode(fixture::decodedEop(version), version == 200));
        QCOMPARE(song.events.size(), size_t(2));
        QCOMPARE(song.events[0].note, 60);
        QCOMPARE(song.events[0].key, 30);
        QCOMPARE(song.events[1].timeUs, 500000);
        QCOMPARE(song.title, QStringLiteral("Fixture song"));
        if (version == 200) {
            QCOMPARE(song.events[0].velocity, 89);
            QCOMPARE(song.events[0].hand, Hand::Left);
        }
    }
    void eopLayeredAndZero() {
        for (int version : {201, 301}) {
            auto song = parseEop(fixture::encode(fixture::decodedEop(version, true)));
            QCOMPARE(song.events.size(), size_t(4));
            QCOMPARE(song.events[0].note, 60);
            QCOMPARE(song.events[1].note, 64);
        }
        auto song = parseEop(fixture::encode(fixture::decodedEop(201, false, true)));
        QCOMPARE(song.events.size(), size_t(2));
        song = parseEop(fixture::encode(fixture::decodedEop(201, true, true)));
        QCOMPARE(song.events.size(), size_t(4));
    }
    void eopHandMappings_data() {
        QTest::addColumn<int>("version");
        QTest::addColumn<bool>("zeroSection");
        QTest::addColumn<quint32>("firstFlag");
        QTest::addColumn<quint32>("secondFlag");
        QTest::addColumn<int>("firstHand");
        QTest::addColumn<int>("secondHand");
        for (int version : {201, 301}) {
            for (bool zero : {false, true}) {
                if (version == 301 && zero)
                    continue;
                for (quint32 flag : {0u, 1u, 2u, 0xffffffffu}) {
                    const auto name =
                        QString("v%1-%2-flag%3").arg(version).arg(zero ? "zero" : "prefixed").arg(flag);
                    const auto first = flag == 1 ? Hand::Left : flag == 0 ? Hand::Right : Hand::Unknown;
                    const auto second = flag == 1 ? Hand::Right : Hand::Left;
                    QTest::newRow(qPrintable(name))
                        << version << zero << flag << quint32(flag == 1 ? 0 : 1) << int(first) << int(second);
                }
            }
        }
    }
    void eopHandMappings() {
        QFETCH(int, version);
        QFETCH(bool, zeroSection);
        QFETCH(quint32, firstFlag);
        QFETCH(quint32, secondFlag);
        QFETCH(int, firstHand);
        QFETCH(int, secondHand);
        const auto song = parseEop(fixture::twoHandEop(version, zeroSection, firstFlag, secondFlag));
        QCOMPARE(song.events.size(), size_t(4));
        QVERIFY(song.warnings.empty());
        QTemporaryDir directory;
        const auto path = directory.filePath("hands.eopiano.json");
        saveRecording(path, song);
        const auto copy = readSong(path);
        for (size_t i = 0; i < song.events.size(); ++i) {
            const auto &event = song.events[i];
            QCOMPARE(int(event.hand), i % 2 == 0 ? firstHand : secondHand);
            QCOMPARE(event.note, i % 2 == 0 ? 60 : 64);
            QCOMPARE(event.key, i % 2 == 0 ? 30 : 79);
            QCOMPARE(event.kind, i < 2 ? Kind::NoteOn : Kind::NoteOff);
            QCOMPARE(event.timeUs, (std::array<int64_t, 4>{0, 100000, 200000, 500000})[i]);
            QCOMPARE(event.velocity, i == 0 ? 90 : i == 1 ? 80 : 0);
            QCOMPARE(copy.events[i].hand, event.hand);
        }
    }
    void eopLayerHands() {
        for (int version : {201, 301}) {
            for (bool zero : {false, true}) {
                if (version == 301 && zero)
                    continue;
                const qsizetype first = 0x1bc + 28;
                const qsizetype second = first + (version == 301 ? 52 : 42) + 4 + 28;
                for (bool samePitch : {false, true}) {
                    for (quint32 secondFlag : {0u, 1u, 2u}) {
                        auto data = fixture::decodedEop(version, true, zero);
                        fixture::u32(data, first + 8, 1);
                        fixture::u32(data, second + 8, secondFlag);
                        fixture::u16(data, second + 34, samePitch ? 60 : 64);
                        const auto song = parseEop(fixture::encode(data));
                        const bool duplicate = samePitch && secondFlag == 1;
                        QCOMPARE(song.events.size(), duplicate ? size_t(2) : size_t(4));
                        QCOMPARE(song.events[0].hand, Hand::Left);
                        QCOMPARE(song.events[0].note, 60);
                        if (!duplicate) {
                            QCOMPARE(song.events[1].hand, secondFlag == 0   ? Hand::Right
                                                          : secondFlag == 1 ? Hand::Left
                                                                            : Hand::Unknown);
                            QCOMPARE(song.events[1].note, samePitch ? 60 : 64);
                        }
                        const size_t count = song.events.size() / 2;
                        for (size_t i = 0; i < count; ++i) {
                            QCOMPARE(song.events[i + count].hand, song.events[i].hand);
                            QCOMPARE(song.events[i + count].note, song.events[i].note);
                            QCOMPARE(song.events[i + count].kind, Kind::NoteOff);
                        }
                    }
                }
            }
        }
    }
    void eopV200RightHand() {
        auto data = fixture::decodedEop(200);
        fixture::u32(data, 0x1b8 + 28 + (30 - 1) * 12 + 4, 1);
        const auto song = parseEop(fixture::encode(data, true));
        QCOMPARE(song.events.size(), size_t(2));
        QCOMPARE(song.events[0].hand, Hand::Right);
        QCOMPARE(song.events[1].hand, Hand::Right);
        QCOMPARE(song.events[0].velocity, 127);
    }
    void eopMappingOffsets() {
        for (int version : {201, 301}) {
            for (bool zeroSection : {false, true}) {
                if (version == 301 && zeroSection)
                    continue;
                for (const auto &[offset, expected] :
                     std::array<std::pair<int32_t, int>, 6>{{{0, 60},
                                                             {9, 63},
                                                             {-3, 63},
                                                             {12, 60},
                                                             {std::numeric_limits<int32_t>::min(), 68},
                                                             {std::numeric_limits<int32_t>::max(), 65}}}) {
                    auto data = fixture::decodedEop(version, false, zeroSection);
                    fixture::u32(data, 0x1bc, uint32_t(offset));
                    const auto song = parseEop(fixture::encode(data));
                    QCOMPARE(song.events.size(), size_t(2));
                    QCOMPARE(song.events[0].note, expected);
                    QCOMPARE(song.events[1].note, expected);
                    QCOMPARE(song.events[0].kind, Kind::NoteOn);
                    QCOMPARE(song.events[1].kind, Kind::NoteOff);
                    QCOMPARE(song.events[0].key, 30);
                    QCOMPARE(song.events[1].timeUs, 500000);

                    QTemporaryDir dir;
                    QVERIFY(dir.isValid());
                    const auto path = dir.filePath("offset.eopiano.json");
                    saveRecording(path, song);
                    const auto restored = readSong(path);
                    QCOMPARE(restored.events.size(), song.events.size());
                    QCOMPARE(restored.events[0].note, expected);
                    QCOMPARE(restored.events[1].note, expected);
                }
            }
        }
    }
    void eopLayerOffsets() {
        for (int version : {201, 301}) {
            for (bool zeroSection : {false, true}) {
                if (version == 301 && zeroSection)
                    continue;
                const qsizetype second = 0x1bc + 28 + (version == 201 ? 42 : 52) + 4;
                auto data = fixture::decodedEop(version, true, zeroSection);
                fixture::u32(data, 0x1bc, 9);
                fixture::u32(data, second, 3);
                auto song = parseEop(fixture::encode(data));
                QCOMPARE(song.events.size(), size_t(4));
                QCOMPARE(song.events[0].note, 63);
                QCOMPARE(song.events[1].note, 73);
                QCOMPARE(song.events[2].note, 63);
                QCOMPARE(song.events[3].note, 73);
                // Distinct base pitches become the same pitch after per-layer offsets.
                fixture::u32(data, 0x1bc, 3);
                fixture::u32(data, second, 7);
                song = parseEop(fixture::encode(data));
                QCOMPARE(song.events.size(), size_t(2));
                QCOMPARE(song.events[0].note, 69);
                QCOMPARE(song.events[1].note, 69);
            }
        }
    }
    void eopOffsetBounds() {
        for (int version : {201, 301}) {
            auto data = fixture::decodedEop(version);
            fixture::u32(data, 0x1bc, 9);
            const qsizetype note = 0x1bc + 28 + 34;
            fixture::u16(data, note, 124);
            auto song = parseEop(fixture::encode(data));
            QCOMPARE(song.events[0].note, 127);
            QCOMPARE(song.events[1].note, 127);
            fixture::u16(data, note, 125);
            QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(data)), FileError);
            fixture::u16(data, note, 0);
            fixture::u32(data, 0x1bc, 0);
            song = parseEop(fixture::encode(data));
            QCOMPARE(song.events[0].note, 0);
            QCOMPARE(song.events[1].note, 0);
        }
        auto data = fixture::decodedEop(200);
        fixture::u32(data, 0x1b8, 9);
        const auto song = parseEop(fixture::encode(data, true));
        QCOMPARE(song.events[0].note, 60);
        QCOMPARE(song.events[1].note, 60);
    }
    void eopMetadataAndExtendedKeys() {
        auto data = fixture::decodedEop(201);
        data.replace(0x134, 64, QByteArray(64, '\0'));
        data.replace(0x134, 4, QByteArray::fromHex("d6d0cec4"));
        const auto event = data.size() - 48;
        data[0x1b8 + 4 + 28 + 4] = char(0xc8);
        data[event + 9] = char(0xc8);
        data[event + 25] = char(0xc8);
        const auto song = parseEop(fixture::encode(data));
        QCOMPARE(song.title, QStringLiteral("中文"));
        QCOMPARE(song.events[0].key, 328);
        QCOMPARE(song.events[0].note, 60);
    }
    void eopFailures() {
        QVERIFY_EXCEPTION_THROWN(parseEop("not EOP"), FileError);
        const auto base = fixture::decodedEop(201);
        const qsizetype event = base.size() - 48;
        auto bad = base;
        fixture::u32(bad, 0x10, 999);
        QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(bad)), FileError);
        bad = base;
        fixture::real(bad, event, std::numeric_limits<double>::quiet_NaN());
        QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(bad)), FileError);
        bad = base;
        bad[event + 9] = 31;
        QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(bad)), FileError);
        bad = base;
        bad[event + 11] = 1;
        QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(bad)), FileError);
        bad = base;
        bad[event + 24] = char(0x90);
        QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(bad)), FileError);
        bad = base;
        fixture::u32(bad, 0x28, 0xffffffff);
        QVERIFY_EXCEPTION_THROWN(parseEop(fixture::encode(bad)), FileError);
        for (qsizetype n = 0; n < base.size(); ++n) {
            try {
                parseEop(fixture::encode(base.first(n)));
            } catch (const FileError &) {
            }
        }
    }
    void eopMutationSafety() {
        auto base = fixture::decodedEop(301, true);
        uint32_t seed = 123456;
        for (int i = 0; i < 3000; ++i) {
            auto bytes = base;
            seed = seed * 1664525u + 1013904223u;
            qsizetype at = qsizetype(seed % uint32_t(bytes.size()));
            bytes[at] = char(bytes[at] ^ char((seed >> 16) | 1));
            try {
                auto song = parseEop(fixture::encode(bytes));
                validateSong(song);
            } catch (const FileError &) {
            }
        }
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
