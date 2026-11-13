#include "core/keyboard.h"
#include "core/language.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
using namespace piano;
class LanguageTests : public QObject {
    Q_OBJECT
  private slots:
    void systemFallback() {
        QCOMPARE(Language::resolve("system", QLocale("zh_CN")), "zh_CN");
        QCOMPARE(Language::resolve("system", QLocale("zh_TW")), "zh_CN");
        QCOMPARE(Language::resolve("system", QLocale("en_GB")), "en");
        QCOMPARE(Language::resolve("system", QLocale("de_DE")), "en");
        QCOMPARE(Language::resolve("en", QLocale("zh_CN")), "en");
        QCOMPARE(Language::resolve("zh_CN", QLocale("en_US")), "zh_CN");
        QVERIFY(!Language::valid("de"));
    }
    void installerAndUserPreference() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("user.ini"), QSettings::IniFormat);
        QCOMPARE(Language::preference(settings, directory.path()), "system");
        QVERIFY(!settings.contains("ui/language"));
        QSettings installer(directory.filePath("install-language.ini"), QSettings::IniFormat);
        installer.setValue("language", "en");
        installer.sync();
        QCOMPARE(Language::preference(settings, directory.path(), false), "en");
        QVERIFY(!settings.contains("ui/language"));
        QCOMPARE(Language::preference(settings, directory.path()), "en");
        settings.sync();
        QSettings reopened(directory.filePath("user.ini"), QSettings::IniFormat);
        QCOMPARE(reopened.value("ui/language").toString(), "en");
        installer.setValue("language", "zh_CN");
        installer.sync();
        QCOMPARE(Language::preference(settings, directory.path()), "en");
        settings.setValue("ui/language", "system");
        QCOMPARE(Language::preference(settings, directory.path()), "system");
        settings.setValue("ui/language", "invalid");
        QCOMPARE(Language::preference(settings, directory.path()), "system");
        settings.clear();
        installer.setValue("language", "bad");
        installer.sync();
        QCOMPARE(Language::preference(settings, directory.path()), "system");
    }
    void embeddedTranslations() {
        Language language;
        QVERIFY(language.load("zh_CN"));
        QCOMPARE(QCoreApplication::translate("OpenKeys", "Audio Settings"), QStringLiteral("音频设置"));
        QCOMPARE(keyName(57), QStringLiteral("Space · 延音"));
        const auto save = QCoreApplication::translate("QPlatformTheme", "Save");
        QVERIFY2(save != "Save", qPrintable(save));
        QVERIFY(language.load("en"));
        QCOMPARE(QCoreApplication::translate("OpenKeys", "Audio Settings"), "Audio Settings");
        QCOMPARE(keyName(57), "Space · Sustain");
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Save"), "Save");
    }
};
QTEST_GUILESS_MAIN(LanguageTests)
#include "language_tests.moc"
