#include "core/language.h"
#include "core/model.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <memory>
#include <string_view>
int main(int argc, char **argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--inspect" || argument.starts_with("--inspect=") || argument == "--help" ||
            argument == "-h" || argument == "--version" || argument == "-v")
            headless = true;
    }
    std::unique_ptr<QCoreApplication> app;
    if (headless)
        app = std::make_unique<QCoreApplication>(argc, argv);
    else
        app = std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setApplicationName("OpenKeysPiano");
    QCoreApplication::setOrganizationName("OpenKeys");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("OpenKeys Piano — offline computer-keyboard piano");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Start the UI without initializing audio and exit automatically."});
    parser.addOption({"screenshot", "Save a diagnostic UI screenshot and exit.", "path"});
    parser.addOption(
        {"inspect", "Validate an EOP/recording and print metadata without initializing audio.", "path"});
    parser.addOption({"soundfont", "Import a local SF2 and save an application copy.", "path"});
    parser.addOption({"language", "Use system, en or zh_CN for this process only.", "language"});
    parser.addPositionalArgument("song", "Optional EOP or OpenKeys recording to open.");
    parser.process(*app);
    const auto overrideLanguage = parser.value("language");
    if (parser.isSet("language") && !piano::Language::valid(overrideLanguage)) {
        std::fprintf(stderr, "Invalid language; expected system, en or zh_CN.\n");
        return 2;
    }
    const bool diagnostic = parser.isSet("smoke-test") || parser.isSet("screenshot");
    std::unique_ptr<QTemporaryDir> diagnosticSettings;
    if (diagnostic && !headless) {
        diagnosticSettings = std::make_unique<QTemporaryDir>();
        if (!diagnosticSettings->isValid())
            return 3;
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setStyle("Fusion");
        QFont previewFont("Noto Sans CJK SC");
        previewFont.setPixelSize(13);
        QApplication::setFont(previewFont);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, diagnosticSettings->path());
    }
    QSettings settings("OpenKeys", "Piano");
    const auto preference =
        parser.isSet("language")
            ? overrideLanguage
            : piano::Language::preference(settings, app->applicationDirPath(), !diagnostic && !headless);
    piano::Language language;
    if (!language.load(preference)) {
        std::fprintf(stderr, "Could not load embedded translations.\n");
        return 3;
    }
    if (parser.isSet("inspect")) {
        try {
            const auto song = piano::readSong(parser.value("inspect"));
            const auto bytes =
                QJsonDocument(QJsonObject{{"title", song.title},
                                          {"author", song.author},
                                          {"events", qint64(song.events.size())},
                                          {"durationUs", qint64(song.durationUs())},
                                          {"warnings", QJsonArray::fromStringList(song.warnings)}})
                    .toJson();
            std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
            return 0;
        } catch (const piano::FileError &e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 2;
        }
    }
    piano::MainWindow window(diagnostic);
    window.show();
    if (parser.isSet("soundfont"))
        window.loadFont(parser.value("soundfont"));
    if (!parser.positionalArguments().isEmpty())
        window.openPath(parser.positionalArguments().first());
    if (diagnostic)
        QTimer::singleShot(300, app.get(), [&] {
            if (parser.isSet("screenshot") && !window.grab().save(parser.value("screenshot"))) {
                app->exit(3);
                return;
            }
            app->quit();
        });
    return app->exec();
}
