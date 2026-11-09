#pragma once
#include "audio/engine.h"
#include <functional>
namespace piano {
struct ImportedFontInfo {
    QString id;
    QString name;
    QString path;
};
enum class ImportPhase { Copying, Loading };
// Return false to cancel. Called on the worker thread, never directly updates UI.
using ImportProgress = std::function<bool(ImportPhase, int)>;
struct ImportedSoundfont {
    std::shared_ptr<Soundfont> font;
    ImportedFontInfo info;
    QString previousId;
    QString error;
    QString errorCode;
};
QString managedSoundfontPath(const QString &directory, const QString &id);
ImportedFontInfo readImportedSoundfont(const QString &directory);
ImportedSoundfont importSoundfont(const QString &source, const QString &directory, double sampleRate,
                                  const ImportProgress &progress = {});
// Only removes a hash-named application copy, never the original or active bank.
bool removeOldSoundfont(const QString &directory, const QString &previousId, const QString &activeId);
} // namespace piano
