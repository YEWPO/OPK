#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto soundfont = qEnvironmentVariable("OPENKEYS_TEST_SOUNDFONT");
    if (soundfont.isEmpty()) {
        std::fprintf(stderr,
                     "SKIPPED: OPENKEYS_TEST_SOUNDFONT is not set; no real piano bank has been tested.\n");
        return 77;
    }
    auto output = qEnvironmentVariable("OPENKEYS_TEST_OUTPUT");
    if (output.isEmpty())
        output = QDir::current().filePath("real-soundfont-artifacts");
    QString tool = QCoreApplication::applicationDirPath() + "/openkeys-soundfont-check";
#ifdef Q_OS_WIN
    tool += ".exe";
#endif
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start(tool, {"--soundfont", soundfont, "--output", output});
    if (!process.waitForStarted(10000)) {
        std::fprintf(stderr, "Could not start soundfont checker\n");
        return 2;
    }
    if (!process.waitForFinished(300000)) {
        process.kill();
        process.waitForFinished();
        std::fprintf(stderr, "Real soundfont check timed out\n");
        return 2;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::fprintf(stderr, "Real soundfont check failed (exit %d); output: %s\n", process.exitCode(),
                     qPrintable(output));
        return 1;
    }
    QJsonParseError error;
    const auto report = QJsonDocument::fromJson(process.readAllStandardOutput(), &error).object();
    if (error.error != QJsonParseError::NoError || !report.value("passed").toBool() ||
        report.value("rates").toArray().size() != 2) {
        std::fprintf(stderr, "Checker returned an invalid report\n");
        return 1;
    }
    QFile saved(QDir(output).filePath("report.json"));
    if (!saved.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(saved.readAll()).object() != report) {
        std::fprintf(stderr, "Saved check report is missing or inconsistent\n");
        return 1;
    }
    std::printf("Real SF2 verified at 44100 and 48000 Hz: %s\nArtifacts: %s\n",
                qPrintable(report.value("soundfontSha256").toString()),
                qPrintable(QDir(output).absolutePath()));
    return 0;
}
