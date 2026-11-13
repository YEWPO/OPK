#pragma once
#include <QLocale>
#include <QSettings>
#include <QString>
#include <QTranslator>

namespace piano {
// Owns translators for the lifetime of the application; install before constructing widgets.
class Language {
  public:
    static bool valid(const QString &preference);
    static QString resolve(const QString &preference, const QLocale &system = QLocale::system());
    static QString preference(QSettings &settings, const QString &installationDirectory,
                              bool saveInitial = true);
    bool load(const QString &language);
    static QString current();

  private:
    QTranslator application_, qt_;
};
} // namespace piano
