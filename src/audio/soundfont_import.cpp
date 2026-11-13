#include "audio/soundfont_import.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QtEndian>
#include <algorithm>

namespace piano {
namespace {
void notify(const ImportProgress &progress, ImportPhase phase, int percent) {
    if (progress && !progress(phase, percent))
        throw FileError("import_cancelled",
                        QCoreApplication::translate("OpenKeys", "Soundfont import cancelled."));
}
void commitInfo(const QString &directory, const ImportedFontInfo &info) {
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1}, {"id", info.id}, {"name", info.name}}).toJson();
    QSaveFile file(QDir(directory).filePath("current.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw FileError("import_settings_failed",
                        QCoreApplication::translate("OpenKeys", "Could not save soundfont settings: %1")
                            .arg(file.errorString()));
}
QByteArray fileHash(const QString &path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        throw FileError("import_read_failed", QCoreApplication::translate(
                                                  "OpenKeys", "Could not read the existing soundfont copy."));
    return hash.result().toHex();
}
} // namespace
QString managedSoundfontPath(const QString &directory, const QString &id) {
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hash.match(id).hasMatch())
        return {};
    return QDir(directory).absoluteFilePath(id + ".sf2");
}
ImportedFontInfo readImportedSoundfont(const QString &directory) {
    const auto path = QDir(directory).filePath("current.json");
    if (!QFileInfo::exists(path))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
        throw FileError(
            "invalid_soundfont_settings",
            QCoreApplication::translate(
                "OpenKeys", "Could not read soundfont settings. Please import the soundfont again."));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = doc.object();
    const QString id = object.value("id").toString();
    const auto managed = managedSoundfontPath(directory, id);
    if (error.error != QJsonParseError::NoError || !doc.isObject() ||
        object.value("version").toDouble() != 1 || managed.isEmpty() || !object.value("name").isString())
        throw FileError(
            "invalid_soundfont_settings",
            QCoreApplication::translate(
                "OpenKeys", "Soundfont settings are damaged. Please import the soundfont again."));
    return {id, object.value("name").toString(), managed};
}
ImportedSoundfont importSoundfont(const QString &source, const QString &directory, double sampleRate,
                                  const ImportProgress &progress) {
    ImportedSoundfont result;
    QString createdPath;
    try {
        notify(progress, ImportPhase::Copying, 0);
        QFile input(source);
        if (!source.endsWith(".sf2", Qt::CaseInsensitive))
            throw FileError("unsupported_soundfont",
                            QCoreApplication::translate(
                                "OpenKeys", "Choose an extracted .sf2 soundfont file. Archives, SFZ files "
                                            "and VST plugins cannot be imported directly."));
        if (!input.open(QIODevice::ReadOnly))
            throw FileError("import_read_failed",
                            QCoreApplication::translate("OpenKeys", "Could not read the soundfont: %1")
                                .arg(input.errorString()));
        const auto total = input.size();
        const auto header = input.peek(12);
        if (total < 12 || header.first(4) != "RIFF" || header.mid(8, 4) != "sfbk" ||
            quint64(qFromLittleEndian<quint32>(header.constData() + 4)) + 8 != quint64(total))
            throw FileError(
                "invalid_soundfont",
                QCoreApplication::translate(
                    "OpenKeys",
                    "This file is not a complete SF2 soundfont. Extract the archive or download it again."));
        if (!QDir().mkpath(directory))
            throw FileError(
                "import_write_failed",
                QCoreApplication::translate("OpenKeys", "Could not create the soundfont directory. Check "
                                                        "free disk space and write permissions."));
        try {
            result.previousId = readImportedSoundfont(directory).id;
        } catch (const FileError &) {
        }
        QTemporaryFile staging(QDir(directory).filePath(".import-XXXXXX.sf2"));
        if (!staging.open())
            throw FileError("import_write_failed",
                            QCoreApplication::translate("OpenKeys", "Could not write the soundfont copy: %1")
                                .arg(staging.errorString()));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 copied = 0;
        int lastProgress = -1;
        while (!input.atEnd()) {
            const auto bytes = input.read(1024 * 1024);
            if (input.error() != QFileDevice::NoError || bytes.isEmpty())
                throw FileError(
                    "import_read_failed",
                    QCoreApplication::translate("OpenKeys", "Soundfont reading was interrupted. The previous "
                                                            "soundfont and settings were preserved."));
            if (staging.write(bytes) != bytes.size())
                throw FileError("import_write_failed",
                                QCoreApplication::translate(
                                    "OpenKeys", "Soundfont copy failed. Check free disk space: %1")
                                    .arg(staging.errorString()));
            hash.addData(bytes);
            copied += bytes.size();
            if (copied > total)
                throw FileError("import_source_changed",
                                QCoreApplication::translate(
                                    "OpenKeys", "The source file changed during import. Please try again."));
            const int percent = int(copied * 100 / total);
            if (percent != lastProgress) {
                notify(progress, ImportPhase::Copying, percent);
                lastProgress = percent;
            }
        }
        if (copied != total || !staging.flush())
            throw FileError(
                "import_copy_failed",
                QCoreApplication::translate(
                    "OpenKeys", "The soundfont copy is incomplete. Check free disk space and try again."));
        input.close();
        const auto idBytes = hash.result().toHex();
        result.info = {QString::fromLatin1(idBytes), QFileInfo(source).completeBaseName(),
                       managedSoundfontPath(directory, QString::fromLatin1(idBytes))};
        notify(progress, ImportPhase::Loading, 0);
        if (QFileInfo::exists(result.info.path)) {
            if (fileHash(result.info.path) != idBytes) {
                // Repair a damaged owned copy without deleting it first. A
                // failed atomic replace leaves the previous file untouched.
                if (!staging.seek(0))
                    throw FileError("import_copy_failed", "Cannot read temporary copy");
                QSaveFile repair(result.info.path);
                if (!repair.open(QIODevice::WriteOnly))
                    throw FileError("import_write_failed", repair.errorString());
                while (!staging.atEnd()) {
                    const auto bytes = staging.read(1024 * 1024);
                    if (bytes.isEmpty() || repair.write(bytes) != bytes.size())
                        throw FileError("import_write_failed", repair.errorString());
                    notify(progress, ImportPhase::Loading, 0);
                }
                if (!repair.commit())
                    throw FileError("import_write_failed", repair.errorString());
            }
        } else {
            staging.close();
            if (!staging.rename(result.info.path))
                throw FileError(
                    "import_write_failed",
                    QCoreApplication::translate("OpenKeys", "Could not save the soundfont copy: %1")
                        .arg(staging.errorString()));
            staging.setAutoRemove(false);
            createdPath = result.info.path;
        }
        auto loaded = AudioEngine::loadSoundfont(result.info.path, sampleRate);
        if (!loaded.font)
            throw FileError(
                "soundfont_load_failed",
                QCoreApplication::translate("OpenKeys", "Could not load the selected SF2. Check that the "
                                                        "file is complete or try a smaller soundfont."));
        result.font = std::move(loaded.font);
        notify(progress, ImportPhase::Loading, 100);
        commitInfo(directory, result.info);
        return result;
    } catch (const FileError &error) {
        result.error = error.detail;
        result.errorCode = error.code;
    } catch (const std::exception &error) {
        result.error =
            QCoreApplication::translate(
                "OpenKeys", "Could not import the soundfont. Check available memory and disk space. %1")
                .arg(QString::fromUtf8(error.what()));
        result.errorCode = "import_failed";
    }
    result.font.reset();
    if (!createdPath.isEmpty())
        QFile::remove(createdPath);
    return result;
}
bool removeOldSoundfont(const QString &directory, const QString &previousId, const QString &activeId) {
    if (previousId == activeId)
        return true;
    const auto path = managedSoundfontPath(directory, previousId);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return true;
    try {
        if (activeId.isEmpty() || readImportedSoundfont(directory).id != activeId)
            return false;
    } catch (const FileError &) {
        return false;
    }
    return QFile::remove(path);
}
} // namespace piano
