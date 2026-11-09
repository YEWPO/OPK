#include "audio/soundfont_check.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("openkeys-soundfont-check");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Validate a piano SF2 offline and produce WAV previews; no soundcard or network required.");
    parser.addHelpOption();
    parser.addOption({"soundfont", "Path to the actual SF2 soundfont to check.", "path"});
    parser.addOption({"output", "Directory for report.json and WAV files.", "directory", "soundfont-check"});
    parser.process(app);
    if (!parser.isSet("soundfont")) {
        std::fprintf(stderr, "--soundfont PATH is required\n");
        return 2;
    }
    try {
        const auto demo = piano::readRecording(piano::readBounded(":/assets/demo.eopiano.json"));
        const auto report = piano::checkSoundfont(parser.value("soundfont"), parser.value("output"), demo);
        const auto json = QJsonDocument(report).toJson();
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        return report.value("passed").toBool() ? 0 : 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
