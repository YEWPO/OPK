#include "core/keyboard.h"
#include "core/model.h"
#include "fixtures.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <limits>
using namespace piano;
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
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
            QCOMPARE(song.events[0].velocity, 70);
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
