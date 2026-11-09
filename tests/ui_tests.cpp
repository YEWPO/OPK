#include "fixtures.h"
#include "platform/input.h"
#include "ui/keyboard_view.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
using namespace piano;
class UiTests : public QObject {
    Q_OBJECT
  private slots:
    void firstUseAndOfflineGuide() {
        QTemporaryDir dir;
        MainWindow window(true, dir.filePath("managed"));
        window.show();
        QVERIFY(window.findChild<QLabel *>("soundfontStatus")->text().contains("导入音源后开始演奏"));
        auto *import = window.findChild<QPushButton *>("importSoundfont");
        QVERIFY(import);
        QVERIFY(import->isEnabled());
        QVERIFY(!QFileInfo::exists(dir.filePath("managed/current.json")));
        QTest::mouseClick(window.findChild<QPushButton *>("demoSong"), Qt::LeftButton);
        QVERIFY(window.findChild<QLabel *>("trackTitle")->text().contains("初次相遇"));
        bool found = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = window.findChild<QDialog *>("soundfontGuideDialog");
            if (!dialog)
                return;
            auto *content = dialog->findChild<QTextBrowser *>("soundfontGuideContent");
            found = content && content->toPlainText().contains("确认导入成功后") &&
                    content->toPlainText().contains(".sf2") && !content->openLinks();
            dialog->accept();
        });
        QTest::mouseClick(window.findChild<QPushButton *>("soundfontGuide"), Qt::LeftButton);
        QVERIFY(found);
    }
    void importsPersistsAndRestoresCopy() {
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("我的钢琴.sf2"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture::testSoundfont());
        file.close();
        const auto store = dir.filePath("managed");
        QString id;
        {
            MainWindow window(true, store);
            window.show();
            window.loadFont(source);
            QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QPushButton *>("importSoundfont")->isEnabled(), 5000);
            QCOMPARE(window.findChild<QLabel *>("soundfontStatus")->text(), QStringLiteral("● 我的钢琴"));
            id = readImportedSoundfont(store).id;
            QVERIFY(!id.isEmpty());
            QVERIFY(QFile::exists(source));
            QVERIFY(QFile::remove(source));
            QTimer dismiss;
            QObject::connect(&dismiss, &QTimer::timeout, [] {
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *message = qobject_cast<QMessageBox *>(widget))
                        message->accept();
            });
            dismiss.start(10);
            const auto bad = dir.filePath("bad.sf2");
            QFile corrupt(bad);
            QVERIFY(corrupt.open(QIODevice::WriteOnly));
            corrupt.write("bad");
            corrupt.close();
            window.loadFont(bad);
            QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QPushButton *>("importSoundfont")->isEnabled(), 5000);
            QCOMPARE(readImportedSoundfont(store).id, id);
            QCOMPARE(window.findChild<QLabel *>("soundfontStatus")->text(), QStringLiteral("● 我的钢琴"));
            const auto oldCopy = managedSoundfontPath(store, id);
            const auto replacement = dir.filePath(QStringLiteral("替换音源.sf2"));
            auto bytes = fixture::testSoundfont();
            bytes[bytes.indexOf("Fixture")] = 'R';
            QFile next(replacement);
            QVERIFY(next.open(QIODevice::WriteOnly));
            next.write(bytes);
            next.close();
            window.loadFont(replacement);
            QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel *>("soundfontStatus")->text(),
                                      QStringLiteral("● 替换音源"), 5000);
            QVERIFY(!QFile::exists(oldCopy));
            QVERIFY(QFile::exists(replacement));
            id = readImportedSoundfont(store).id;
        }
        MainWindow restored(true, store);
        restored.show();
        QTRY_COMPARE_WITH_TIMEOUT(restored.findChild<QLabel *>("soundfontStatus")->text(),
                                  QStringLiteral("● 替换音源"), 5000);
        QCOMPARE(readImportedSoundfont(store).id, id);
    }
    void legacyPathOnlyPromptsForImport() {
        QTemporaryDir dir;
        const auto source = dir.filePath("legacy.sf2");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture::testSoundfont());
        file.close();
        QSettings settings("OpenKeys", "Piano");
        settings.setValue("soundfont", source);
        settings.sync();
        {
            MainWindow window(true, dir.filePath("managed"));
            window.show();
            QTest::qWait(30);
            QVERIFY(!QFileInfo::exists(dir.filePath("managed/current.json")));
            QVERIFY(window.findChild<QLabel *>("soundfontStatus")->text().contains("导入音源后开始演奏"));
        }
        settings.remove("soundfont");
    }
    void importEopAndChangeTranspose() {
        QTemporaryDir dir;
        const auto path = dir.filePath("music.eop");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture::encode(fixture::decodedEop(201)));
        file.close();
        MainWindow window(true);
        window.show();
        window.openPath(path);
        QCOMPARE(window.findChild<QLabel *>("trackTitle")->text(), QStringLiteral("Fixture song"));
        auto *transpose = window.findChild<QSpinBox *>("transpose");
        QVERIFY(transpose);
        transpose->setValue(3);
        // The native EOP data remains importable independently of live settings.
        QCOMPARE(readSong(path).events[0].note, 60);
        QTimer::singleShot(0, [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget))
                    box->accept();
        });
        window.openPath(dir.filePath("missing.eop"));
        QCOMPARE(window.findChild<QLabel *>("trackTitle")->text(), QStringLiteral("Fixture song"));
        QVERIFY(!window.grab().isNull());
    }
    void typingAndEmergencyShortcut() {
        QWidget window;
        QVBoxLayout layout(&window);
        QWidget instrument;
        instrument.setFocusPolicy(Qt::StrongFocus);
        QLineEdit text;
        layout.addWidget(&instrument);
        layout.addWidget(&text);
        InputRouter input(&window);
        int notes = 0, panic = 0, releases = 0;
        input.isBound = [](uint16_t code) { return code == 30; };
        input.keyChanged = [&](uint16_t, bool down) {
            if (down)
                ++notes;
        };
        input.suspended = [&] { ++releases; };
        input.emergency = [&] { ++panic; };
        window.show();
        window.activateWindow();
        instrument.setFocus();
        QTest::qWait(10);
#ifndef Q_OS_WIN
        QTest::keyClick(&instrument, Qt::Key_A);
        QCOMPARE(notes, 1);
#endif
        text.setFocus();
        QTest::qWait(10);
        const int before = notes;
        QTest::keyClicks(&text, "abc");
        QCOMPARE(text.text(), QStringLiteral("abc"));
        QCOMPARE(notes, before);
        QVERIFY(releases > 0);
        QTest::keyClick(&text, Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(panic, 1);
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QCoreApplication::setOrganizationName("OpenKeysTests");
    QCoreApplication::setApplicationName("UI");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui_tests.moc"
