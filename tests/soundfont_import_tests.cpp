#include "audio/soundfont_import.h"
#include "fixtures.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
using namespace piano;
namespace {
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw std::runtime_error("fixture write failed");
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("fixture read failed");
    return file.readAll();
}
} // namespace
class SoundfontImportTests : public QObject {
    Q_OBJECT
  private slots:
    void copySurvivesSourceRemovalAndDuplicateImport() {
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("下载 钢琴.SF2"));
        const auto store = dir.filePath("managed");
        const auto bank = fixture::testSoundfont();
        write(source, bank);
        std::vector<ImportPhase> phases;
        auto first = importSoundfont(source, store, 48000, [&](ImportPhase phase, int percent) {
            Q_UNUSED(percent);
            phases.push_back(phase);
            return true;
        });
        QVERIFY2(bool(first.font), qPrintable(first.error));
        QCOMPARE(first.info.id.size(), 64);
        QCOMPARE(first.info.name, QStringLiteral("下载 钢琴"));
        QVERIFY(first.info.path != source);
        QCOMPARE(read(source), bank);
        QCOMPARE(read(first.info.path), bank);
        QCOMPARE(readImportedSoundfont(store).id, first.info.id);
        QVERIFY(std::find(phases.begin(), phases.end(), ImportPhase::Copying) != phases.end());
        QVERIFY(std::find(phases.begin(), phases.end(), ImportPhase::Loading) != phases.end());
        auto duplicate = importSoundfont(source, store, 44100);
        QVERIFY2(bool(duplicate.font), qPrintable(duplicate.error));
        QCOMPARE(duplicate.info.path, first.info.path);
        QCOMPARE(QDir(store).entryList({"*.sf2"}, QDir::Files).size(), 1);
        QVERIFY(QFile::remove(source));
        const auto saved = readImportedSoundfont(store);
        auto reopened = AudioEngine::loadSoundfont(saved.path);
        QVERIFY2(bool(reopened.font), qPrintable(reopened.error));
        AudioEngine engine;
        engine.setSoundfont(reopened.font);
        engine.send({0, Kind::NoteOn, 69, 100, Hand::Right});
        std::array<float, 2048> samples{};
        engine.render(samples.data(), 1024);
        QVERIFY(std::any_of(samples.begin(), samples.end(), [](float sample) { return sample != 0; }));
    }
    void replacementCleansOnlyOldOwnedCopy() {
        QTemporaryDir dir;
        const auto store = dir.filePath("managed"), one = dir.filePath("one.sf2"),
                   two = dir.filePath("two.sf2");
        auto bank = fixture::testSoundfont();
        write(one, bank);
        bank[bank.indexOf("Fixture")] = 'N';
        write(two, bank);
        auto first = importSoundfont(one, store, 48000);
        QVERIFY(bool(first.font));
        AudioEngine engine;
        engine.setSoundfont(std::move(first.font));
        auto second = importSoundfont(two, store, 48000);
        QVERIFY2(bool(second.font), qPrintable(second.error));
        QCOMPARE(second.previousId, first.info.id);
        QVERIFY(QFile::exists(first.info.path));
        engine.setSoundfont(std::move(second.font));
        QVERIFY(removeOldSoundfont(store, second.previousId, second.info.id));
        QVERIFY(!QFile::exists(first.info.path));
        QVERIFY(QFile::exists(second.info.path));
        QVERIFY(QFile::exists(one));
        QVERIFY(QFile::exists(two));
        QVERIFY(removeOldSoundfont(store, "../../one", second.info.id));
        QVERIFY(removeOldSoundfont(store, second.info.id, second.info.id));
        QVERIFY(QFile::exists(second.info.path));
    }
    void failureAndCancellationPreserveExistingBank() {
        QTemporaryDir dir;
        const auto store = dir.filePath("managed"), source = dir.filePath("good.sf2");
        const auto bank = fixture::testSoundfont();
        write(source, bank);
        auto first = importSoundfont(source, store, 48000);
        QVERIFY(bool(first.font));
        const auto settings = read(QDir(store).filePath("current.json"));
        const auto bad = dir.filePath("bad.sf2");
        write(bad, "not an sf2");
        const auto rejected = importSoundfont(bad, store, 48000);
        QVERIFY(!rejected.font);
        QVERIFY(!rejected.error.isEmpty());
        QCOMPARE(read(QDir(store).filePath("current.json")), settings);
        const auto zip = dir.filePath("bank.zip");
        write(zip, bank);
        QVERIFY(!importSoundfont(zip, store, 48000).font);
        auto other = bank;
        other[other.indexOf("Fixture")] = 'X';
        const auto newSource = dir.filePath("new.sf2");
        write(newSource, other);
        for (auto cancelAt : {ImportPhase::Copying, ImportPhase::Loading}) {
            const auto cancelled =
                importSoundfont(newSource, store, 48000, [cancelAt](ImportPhase phase, int percent) {
                    return phase != cancelAt || percent < 100;
                });
            QVERIFY(!cancelled.font);
            QCOMPARE(cancelled.errorCode, QStringLiteral("import_cancelled"));
            QCOMPARE(read(QDir(store).filePath("current.json")), settings);
            QCOMPARE(QDir(store).entryList({"*.sf2"}, QDir::Files).size(), 1);
            QVERIFY(QDir(store).entryList({".import-*"}, QDir::Files | QDir::Hidden).isEmpty());
        }
        // A valid outer RIFF with invalid sampler contents must not be adopted.
        auto broken = bank;
        auto type = broken.indexOf("ifil");
        QVERIFY(type > 0);
        fixture::u16(broken, type + 8, 9);
        write(bad, broken);
        const auto invalid = importSoundfont(bad, store, 48000);
        QVERIFY(!invalid.font);
        QCOMPARE(read(QDir(store).filePath("current.json")), settings);
        QCOMPARE(QDir(store).entryList({"*.sf2"}, QDir::Files).size(), 1);
        QVERIFY(QFile::exists(first.info.path));
    }
    void multiBlockCopyAndMidCopyCancellation() {
        QTemporaryDir directory;
        auto bank = fixture::testSoundfont();
        const auto samples = bank.indexOf("smpl"), sdta = bank.indexOf("sdta");
        QVERIFY(samples > 0 && sdta > 0);
        const auto sampleBytes = qFromLittleEndian<quint32>(bank.constData() + samples + 4);
        const auto listBytes = qFromLittleEndian<quint32>(bank.constData() + sdta - 4);
        constexpr int extra = 8 * 1024 * 1024;
        bank.insert(samples + 8 + sampleBytes, QByteArray(extra, '\0'));
        fixture::u32(bank, samples + 4, sampleBytes + extra);
        fixture::u32(bank, sdta - 4, listBytes + extra);
        fixture::u32(bank, 4, quint32(bank.size() - 8));
        const auto source = directory.filePath("multi-block.sf2"), store = directory.filePath("managed");
        write(source, bank);
        std::vector<int> percentages;
        auto result = importSoundfont(source, store, 48000, [&](ImportPhase phase, int percent) {
            if (phase == ImportPhase::Copying)
                percentages.push_back(percent);
            return true;
        });
        QVERIFY2(bool(result.font), qPrintable(result.error));
        QVERIFY(percentages.size() > 3);
        QVERIFY(std::is_sorted(percentages.begin(), percentages.end()));
        QCOMPARE(percentages.back(), 100);
        QCOMPARE(read(result.info.path), bank);
        const auto originalSettings = read(QDir(store).filePath("current.json"));
        bank[bank.indexOf("Fixture")] = 'Z';
        write(source, bank);
        auto cancelled = importSoundfont(source, store, 48000, [](ImportPhase phase, int percent) {
            return phase != ImportPhase::Copying || percent < 25;
        });
        QCOMPARE(cancelled.errorCode, QStringLiteral("import_cancelled"));
        QCOMPARE(read(QDir(store).filePath("current.json")), originalSettings);
        QCOMPARE(QDir(store).entryList({"*.sf2"}, QDir::Files).size(), 1);
        QVERIFY(QDir(store).entryList({".import-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void storageErrorsAndUntrustedMetadata() {
        QTemporaryDir dir;
        auto source = dir.filePath("bank.sf2");
        write(source, fixture::testSoundfont());
        const auto notDirectory = dir.filePath("file");
        write(notDirectory, "keep");
        QVERIFY(!importSoundfont(source, notDirectory, 48000).font);
        QCOMPARE(read(notDirectory), QByteArray("keep"));
        const auto store = dir.filePath("managed");
        QVERIFY(QDir().mkpath(QDir(store).filePath("current.json")));
        const auto failed = importSoundfont(source, store, 48000);
        QVERIFY(!failed.font);
        QCOMPARE(failed.errorCode, QStringLiteral("import_settings_failed"));
        QVERIFY(QDir(store).entryList({"*.sf2"}, QDir::Files).isEmpty());
        const auto invalidStore = dir.filePath("invalid");
        QVERIFY(QDir().mkpath(invalidStore));
        write(
            QDir(invalidStore).filePath("current.json"),
            QJsonDocument(QJsonObject{{"version", 1}, {"id", "../../source"}, {"name", "unsafe"}}).toJson());
        QVERIFY_EXCEPTION_THROWN(readImportedSoundfont(invalidStore), FileError);
        auto recovered = importSoundfont(source, invalidStore, 48000);
        QVERIFY(bool(recovered.font));
        QCOMPARE(readImportedSoundfont(invalidStore).id, recovered.info.id);
    }
};
QTEST_GUILESS_MAIN(SoundfontImportTests)
#include "soundfont_import_tests.moc"
