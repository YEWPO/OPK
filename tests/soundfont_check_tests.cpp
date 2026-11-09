#include "audio/soundfont_check.h"
#include "fixtures.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
using namespace piano;
namespace {
QString checker() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/openkeys-soundfont-check.exe";
#else
    return QCoreApplication::applicationDirPath() + "/openkeys-soundfont-check";
#endif
}
} // namespace
class SoundfontCheckTests : public QObject {
    Q_OBJECT
  private slots:
    void rendersAndReportsWithoutDevice() {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("测试.sf2"));
        QFile font(path);
        QVERIFY(font.open(QIODevice::WriteOnly));
        font.write(fixture::testSoundfont());
        font.close();
        QProcess process;
        process.start(checker(), {"--soundfont", path, "--output", dir.filePath("result")});
        QVERIFY(process.waitForFinished(45000));
        const auto standardError = process.readAllStandardError();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, standardError.constData());
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        const auto report = document.object();
        QVERIFY(report.value("passed").toBool());
        QVERIFY(!report.value("physicalAudioDeviceTested").toBool());
        QCOMPARE(report.value("soundfontSha256").toString().size(), 64);
        const auto rates = report.value("rates").toArray();
        QCOMPARE(rates.size(), 2);
        for (const auto &item : rates) {
            const auto rate = item.toObject();
            const int sampleRate = rate.value("sampleRate").toInt();
            QVERIFY(sampleRate == 44100 || sampleRate == 48000);
            QVERIFY(rate.value("passed").toBool());
            QCOMPARE(rate.value("notes").toArray().size(), 88 * 3);
            QCOMPARE(rate.value("nonFiniteSamples").toInteger(), 0);
            QCOMPARE(rate.value("renderFailures").toInteger(), 0);
            for (const auto &kind : {"demo", "sustain"}) {
                QFile wave(dir.filePath(QString("result/%1-%2.wav").arg(kind).arg(sampleRate)));
                QVERIFY(wave.open(QIODevice::ReadOnly));
                const auto bytes = wave.readAll();
                QCOMPARE(bytes.first(4), QByteArray("RIFF"));
                QCOMPARE(bytes.mid(8, 4), QByteArray("WAVE"));
                QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + 20), quint16(1));
                QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + 22), quint16(2));
                QCOMPARE(qFromLittleEndian<quint32>(bytes.constData() + 24), quint32(sampleRate));
                QCOMPARE(qFromLittleEndian<quint32>(bytes.constData() + 40), quint32(bytes.size() - 44));
                QCOMPARE(qFromLittleEndian<quint32>(bytes.constData() + 4), quint32(bytes.size() - 8));
                bool audible = false;
                for (qsizetype p = 44; p < bytes.size(); ++p)
                    if (bytes[p]) {
                        audible = true;
                        break;
                    }
                QVERIFY(audible);
            }
        }
        QFile saved(dir.filePath("result/report.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object(), report);
        // Restrict the SF2 to one key: residual effect tails must not make a
        // missing range pass. The checker also requires active sample voices.
        auto limited = fixture::testSoundfont();
        const auto generator = limited.indexOf("igen");
        QVERIFY(generator > 0);
        fixture::u16(limited, generator + 8, 43);
        fixture::u16(limited, generator + 10, quint16(69 | (69 << 8)));
        QVERIFY(font.open(QIODevice::WriteOnly | QIODevice::Truncate));
        font.write(limited);
        font.close();
        process.start(checker(), {"--soundfont", path, "--output", dir.filePath("limited")});
        QVERIFY(process.waitForFinished(45000));
        QCOMPARE(process.exitCode(), 1);
        const auto failed = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QVERIFY(!failed.value("passed").toBool());
        QVERIFY(!failed.value("rates").toArray()[0].toObject().value("errors").toArray().isEmpty());
    }
    void invalidSourceAndMissingArguments() {
        QTemporaryDir dir;
        QProcess process;
        process.start(checker(),
                      {"--soundfont", dir.filePath("missing.sf2"), "--output", dir.filePath("absent")});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 2);
        QVERIFY(!QFileInfo::exists(dir.filePath("absent/report.json")));
        process.start(checker(), QStringList{});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 2);
        QVERIFY(process.readAllStandardError().contains("--soundfont"));
        const auto bad = dir.filePath("bad.sf2");
        QFile file(bad);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("broken");
        file.close();
        process.start(checker(), {"--soundfont", bad, "--output", dir.filePath("failed")});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 2);
        QFile report(dir.filePath("failed/report.json"));
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto failed = QJsonDocument::fromJson(report.readAll()).object();
        QVERIFY(!failed.value("passed").toBool());
        QCOMPARE(failed.value("state").toString(), QStringLiteral("failed"));
        const auto protectedPath = dir.filePath("report.json");
        QFile protectedFont(protectedPath);
        QVERIFY(protectedFont.open(QIODevice::WriteOnly));
        const auto original = fixture::testSoundfont();
        protectedFont.write(original);
        protectedFont.close();
        process.start(checker(), {"--soundfont", protectedPath, "--output", dir.path()});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 2);
        QVERIFY(protectedFont.open(QIODevice::ReadOnly));
        QCOMPARE(protectedFont.readAll(), original);
    }
};
QTEST_GUILESS_MAIN(SoundfontCheckTests)
#include "soundfont_check_tests.moc"
