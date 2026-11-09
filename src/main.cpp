#include "core/model.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    parser.addPositionalArgument("song", "Optional EOP or OpenKeys recording to open.");
    parser.process(*app);
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
    const bool diagnostic = parser.isSet("smoke-test") || parser.isSet("screenshot");
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
