#include "fixtures.h"
#include "platform/input.h"
#include "ui/keyboard_view.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
using namespace piano;
namespace {
void clickStep(QAbstractSpinBox *control, bool up) {
    QStyleOptionSpinBox option;
    option.initFrom(control);
    option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
    const auto part = up ? QStyle::SC_SpinBoxUp : QStyle::SC_SpinBoxDown;
    const auto rect = control->style()->subControlRect(QStyle::CC_SpinBox, &option, part, control);
    QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, rect.center());
}
QPoint computerKeyPoint(const KeyboardView &view, uint16_t code) {
    const auto &keys = physicalKeys();
    const auto key = std::find_if(keys.begin(), keys.end(), [code](const auto &k) { return k.code == code; });
    Q_ASSERT(key != keys.end());
    const double unit = (view.width() - 24.) / 24.;
    const double height = std::clamp((view.height() - 220.) / 6., 34., 52.);
    return QPointF(12 + key->column * unit + (unit * key->width - 5) / 2,
                   34 + key->row * height + (height - 5) / 2)
        .toPoint();
}
QPoint pianoPoint(const KeyboardView &view, double whiteIndex, bool upper = false) {
    return QPointF(12 + whiteIndex * (view.width() - 24.) / 52., view.height() - 156. + (upper ? 25 : 110))
        .toPoint();
}
void moveMouse(KeyboardView &view, QPoint point, bool down) {
    QMouseEvent event(QEvent::MouseMove, point, view.mapToGlobal(point), Qt::NoButton,
                      down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &event);
}
KeyboardView *keyboardView(MainWindow &window) {
    return static_cast<KeyboardView *>(window.findChild<QWidget *>("keyboardView"));
}
} // namespace
class UiTests : public QObject {
    Q_OBJECT
  private slots:
    void startupDefaults() {
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QVERIFY(QDir().mkpath(directory));
        const auto path = directory + "/keyboard.json";
        QFile existing(path);
        const bool hadProfile = existing.exists();
        QByteArray previous;
        if (hadProfile) {
            QVERIFY(existing.open(QIODevice::ReadOnly));
            previous = existing.readAll();
            existing.close();
        }
        const auto restore = qScopeGuard([&] {
            if (hadProfile) {
                QFile file(path);
                if (file.open(QIODevice::WriteOnly))
                    file.write(previous);
            } else {
                QFile::remove(path);
            }
        });
        auto map = Keymap::defaults();
        map.transpose = 3;
        map.octave = {-1, 2};
        map.velocity = {65, 90};
        map.bindings[30].note = 61;
        map.save(path);
        {
            MainWindow window(true);
            const auto *view = keyboardView(window);
            QCOMPARE(view->map.transpose, 0);
            QCOMPARE(view->map.octave[0], 0);
            QCOMPARE(view->map.octave[1], 0);
            QCOMPARE(view->map.velocity[0], 65);
            QCOMPARE(view->map.bindings.at(30).note, 61);
            QCOMPARE(window.findChild<QSpinBox *>("transpose")->text(), QStringLiteral("C"));
            QCOMPARE(window.findChild<QSpinBox *>("leftOctave")->text(), QStringLiteral("原音区"));
            QCOMPARE(window.findChild<QSpinBox *>("rightOctave")->text(), QStringLiteral("原音区"));
        }
        QFile::remove(path);
        MainWindow firstLaunch(true);
        QCOMPARE(keyboardView(firstLaunch)->map.transpose, 0);
        QCOMPARE(keyboardView(firstLaunch)->map.octave[0], 0);
        {
            QFile broken(path);
            QVERIFY(broken.open(QIODevice::WriteOnly));
            broken.write("invalid JSON");
        }
        MainWindow corruptProfile(true);
        QCOMPARE(keyboardView(corruptProfile)->map.transpose, 0);
        QCOMPARE(keyboardView(corruptProfile)->map.octave[1], 0);
    }
    void mouseKeyboardAndPianoGlissando() {
        KeyboardView view;
        view.resize(1100, 500);
        view.show();
        std::vector<std::pair<int, bool>> keys, notes;
        view.mouseKey = [&](uint16_t code, bool down) { keys.emplace_back(code, down); };
        view.mouseNote = [&](int note, bool down) { notes.emplace_back(note, down); };
        const auto a = computerKeyPoint(view, 30), s = computerKeyPoint(view, 31);
        moveMouse(view, a, false);
        QCOMPARE(view.cursor().shape(), Qt::PointingHandCursor);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, a);
        QCOMPARE(keys, (std::vector<std::pair<int, bool>>{{30, true}}));
        moveMouse(view, a, true);
        QCOMPARE(keys.size(), size_t(1));
        moveMouse(view, s, true);
        QCOMPARE(keys, (std::vector<std::pair<int, bool>>{{30, true}, {30, false}, {31, true}}));
        // A0 white key followed by A#0, whose upper rectangle overlaps the white keys.
        moveMouse(view, pianoPoint(view, .5), true);
        QCOMPARE(keys.back(), (std::pair<int, bool>{31, false}));
        QCOMPARE(notes.back(), (std::pair<int, bool>{21, true}));
        moveMouse(view, pianoPoint(view, 1., true), true);
        QCOMPARE(notes, (std::vector<std::pair<int, bool>>{{21, true}, {21, false}, {22, true}}));
        moveMouse(view, QPoint(3, 3), true);
        QCOMPARE(notes.back(), (std::pair<int, bool>{22, false}));
        QCOMPARE(view.cursor().shape(), Qt::ArrowCursor);
        moveMouse(view, pianoPoint(view, 1.5), true);
        QCOMPARE(notes.back(), (std::pair<int, bool>{23, true}));
        // A right/middle release must not stop a left-button performance.
        QMouseEvent rightRelease(QEvent::MouseButtonRelease, pianoPoint(view, 1.5),
                                 view.mapToGlobal(pianoPoint(view, 1.5)), Qt::RightButton, Qt::LeftButton,
                                 Qt::NoModifier);
        QApplication::sendEvent(&view, &rightRelease);
        QCOMPARE(notes.back(), (std::pair<int, bool>{23, true}));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(-5, -5));
        QCOMPARE(notes.back(), (std::pair<int, bool>{23, false}));
    }
    void mouseCancellation_data() {
        QTest::addColumn<int>("eventType");
        QTest::newRow("focus") << int(QEvent::FocusOut);
        QTest::newRow("deactivate") << int(QEvent::WindowDeactivate);
        QTest::newRow("hide") << int(QEvent::Hide);
        QTest::newRow("lost-grab") << int(QEvent::UngrabMouse);
        QTest::newRow("leave") << int(QEvent::Leave);
    }
    void mouseCancellation() {
        QFETCH(int, eventType);
        for (const bool piano : {true, false}) {
            KeyboardView view;
            view.resize(1100, 500);
            view.show();
            int presses = 0, releases = 0;
            const auto callback = [&](int, bool down) { down ? ++presses : ++releases; };
            view.mouseKey = callback;
            view.mouseNote = callback;
            const auto point = piano ? pianoPoint(view, .5) : computerKeyPoint(view, 30);
            QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(presses, 1);
            QEvent cancel(static_cast<QEvent::Type>(eventType));
            QApplication::sendEvent(&view, &cancel);
            QCOMPARE(releases, 1);
            QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(-5, -5));
            QCOMPARE(releases, 1);
        }
    }
    void rightClickAndReset() {
        KeyboardView view;
        view.resize(1100, 500);
        view.show();
        int edited = 0, presses = 0, releases = 0;
        view.editKey = [&](uint16_t key) { edited = key; };
        view.mouseKey = [&](uint16_t, bool down) { down ? ++presses : ++releases; };
        const auto a = computerKeyPoint(view, 30);
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, a);
        QCOMPARE(edited, 30);
        QCOMPARE(presses, 0);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, a);
        QCOMPARE(presses, 1);
        view.resetMouseInteraction(); // owner has already released its musical state
        moveMouse(view, computerKeyPoint(view, 31), true);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, a);
        QCOMPARE(presses, 1);
        QCOMPARE(releases, 0);
        view.map.bindings.erase(30);
        moveMouse(view, a, false);
        QCOMPARE(view.cursor().shape(), Qt::ArrowCursor);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, a);
        QCOMPARE(presses, 1);
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, a);
        QCOMPARE(edited, 30); // unbound keys remain editable
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, computerKeyPoint(view, 42));
        QCOMPARE(presses, 2); // fixed velocity modifiers remain clickable
        QCOMPARE(releases, 1);
    }
    void silentMouseFeedbackAndIcon() {
        MainWindow window(true);
        window.show();
        auto *view = keyboardView(window);
        QVERIFY(view);
        QVERIFY(!window.windowIcon().isNull());
        for (const int size : {16, 24, 32, 48, 64, 128, 256})
            QVERIFY(!window.windowIcon().pixmap(size).isNull());
        const auto a = computerKeyPoint(*view, 30);
        moveMouse(*view, a, false);
        const auto hovered = view->grab().toImage();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, a);
        const auto pressed = view->grab().toImage();
        QVERIFY(pressed != hovered);
        QVERIFY(view->pressedKeys.contains(30));
        QVERIFY(window.statusBar()->currentMessage().contains("导入音源"));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, a);
        QVERIFY(!view->pressedKeys.contains(30));
        QCOMPARE(view->grab().toImage(), hovered);
        for (const auto &hand : view->liveNotes)
            for (const auto count : hand)
                QCOMPARE(count, 0);
        const auto note = pianoPoint(*view, .5);
        moveMouse(*view, note, false);
        const auto noteHover = view->grab().toImage();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, note);
        QVERIFY(view->grab().toImage() != noteHover);
        QTest::keyClick(view, Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier);
        moveMouse(*view, note, false);
        QCOMPARE(view->grab().toImage(), noteHover);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, note);
    }
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
    void performanceControls() {
        MainWindow window(true);
        window.show();
        window.resize(980, 760);
        QApplication::processEvents();
        for (const char *name : {"transpose", "leftOctave", "rightOctave", "leftVelocity", "rightVelocity"}) {
            auto *control = window.findChild<QSpinBox *>(QLatin1String(name));
            QVERIFY(control);
            const auto labels = control->parentWidget()->findChildren<QLabel *>();
            QCOMPARE(labels.size(), 1);
            QCOMPARE(control->geometry().left() - labels[0]->geometry().right() - 1, 8);
        }
        auto *transpose = window.findChild<QSpinBox *>("transpose");
        QVERIFY(transpose);
        transpose->setValue(0);
        QCOMPARE(transpose->text(), QStringLiteral("C"));
        for (int n = 0; n < 3; ++n)
            clickStep(transpose, true);
        QCOMPARE(transpose->value(), 3);
        QCOMPARE(transpose->text(), QStringLiteral("Eb"));
        QCOMPARE(keyboardView(window)->map.transpose, 3);
        transpose->setValue(-1);
        QCOMPARE(transpose->text(), QStringLiteral("B（低 1 个八度）"));
        transpose->setValue(12);
        QCOMPARE(transpose->text(), QStringLiteral("C（高 1 个八度）"));
        clickStep(transpose, true);
        QCOMPARE(transpose->value(), 12);
        transpose->setValue(-12);
        QCOMPARE(transpose->text(), QStringLiteral("C（低 1 个八度）"));
        clickStep(transpose, false);
        QCOMPARE(transpose->value(), -12);
        for (const char *name : {"leftOctave", "rightOctave"}) {
            auto *control = window.findChild<QSpinBox *>(QLatin1String(name));
            QVERIFY(control);
            control->setValue(0);
            QCOMPARE(control->text(), QStringLiteral("原音区"));
            clickStep(control, true);
            QCOMPARE(control->text(), QStringLiteral("升高 1 个八度"));
            clickStep(control, false);
            clickStep(control, false);
            QCOMPARE(control->text(), QStringLiteral("降低 1 个八度"));
        }
        for (auto *control : window.findChildren<QAbstractSpinBox *>()) {
            auto *editor = control->findChild<QLineEdit *>();
            QVERIFY(editor);
            QVERIFY(editor->isReadOnly());
            const auto before = control->text();
            QTest::keyClicks(editor, "99");
            QApplication::clipboard()->setText("88");
            editor->selectAll();
            QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
            for (auto key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown}) {
                QTest::keyClick(editor, key);
                QTest::keyClick(control, key);
            }
            for (QWidget *target : {static_cast<QWidget *>(control), static_cast<QWidget *>(editor)}) {
                QWheelEvent wheel(QPointF(5, 5), target->mapToGlobal(QPoint(5, 5)), {},
                                  QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                                  Qt::NoScrollPhase, false);
                QApplication::sendEvent(target, &wheel);
            }
            QCOMPARE(control->text(), before);
        }
        for (const char *name : {"leftVelocity", "rightVelocity"}) {
            auto *control = window.findChild<QSpinBox *>(QLatin1String(name));
            QVERIFY(control);
            control->setValue(80);
            clickStep(control, true);
            QCOMPARE(control->value(), 81);
            clickStep(control, false);
            QCOMPARE(control->value(), 80);
        }
        auto *speed = window.findChild<QDoubleSpinBox *>("playbackSpeed");
        QVERIFY(speed);
        clickStep(speed, true);
        QCOMPARE(speed->value(), 1.05);
        clickStep(speed, false);
        QCOMPARE(speed->value(), 1.0);
        // Resetting a profile must refresh the formatted controls as well as the map.
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            for (auto *candidate : dialog->findChildren<QPushButton *>())
                if (candidate->text() == QStringLiteral("恢复默认双手布局")) {
                    candidate->click();
                    return;
                }
            dialog->reject();
            QFAIL("Missing reset button");
        });
        for (auto *candidate : window.findChildren<QPushButton *>())
            if (candidate->text() == QStringLiteral("键位方案")) {
                candidate->click();
                break;
            }
        QCOMPARE(transpose->text(), QStringLiteral("C"));
        QCOMPARE(window.findChild<QSpinBox *>("leftOctave")->text(), QStringLiteral("原音区"));
        QCOMPARE(window.findChild<QSpinBox *>("rightOctave")->text(), QStringLiteral("原音区"));
        QCOMPARE(keyboardView(window)->map.transpose, 0);
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
#ifndef Q_OS_WIN
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
#endif
    QCoreApplication::setOrganizationName("OpenKeysTests");
    QCoreApplication::setApplicationName("UI");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui_tests.moc"
