#include "core/language.h"
#include <QCoreApplication>
#include <QDir>

static void initializeTranslations() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(translations);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace piano {
namespace {
QString activeLanguage = "en";
}
bool Language::valid(const QString &preference) {
    return preference == "system" || preference == "en" || preference == "zh_CN";
}
QString Language::resolve(const QString &preference, const QLocale &system) {
    if (preference == "en" || preference == "zh_CN")
        return preference;
    return system.language() == QLocale::Chinese ? "zh_CN" : "en";
}
QString Language::preference(QSettings &settings, const QString &installationDirectory, bool saveInitial) {
    if (settings.contains("ui/language")) {
        const auto saved = settings.value("ui/language").toString();
        return valid(saved) ? saved : QStringLiteral("system");
    }
    QSettings installer(QDir(installationDirectory).filePath("install-language.ini"), QSettings::IniFormat);
    const auto initial = installer.value("language").toString();
    if (initial == "en" || initial == "zh_CN") {
        if (saveInitial)
            settings.setValue("ui/language", initial);
        return initial;
    }
    return "system";
}
bool Language::load(const QString &language) {
    initializeTranslations();
    QCoreApplication::removeTranslator(&application_);
    QCoreApplication::removeTranslator(&qt_);
    activeLanguage = resolve(language);
    if (activeLanguage == "en")
        return true;
    if (!qt_.load(":/i18n/qtbase_zh_CN.qm") || !application_.load(":/i18n/openkeys_zh_CN.qm"))
        return false;
    QCoreApplication::installTranslator(&qt_);
    QCoreApplication::installTranslator(&application_);
    return true;
}
QString Language::current() {
    return activeLanguage;
}
} // namespace piano
