#include "ui/main_window.h"
#include "core/language.h"
#include "platform/input.h"
#include "ui/keyboard_view.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
static void initializeGuide() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(import_guide);
        Q_INIT_RESOURCE(app_icons);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace piano {
namespace {
class PlaybackSlider : public QSlider {
  public:
    PlaybackSlider() : QSlider(Qt::Horizontal) {}

  protected:
    void positionAt(QMouseEvent *event) {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const auto groove =
            style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const auto handle =
            style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        setSliderPosition(QStyle::sliderValueFromPosition(
            minimum(), maximum(), int(event->position().x()) - groove.left() - handle.width() / 2,
            std::max(1, groove.width() - handle.width()), option.upsideDown));
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        setSliderDown(true);
        positionAt(event);
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (isSliderDown())
            positionAt(event);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            positionAt(event);
            setSliderDown(false);
        }
        event->accept();
    }
};
int velocityPercent(int midi) {
    return (midi * 100 + 63) / 127;
}
int midiVelocity(int percent) {
    return (percent * 127 + 50) / 100;
}
QString timeText(int64_t us) {
    const auto sec = us / 1000000;
    return QString("%1:%2").arg(sec / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'));
}
QPushButton *button(const QString &text, QWidget *parent = nullptr) {
    auto *b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
QSpinBox *spin(int lo, int hi, int value) {
    auto *s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(value);
    s->setKeyboardTracking(false);
    return s;
}
template <typename Base> class ArrowOnlySpin : public Base {
  public:
    ArrowOnlySpin() {
        this->lineEdit()->setReadOnly(true);
        this->lineEdit()->installEventFilter(this);
        this->setFocusPolicy(Qt::NoFocus);
        this->setWrapping(false);
        this->setStyleSheet(R"(
            QSpinBox,QDoubleSpinBox {padding-right:28px;}
            QSpinBox::up-button,QDoubleSpinBox::up-button {width:24px;background:#354761;}
            QSpinBox::down-button,QDoubleSpinBox::down-button {width:24px;background:#354761;}
            QSpinBox::up-button:hover,QDoubleSpinBox::up-button:hover,
            QSpinBox::down-button:hover,QDoubleSpinBox::down-button:hover {background:#4c6688;}
            QSpinBox::up-arrow,QDoubleSpinBox::up-arrow,
            QSpinBox::down-arrow,QDoubleSpinBox::down-arrow {image:none;}
        )");
    }

  protected:
    void keyPressEvent(QKeyEvent *event) override {
        event->ignore();
    }
    void wheelEvent(QWheelEvent *event) override {
        event->ignore();
    }
    void paintEvent(QPaintEvent *event) override {
        Base::paintEvent(event);
        QStyleOptionSpinBox option;
        this->initStyleOption(&option);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        for (bool up : {true, false}) {
            const auto part = up ? QStyle::SC_SpinBoxUp : QStyle::SC_SpinBoxDown;
            const auto center =
                this->style()->subControlRect(QStyle::CC_SpinBox, &option, part, this).center();
            const bool enabled = option.stepEnabled &
                                 (up ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled);
            painter.setBrush(QColor(enabled ? "#e6edf7" : "#758295"));
            const double x = center.x(), y = center.y(), direction = up ? -1. : 1.;
            painter.drawPolygon(QPolygonF{QPointF(x, y + direction * 3), QPointF(x - 4, y - direction * 2),
                                          QPointF(x + 4, y - direction * 2)});
        }
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == this->lineEdit() &&
            (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
             event->type() == QEvent::Wheel))
            return true;
        return Base::eventFilter(object, event);
    }
};
class PerformanceSpin : public ArrowOnlySpin<QSpinBox> {
  public:
    enum Display { Number, Key, Octave };
    explicit PerformanceSpin(Display display) : display_(display) {}
    QString displayText() const {
        return textFromValue(value());
    }
    QSize sizeHint() const override {
        auto size = ArrowOnlySpin<QSpinBox>::sizeHint();
        int width = 0;
        for (int value = minimum(); value <= maximum(); ++value)
            width = std::max(width, fontMetrics().horizontalAdvance(textFromValue(value)));
        // Include both stylesheet padding and the separate arrow column.
        size.setWidth(std::max(size.width(), width + 72));
        return size;
    }

  protected:
    QString textFromValue(int value) const override {
        if (display_ == Key) {
            static const char *names[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
            const int index = (value % 12 + 12) % 12;
            const int octave = (value - index) / 12;
            const auto name = QString::fromLatin1(names[index]);
            return octave == 0
                       ? name
                       : name + (octave > 0 ? QCoreApplication::translate("OpenKeys", " (1 octave up)")
                                            : QCoreApplication::translate("OpenKeys", " (1 octave down)"));
        }
        if (display_ == Octave)
            return value == 0 ? QCoreApplication::translate("OpenKeys", "Original")
                              : (value > 0 ? QCoreApplication::translate("OpenKeys", "Up %1 oct.")
                                           : QCoreApplication::translate("OpenKeys", "Down %1 oct."))
                                    .arg(std::abs(value));
        return QSpinBox::textFromValue(value);
    }

  private:
    Display display_;
};
QSpinBox *performanceSpin(int lo, int hi, int value, PerformanceSpin::Display display) {
    auto *control = new PerformanceSpin(display);
    control->setRange(lo, hi);
    control->setValue(value);
    return control;
}
void applyVisual(KeyboardView *view, const Event &e, bool playback) {
    auto &notes = playback ? view->playbackNotes : view->liveNotes;
    auto &keys = playback ? view->playbackKeys : view->liveKeys;
    if (e.kind != Kind::NoteOn && e.kind != Kind::NoteOff)
        return;
    const int delta = e.kind == Kind::NoteOn ? 1 : -1;
    auto &n = notes[size_t(e.hand)][size_t(e.note)];
    n = std::max(0, n + delta);
    if (e.key) {
        auto &counts = keys[e.key];
        auto &count = counts[size_t(e.hand)];
        count = std::max(0, count + delta);
        if (std::none_of(counts.begin(), counts.end(), [](int n) { return n > 0; }))
            keys.erase(e.key);
    }
}
} // namespace
MainWindow::MainWindow(bool diagnostic, const QString &soundfontDirectory)
    : settings_("OpenKeys", "Piano"), diagnostic_(diagnostic) {
    initializeGuide();
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QString(":/assets/icons/openkeys-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);
    setWindowIcon(icon);
    soundfontDirectory_ = soundfontDirectory.isEmpty()
                              ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                    .filePath("soundfonts")
                              : soundfontDirectory;
    setWindowTitle(QCoreApplication::translate("OpenKeys", "OpenKeys Piano · Keyboard Piano"));
    resize(1200, 960);
    setMinimumSize(980, 760);
    const auto configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!diagnostic_)
        QDir().mkpath(configDir);
    profilePath_ = configDir + "/keyboard.json";
    if (QFileInfo::exists(profilePath_))
        try {
            keyboard_.keymap = Keymap::read(profilePath_);
        } catch (const FileError &e) {
            statusBar()->showMessage(
                QCoreApplication::translate("OpenKeys",
                                            "Could not read the keymap; using the default layout: %1")
                    .arg(e.what()),
                20000);
        }
    keyboard_.keymap.transpose = 0;
    keyboard_.keymap.octave = {0, 0};
    keyboard_.keymap.velocity = {102, 127};
    config_.frames = settings_.value("audio/frames", 256).toInt();
    config_.exclusive = settings_.value("audio/exclusive", false).toBool();
    buildUi();
    input_ = std::make_unique<InputRouter>(this);
    input_->keyChanged = [this](uint16_t code, bool down) { key(code, down); };
    input_->isBound = [this](uint16_t code) { return keyboard_.keymap.bindings.contains(code); };
    input_->suspended = [this] { releaseHeld(); };
    input_->emergency = [this] { emergencyStop(); };
    background_->setEnabled(input_->globalAvailable());
    background_->setToolTip(input_->error());
    connect(background_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!input_->setBackground(enabled)) {
            QSignalBlocker block(background_);
            background_->setChecked(input_->background());
            statusBar()->showMessage(input_->error(), 10000);
        }
    });
    connect(&loader_, &QFutureWatcher<ImportedSoundfont>::finished, this, [this] {
        loading_ = false;
        fontButton_->setEnabled(true);
        importProgress_->hide();
        auto result = loader_.result();
        if (!result.font) {
            fontLabel_->setText(
                audio_.status().ready
                    ? QStringLiteral("● ") + fontName_
                    : QCoreApplication::translate("OpenKeys", "○ Import a soundfont to start playing"));
            if (audio_.status().ready)
                startAudio(false);
            showError(importing_ ? QCoreApplication::translate("OpenKeys", "Soundfont import failed")
                                 : QCoreApplication::translate("OpenKeys", "Soundfont loading failed"),
                      result.error);
            return;
        }
        releaseHeld();
        audio_.setSoundfont(std::move(result.font));
        importedInfo_ = result.info;
        fontPath_ = result.info.path;
        fontName_ = result.info.name;
        fontLabel_->setText(QStringLiteral("● %1").arg(fontName_));
        fontLabel_->setToolTip(QCoreApplication::translate(
            "OpenKeys", "An application copy is saved and will load automatically next time."));
        if (importing_) {
            settings_.remove("soundfont");
            legacyFontPath_.clear();
            if (!removeOldSoundfont(soundfontDirectory_, result.previousId, result.info.id))
                statusBar()->showMessage(
                    QCoreApplication::translate(
                        "OpenKeys", "New soundfont imported; the previous copy could not be removed yet."),
                    10000);
            else
                statusBar()->showMessage(
                    QCoreApplication::translate(
                        "OpenKeys", "Soundfont imported and copied. You can remove the original download."),
                    8000);
        }
        startAudio();
    });
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    timer_.start(16);
    if (!diagnostic_ || !soundfontDirectory.isEmpty()) {
        try {
            importedInfo_ = readImportedSoundfont(soundfontDirectory_);
            if (!importedInfo_.path.isEmpty() && QFileInfo(importedInfo_.path).isReadable() &&
                QFileInfo(importedInfo_.path).isFile()) {
                fontPath_ = importedInfo_.path;
                fontName_ = importedInfo_.name;
                QTimer::singleShot(0, this, [this] { beginFontTask(fontPath_, false); });
            } else {
                legacyFontPath_ = settings_.value("soundfont").toString();
                statusBar()->showMessage(
                    importedInfo_.id.isEmpty() && legacyFontPath_.isEmpty()
                        ? QCoreApplication::translate(
                              "OpenKeys", "Choose Import Soundfont to begin. The Import Guide works offline.")
                        : QCoreApplication::translate(
                              "OpenKeys",
                              "Please import your soundfont again to save a working application copy."));
            }
        } catch (const FileError &error) {
            statusBar()->showMessage(QString::fromUtf8(error.what()));
        }
    }
}

MainWindow::~MainWindow() {
    timer_.stop();
    releaseHeld();
    view_->mouseKey = {};
    view_->mouseNote = {};
    view_->editKey = {};
    if (cancelImport_)
        cancelImport_->store(true);
    loader_.waitForFinished();
    input_.reset();
    audio_.setSoundfont({});
    if (loader_.future().resultCount() > 0) {
        const auto result = loader_.result();
        if (result.font && !result.previousId.isEmpty())
            removeOldSoundfont(soundfontDirectory_, result.previousId, result.info.id);
    }
}
void MainWindow::buildUi() {
    setStyleSheet(R"(
        QMainWindow,QDialog,QWidget#mainContent,QScrollArea {background:#0d141d;color:#e6edf5;}
        QWidget {font-family:"Noto Sans CJK SC","Microsoft YaHei UI",sans-serif;font-size:13px;color:#e6edf5;}
        QLabel#brand {font-size:25px;font-weight:700;letter-spacing:1px;}
        QLabel#muted {color:#8493a8;}
        QFrame#card,QGroupBox {background:#141e2b;border:1px solid #2b394b;border-radius:10px;}
        QGroupBox {margin-top:9px;padding-top:14px;}
        QGroupBox::title {subcontrol-origin:margin;left:14px;color:#93a4bc;}
        QPushButton {background:#213045;border:1px solid #354761;border-radius:6px;padding:9px 14px;}
        QPushButton:hover {background:#2e425d;border-color:#6d8bad;}
        QPushButton:disabled {color:#58677c;background:#16202d;}
        QPushButton#primary:disabled {background:#213045;color:#58677c;border-color:#354761;}
        QPushButton#primary {background:#83dfc1;color:#10251e;font-weight:bold;border-color:#83dfc1;}
        QPushButton#record[active="true"] {background:#843d4b;border-color:#df8793;}
        QSpinBox,QDoubleSpinBox,QComboBox {background:#101824;border:1px solid #354761;border-radius:4px;padding:5px;}
        QComboBox QAbstractItemView {background:#1a2532;selection-background-color:#354761;}
        QFileDialog QAbstractItemView,QFileDialog QLineEdit,QFileDialog QComboBox {
            background:#101824;color:#e6edf5;alternate-background-color:#1a2532;
            selection-background-color:#354761;selection-color:#e6edf5;
        }
        QFileDialog QAbstractItemView::item:selected {background:#354761;color:#e6edf5;}
        QFileDialog QLineEdit {border:1px solid #354761;border-radius:4px;padding:5px;}
        QFileDialog QHeaderView,QFileDialog QHeaderView::section,QFileDialog QAbstractScrollArea::corner {
            background:#1a2532;color:#e6edf5;border:0;
        }
        QFileDialog QToolButton {background:#213045;color:#e6edf5;border:1px solid #354761;padding:4px;}
        QFileDialog QToolButton:hover {background:#2e425d;}
        QFileDialog QMenu {background:#101824;color:#e6edf5;}
        QFileDialog QMenu::item:selected {background:#354761;color:#e6edf5;}
        QFileDialog QLineEdit:disabled,QFileDialog QComboBox:disabled,QFileDialog QToolButton:disabled {
            color:#8493a8;
        }
        QCheckBox {spacing:8px;}
        QProgressBar {border:0;background:#263449;border-radius:3px;max-height:6px;}
        QProgressBar::chunk {background:#83dfc1;border-radius:3px;}
        QSlider::groove:horizontal {height:4px;background:#354761;}
        QSlider::handle:horizontal {background:#83dfc1;width:12px;margin:-4px 0;border-radius:6px;}
        QStatusBar {background:#101824;color:#93a4bc;}
        QToolTip {background:#263449;color:white;border:1px solid #354761;}
    )");
    auto *central = new QWidget;
    central->setObjectName("mainContent");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->setSizeConstraint(QLayout::SetMinimumSize);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(central);
    setCentralWidget(scroll);
    auto *header = new QHBoxLayout;
    auto *brand = new QLabel("OpenKeys Piano");
    brand->setObjectName("brand");
    header->addWidget(brand);
    auto *subtitle = new QLabel(QCoreApplication::translate("OpenKeys", "Your keyboard, your piano"));
    subtitle->setObjectName("muted");
    header->addWidget(subtitle);
    header->addStretch();
    auto *about = button(QCoreApplication::translate("OpenKeys", "About"));
    header->addWidget(about);
    auto *demo = button(QCoreApplication::translate("OpenKeys", "Demo"));
    demo->setObjectName("demoSong");
    header->addWidget(demo);
    connect(demo, &QPushButton::clicked, this, [this] { openPath(":/assets/demo.eopiano.json"); });
    auto *settings = button(QCoreApplication::translate("OpenKeys", "Audio Settings"));
    header->addWidget(settings);
    root->addLayout(header);
    auto *languageRow = new QHBoxLayout;
    languageRow->addStretch();
    languageRow->addWidget(new QLabel(QStringLiteral("Language / 语言")));
    auto *languageChoice = new QComboBox;
    languageChoice->setObjectName("languageChoice");
    languageChoice->addItem(QCoreApplication::translate("OpenKeys", "Follow System"), "system");
    languageChoice->addItem(QStringLiteral("English"), "en");
    languageChoice->addItem(QStringLiteral("简体中文"), "zh_CN");
    languageChoice->setCurrentIndex(languageChoice->findData(
        Language::preference(settings_, QCoreApplication::applicationDirPath(), false)));
    languageRow->addWidget(languageChoice);
    root->addLayout(languageRow);
    connect(languageChoice, &QComboBox::activated, this, [this, languageChoice] {
        settings_.setValue("ui/language", languageChoice->currentData());
        settings_.sync();
        statusBar()->showMessage(
            settings_.status() == QSettings::NoError
                ? QCoreApplication::translate("OpenKeys",
                                              "Language saved. Restart OpenKeys Piano to apply it.")
                : QCoreApplication::translate(
                      "OpenKeys", "Could not save the language setting. Check configuration permissions."));
        view_->setFocus();
    });
    connect(about, &QPushButton::clicked, this, [this] {
        releaseHeld();
        QMessageBox::about(
            this, QCoreApplication::translate("OpenKeys", "About OpenKeys Piano"),
            QCoreApplication::translate(
                "OpenKeys",
                "OpenKeys Piano %1\nFree and open-source computer-keyboard piano · GPL-3.0-or-later\n\nQt "
                "6 · FluidSynth · PortAudio\nImport your own soundfont. None is bundled or "
                "downloaded.\n\nReads known EOP v200/v201/v301 containers; unsupported controls are "
                "reported.\nCtrl+Alt+F12: disable background playing and silence all notes.")
                .arg(QCoreApplication::applicationVersion()));
    });
    connect(settings, &QPushButton::clicked, this, [this] { audioSettings(); });
    auto *status = new QHBoxLayout;
    fontLabel_ = new QLabel(QCoreApplication::translate("OpenKeys", "○ Import a soundfont to start playing"));
    fontLabel_->setObjectName("soundfontStatus");
    status->addWidget(fontLabel_, 1);
    fontButton_ = button(QCoreApplication::translate("OpenKeys", "Import Soundfont"));
    fontButton_->setObjectName("importSoundfont");
    status->addWidget(fontButton_);
    auto *guide = button(QCoreApplication::translate("OpenKeys", "Import Guide"));
    guide->setObjectName("soundfontGuide");
    status->addWidget(guide);
    connect(guide, &QPushButton::clicked, this, [this] { showSoundfontGuide(); });
    background_ = new QCheckBox(QCoreApplication::translate("OpenKeys", "Background Playing"));
    status->addWidget(background_);
    auto *panic = button(QCoreApplication::translate("OpenKeys", "Panic"));
    panic->setToolTip(
        QCoreApplication::translate("OpenKeys", "Ctrl+Alt+F12 · Also disables background playing"));
    status->addWidget(panic);
    root->addLayout(status);
    importProgress_ = new QProgressBar;
    importProgress_->setObjectName("fontImportProgress");
    importProgress_->setStyleSheet("QProgressBar {min-height:20px;max-height:20px;}");
    importProgress_->setTextVisible(true);
    importProgress_->hide();
    root->addWidget(importProgress_);
    connect(fontButton_, &QPushButton::clicked, this, [this] { chooseSoundfont(); });
    connect(panic, &QPushButton::clicked, this, [this] { emergencyStop(); });
    auto *transport = new QFrame;
    transport->setObjectName("card");
    auto *transportLayout = new QVBoxLayout(transport);
    auto *track = new QHBoxLayout;
    titleLabel_ = new QLabel(QCoreApplication::translate("OpenKeys", "Free Play"));
    titleLabel_->setObjectName("trackTitle");
    titleLabel_->setStyleSheet("font-size:16px;font-weight:600");
    track->addWidget(titleLabel_, 1);
    recordLabel_ = new QLabel(QCoreApplication::translate("OpenKeys", "Play locally · Works offline"));
    recordLabel_->setObjectName("muted");
    track->addWidget(recordLabel_);
    transportLayout->addLayout(track);
    auto *row = new QHBoxLayout;
    playButton_ = button(QCoreApplication::translate("OpenKeys", "▶ Play"));
    playButton_->setObjectName("primary");
    playButton_->setEnabled(false);
    row->addWidget(playButton_);
    auto *stop = button(QCoreApplication::translate("OpenKeys", "■ Stop"));
    row->addWidget(stop);
    recordButton_ = button(QCoreApplication::translate("OpenKeys", "● Record"));
    recordButton_->setEnabled(false);
    recordButton_->setObjectName("record");
    row->addWidget(recordButton_);
    auto *open = button(QCoreApplication::translate("OpenKeys", "Open EOP / Recording"));
    row->addWidget(open);
    auto *save = button(QCoreApplication::translate("OpenKeys", "Save Recording"));
    row->addWidget(save);
    row->addStretch();
    row->addWidget(new QLabel(QCoreApplication::translate("OpenKeys", "Speed")));
    speed_ = new ArrowOnlySpin<QDoubleSpinBox>;
    speed_->setObjectName("playbackSpeed");
    speed_->setRange(.25, 2);
    speed_->setSingleStep(.05);
    speed_->setValue(1);
    speed_->setSuffix(" ×");
    speed_->setKeyboardTracking(false);
    row->addWidget(speed_);
    timeLabel_ = new QLabel("00:00 / 00:00");
    timeLabel_->setMinimumWidth(110);
    row->addWidget(timeLabel_);
    transportLayout->addLayout(row);
    progress_ = new PlaybackSlider;
    progress_->setObjectName("playbackProgress");
    progress_->setTracking(false);
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    connect(progress_, &QSlider::sliderMoved, this, [this](int value) {
        timeLabel_->setText(timeText(song_.durationUs() * value / 1000) + " / " +
                            timeText(song_.durationUs()));
    });
    connect(progress_, &QSlider::valueChanged, this, [this](int value) {
        if (!progress_->isSliderDown())
            seekPlayback(value);
    });
    transportLayout->addWidget(progress_);
    root->addWidget(transport);
    connect(playButton_, &QPushButton::clicked, this, [this] {
        if (recordingActive_ || !audio_.status().ready || !audio_.status().active)
            return;
        if (audio_.status().playing)
            audio_.pause();
        else
            startPlayback();
        syncPerformance(pendingPlayRequest_ ? pendingPlayPosition_ : audio_.status().positionUs);
        view_->setFocus();
    });
    connect(stop, &QPushButton::clicked, this, [this] {
        if (recordingActive_)
            finishRecord();
        else
            audio_.stop();
        playbackConfiguration_ = false;
        cancelSeekDrag();
        pendingPlayRequest_ = 0;
        syncPerformance(0);
        view_->setFocus();
    });
    connect(recordButton_, &QPushButton::clicked, this, [this] {
        toggleRecord();
        view_->setFocus();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        if (recordingActive_)
            finishRecord();
        releaseHeld();
        if (!maybeSave())
            return;
        auto path = QFileDialog::getOpenFileName(
            this, QCoreApplication::translate("OpenKeys", "Open Performance"), {},
            QCoreApplication::translate("OpenKeys", "Performances (*.eop *.eopiano.json);;All files (*)"));
        if (!path.isEmpty())
            openPath(path);
    });
    connect(save, &QPushButton::clicked, this, [this] { saveSong(); });
    connect(speed_, &QDoubleSpinBox::valueChanged, this, [this](double value) { audio_.setSpeed(value); });
    auto *performance = new QHBoxLayout;
    auto *keyGroup = new QGroupBox(QCoreApplication::translate("OpenKeys", "Performance Settings"));
    auto *keyRow = new QVBoxLayout(keyGroup);
    const auto field = [](const QString &name, QWidget *control) {
        auto *group = new QWidget;
        auto *layout = new QHBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->setSizeConstraint(QLayout::SetFixedSize);
        layout->addWidget(new QLabel(name));
        control->setFixedWidth(control->sizeHint().width());
        layout->addWidget(control);
        return group;
    };
    auto *topRow = new QHBoxLayout;
    auto *handsRow = new QHBoxLayout;
    topRow->setSpacing(20);
    handsRow->setSpacing(20);
    keyRow->addLayout(topRow);
    keyRow->addLayout(handsRow);
    keyGroup->setToolTip(QCoreApplication::translate(
        "OpenKeys",
        "Key and octave changes affect new live notes only. Playback uses the pitches in the file."));
    auto *transpose = performanceSpin(-12, 12, keyboard_.keymap.transpose, PerformanceSpin::Key);
    transpose->setObjectName("transpose");
    topRow->addWidget(field(QCoreApplication::translate("OpenKeys", "Key"), transpose));
    topRow->addStretch();
    for (int hand = 0; hand < 2; ++hand) {
        auto *octave = performanceSpin(-3, 3, keyboard_.keymap.octave[size_t(hand)], PerformanceSpin::Octave);
        octave->setObjectName(hand == 0 ? "leftOctave" : "rightOctave");
        handsRow->addWidget(field(hand == 0 ? QCoreApplication::translate("OpenKeys", "Left Octave")
                                            : QCoreApplication::translate("OpenKeys", "Right Octave"),
                                  octave));
        auto *velocity = performanceSpin(0, 100, velocityPercent(keyboard_.keymap.velocity[size_t(hand)]),
                                         PerformanceSpin::Number);
        velocity->setObjectName(hand == 0 ? "leftVelocity" : "rightVelocity");
        handsRow->addWidget(field(QCoreApplication::translate("OpenKeys", "Velocity (%)"), velocity));
        connect(octave, &QSpinBox::valueChanged, this, [this, hand](int value) {
            keyboard_.keymap.octave[size_t(hand)] = value;
            updateProfile();
        });
        connect(velocity, &QSpinBox::valueChanged, this, [this, hand](int value) {
            keyboard_.keymap.velocity[size_t(hand)] = midiVelocity(value);
            updateProfile();
        });
    }
    connect(transpose, &QSpinBox::valueChanged, this, [this](int value) {
        keyboard_.keymap.transpose = value;
        updateProfile();
    });
    auto *profile = button(QCoreApplication::translate("OpenKeys", "Keymap"));
    handsRow->addStretch();
    topRow->addWidget(profile);
    connect(profile, &QPushButton::clicked, this, [this] { profileSettings(); });
    performance->addWidget(keyGroup, 1);
    root->addLayout(performance);
    view_ = new KeyboardView;
    view_->setObjectName("keyboardView");
    view_->map = keyboard_.keymap;
    root->addWidget(view_, 1);
    view_->editKey = [this](uint16_t key) { editKey(key); };
    view_->mouseKey = [this](uint16_t code, bool down) { key(code, down, KeySource::Mouse); };
    view_->mouseNote = [this](int note, bool down) {
        if (down) {
            if (!canPlayLive(true))
                return;
            mouseHeld_ = note;
        } else {
            if (mouseHeld_ < 0)
                return;
            note = mouseHeld_;
            mouseHeld_ = -1;
        }
        dispatchLive(Event{0, down ? Kind::NoteOn : Kind::NoteOff, uint8_t(note), uint8_t(down ? 100 : 0),
                           Hand::Right, 0, 0});
    };
    auto *foot = new QHBoxLayout;
    auto *hint = new QLabel(QCoreApplication::translate(
        "OpenKeys",
        "Click or drag to play · Right-click to edit · Independent hand velocities · Original EOP keys"));
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    foot->addWidget(hint, 1);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(settings_.value("volume", 80).toInt());
    volume->setFixedWidth(120);
    volume->setToolTip(QCoreApplication::translate("OpenKeys", "Master volume"));
    foot->addWidget(new QLabel(QCoreApplication::translate("OpenKeys", "Volume")));
    foot->addWidget(volume);
    root->addLayout(foot);
    connect(volume, &QSlider::valueChanged, this, [this](int value) {
        audio_.setVolume(value / 100.);
        settings_.setValue("volume", value);
    });
    deviceLabel_ = new QLabel(QCoreApplication::translate("OpenKeys", "Audio has not started"));
    statusBar()->addPermanentWidget(deviceLabel_);
}
bool MainWindow::canPlayLive(bool notify) {
    const auto status = audio_.status();
    if (status.ready && status.active && !loading_)
        return true;
    if (notify)
        statusBar()->showMessage(
            loading_ ? QCoreApplication::translate("OpenKeys",
                                                   "The soundfont is loading. Please wait before playing.")
            : !status.ready ? QCoreApplication::translate(
                                  "OpenKeys", "Choose Import Soundfont and import an SF2 to start playing.")
                            : QCoreApplication::translate(
                                  "OpenKeys", "Audio has not started. Connect a device in Audio Settings."),
            5000);
    return false;
}
void MainWindow::key(uint16_t code, bool down, KeySource source) {
    if (down && code != 42 && code != 54 && !keyboard_.keymap.bindings.contains(code))
        return;
    if (!keyHolds_.change(code, down, source))
        return;
    if (down)
        view_->pressedKeys.insert(code);
    else
        view_->pressedKeys.erase(code);
    view_->update();
    if (down && !canPlayLive(source == KeySource::Mouse))
        return;
    for (auto e : keyboard_.change(code, down))
        dispatchLive(e);
}
void MainWindow::dispatchLive(const Event &input) {
    auto event = input;
    event.source = 0;
    audio_.send(event);
    applyVisual(view_, event, false);
    view_->update();
    if (recordingActive_) {
        event.timeUs = recordingClock_.nsecsElapsed() / 1000;
        // Keep headroom for the matching note-offs generated by finishRecord().
        event.timeUs = std::min(event.timeUs, MaxSongUs);
        recording_.events.push_back(event);
        if (recording_.events.size() >= 400000 - 2048 || event.timeUs >= MaxSongUs - 1000000) {
            finishRecord();
            statusBar()->showMessage(
                QCoreApplication::translate(
                    "OpenKeys", "Recording stopped because the duration or event limit was reached."),
                10000);
        }
    }
}
std::vector<Event> MainWindow::takeLiveReleases() {
    keyHolds_.clear();
    view_->pressedKeys.clear();
    view_->resetMouseInteraction();
    auto events = keyboard_.releaseAll();
    if (mouseHeld_ >= 0) {
        events.push_back(Event{0, Kind::NoteOff, uint8_t(mouseHeld_), 0, Hand::Right, 0, 0});
        mouseHeld_ = -1;
    }
    return events;
}
void MainWindow::releaseHeld() {
    for (auto e : takeLiveReleases())
        dispatchLive(e);
    if (view_) {
        view_->liveKeys.clear();
        for (auto &hand : view_->liveNotes)
            hand.fill(0);
        view_->update();
    }
    audio_.releaseLive();
}
void MainWindow::emergencyStop() {
    if (recordingActive_)
        finishRecord();
    else
        releaseHeld();
    if (background_->isChecked())
        background_->setChecked(false);
    playbackConfiguration_ = false;
    cancelSeekDrag();
    pendingPlayRequest_ = 0;
    syncPerformance(0);
    audio_.panic();
    statusBar()->showMessage(
        QCoreApplication::translate("OpenKeys", "All notes silenced. Background playing is off."), 5000);
    view_->setFocus();
}
void MainWindow::setSong(Song song) {
    releaseHeld();
    audio_.setSong(song);
    song_ = std::move(song);
    playbackConfiguration_ = false;
    cancelSeekDrag();
    pendingPlayRequest_ = 0;
    syncPerformance(0);
    dirty_ = false;
    visualCursor_ = 0;
    visualTime_ = -1;
    view_->playbackKeys.clear();
    for (auto &h : view_->playbackNotes)
        h.fill(0);
    titleLabel_->setText(song_.title.isEmpty() ? QCoreApplication::translate("OpenKeys", "Untitled Recording")
                                               : song_.title);
    playButton_->setEnabled(!song_.events.empty() && audio_.status().ready && audio_.status().active &&
                            !loading_);
    recordLabel_->setText(
        QCoreApplication::translate("OpenKeys", "Loaded · Events: %1").arg(song_.events.size()));
    audio_.setSpeed(speed_->value());
    view_->update();
}
void MainWindow::openPath(const QString &path) {
    if (recordingActive_)
        finishRecord();
    if (!maybeSave())
        return;
    try {
        auto song = readSong(path);
        if (path == ":/assets/demo.eopiano.json")
            song.title = QCoreApplication::translate("OpenKeys", "First Encounter · OpenKeys Demo");
        if (song.title.isEmpty())
            song.title = QFileInfo(path).completeBaseName();
        const auto warnings = song.warnings;
        setSong(std::move(song));
        if (!warnings.empty())
            QMessageBox::information(this, QCoreApplication::translate("OpenKeys", "EOP Compatibility"),
                                     warnings.join('\n'));
        statusBar()->showMessage(path.startsWith(":/")
                                     ? QCoreApplication::translate("OpenKeys", "Demo opened")
                                     : QCoreApplication::translate("OpenKeys", "Opened: ") + path,
                                 10000);
    } catch (const FileError &e) {
        showError(QCoreApplication::translate("OpenKeys", "Could Not Open Performance"),
                  QString::fromUtf8(e.what()));
    }
}
void MainWindow::toggleRecord() {
    if (recordingActive_) {
        finishRecord();
        return;
    }
    if (!audio_.status().ready || !audio_.status().active) {
        showError(
            QCoreApplication::translate("OpenKeys", "Recording Unavailable"),
            QCoreApplication::translate("OpenKeys", "Import a soundfont and enable an audio device first."));
        return;
    }
    if (!maybeSave())
        return;
    releaseHeld();
    playbackConfiguration_ = false;
    cancelSeekDrag();
    pendingPlayRequest_ = 0;
    syncPerformance(0);
    audio_.setSong({});
    song_ = {};
    recording_ = {};
    recording_.title = QCoreApplication::translate("OpenKeys", "Untitled Recording");
    recordingActive_ = true;
    recordingClock_.start();
    titleLabel_->setText(QCoreApplication::translate("OpenKeys", "Recording"));
    recordButton_->setText(QCoreApplication::translate("OpenKeys", "■ Finish Recording"));
    recordButton_->setProperty("active", true);
    recordButton_->setStyleSheet({});
    playButton_->setEnabled(false);
}
void MainWindow::finishRecord() {
    if (!recordingActive_)
        return;
    // Append release events directly before disabling recording, without recursive limits.
    auto events = takeLiveReleases();
    for (auto e : events) {
        e.timeUs = std::min(recordingClock_.nsecsElapsed() / 1000, qint64(MaxSongUs));
        recording_.events.push_back(e);
        audio_.send(e);
    }
    recordingActive_ = false;
    recordButton_->setText(QCoreApplication::translate("OpenKeys", "● Record"));
    recordButton_->setProperty("active", false);
    recordButton_->setStyleSheet({});
    try {
        setSong(recording_);
        dirty_ = !song_.events.empty();
        recordLabel_->setText(dirty_ ? QCoreApplication::translate("OpenKeys", "Recording finished · Unsaved")
                                     : QCoreApplication::translate("OpenKeys", "Empty recording"));
    } catch (const FileError &e) {
        showError(QCoreApplication::translate("OpenKeys", "Invalid Recording"), e.what());
    }
}
bool MainWindow::saveSong() {
    if (recordingActive_)
        finishRecord();
    releaseHeld();
    if (song_.events.empty()) {
        statusBar()->showMessage(
            QCoreApplication::translate("OpenKeys", "There is no performance to save yet."), 5000);
        return false;
    }
    QFileDialog dialog(this, QCoreApplication::translate("OpenKeys", "Save Recording"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QCoreApplication::translate("OpenKeys", "OpenKeys recordings (*.eopiano.json)"));
    dialog.setDefaultSuffix("eopiano.json");
    dialog.selectFile(song_.title + ".eopiano.json");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    const auto path = dialog.selectedFiles().first();
    try {
        saveRecording(path, song_);
        dirty_ = false;
        recordLabel_->setText(QCoreApplication::translate("OpenKeys", "Saved"));
        statusBar()->showMessage(QCoreApplication::translate("OpenKeys", "Saved: ") + path, 8000);
        return true;
    } catch (const FileError &e) {
        showError(QCoreApplication::translate("OpenKeys", "Save Failed"), e.what());
        return false;
    }
}
bool MainWindow::maybeSave() {
    if (!dirty_)
        return true;
    releaseHeld();
    const auto answer = QMessageBox::question(
        this, QCoreApplication::translate("OpenKeys", "Save Recording"),
        QCoreApplication::translate("OpenKeys", "The current recording has not been saved. Save it now?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveSong();
    dirty_ = false;
    return true;
}
void MainWindow::loadFont(const QString &path) {
    beginFontTask(path, true);
}
void MainWindow::chooseSoundfont() {
    releaseHeld();
    const auto path =
        QFileDialog::getOpenFileName(this, QCoreApplication::translate("OpenKeys", "Import SF2 Soundfont"),
                                     legacyFontPath_, "SoundFont (*.sf2 *.SF2)");
    if (!path.isEmpty())
        loadFont(path);
}
void MainWindow::showSoundfontGuide() {
    releaseHeld();
    QDialog dialog(this);
    dialog.setObjectName("soundfontGuideDialog");
    dialog.setWindowTitle(QCoreApplication::translate("OpenKeys", "Soundfont Import Guide"));
    dialog.resize(800, 640);
    auto *layout = new QVBoxLayout(&dialog);
    auto *browser = new QTextBrowser;
    browser->setObjectName("soundfontGuideContent");
    browser->setStyleSheet(
        "QTextBrowser {background:#111923;color:#e6edf5;border:1px solid #354761;padding:12px;}");
    auto palette = browser->palette();
    palette.setColor(QPalette::Link, QColor("#83dfc1"));
    browser->setPalette(palette);
    browser->setOpenLinks(false);
    connect(browser, &QTextBrowser::anchorClicked, &dialog, [browser](const QUrl &link) {
        if (link.scheme() == "https")
            QDesktopServices::openUrl(link);
        else if (link.fileName() == "soundfont.md" || link.fileName() == "soundfont.zh-CN.md")
            browser->setSource(QUrl("qrc:/docs/" + link.fileName()), QTextDocument::MarkdownResource);
        else if (link.isRelative() && link.hasFragment())
            browser->scrollToAnchor(link.fragment());
    });
    const auto guidePath =
        Language::current() == "zh_CN" ? "qrc:/docs/soundfont.zh-CN.md" : "qrc:/docs/soundfont.md";
    browser->setSource(QUrl(guidePath), QTextDocument::MarkdownResource);
    if (browser->toPlainText().isEmpty())
        browser->setPlainText(QCoreApplication::translate(
            "OpenKeys", "Could not read the guide. See soundfont.md in the installed docs directory."));
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
void MainWindow::beginFontTask(const QString &path, bool importing) {
    if (loading_)
        return;
    if (recordingActive_)
        finishRecord();
    releaseHeld();
    audio_.shutdown();
    wasAudioActive_ = false;
    loading_ = true;
    importing_ = importing;
    fontButton_->setEnabled(false);
    importProgress_->show();
    importProgress_->setRange(0, 100);
    importProgress_->setValue(0);
    importProgress_->setFormat(importing ? QCoreApplication::translate("OpenKeys", "Copying soundfont %p%")
                                         : QCoreApplication::translate("OpenKeys", "Loading soundfont…"));
    if (!importing)
        importProgress_->setRange(0, 0);
    fontLabel_->setText(importing ? QCoreApplication::translate("OpenKeys", "◌ Importing soundfont…")
                                  : QCoreApplication::translate("OpenKeys", "◌ Loading soundfont…"));
    const auto desired = settings_.value("audio/deviceName").toString();
    config_.device = -1;
    if (!diagnostic_ && !desired.isEmpty())
        for (const auto &device : audio_.devices())
            if (device.name == desired) {
                config_.device = device.id;
                break;
            }
    const double rate = diagnostic_ ? 48000 : audio_.preferredSampleRate(config_);
    const auto directory = soundfontDirectory_;
    const auto info = importedInfo_;
    cancelImport_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancelImport_;
    QPointer<MainWindow> window(this);
    loader_.setFuture(QtConcurrent::run([path, directory, info, rate, importing, cancel, window] {
        const auto progress = [cancel, window](ImportPhase phase, int percent) {
            if (cancel->load())
                return false;
            if (window)
                QMetaObject::invokeMethod(
                    window,
                    [window, phase, percent] {
                        if (!window || !window->loading_)
                            return;
                        if (phase == ImportPhase::Copying) {
                            window->importProgress_->setRange(0, 100);
                            window->importProgress_->setValue(percent);
                            window->importProgress_->setFormat(
                                QCoreApplication::translate("OpenKeys", "Copying soundfont %p%"));
                        } else {
                            window->importProgress_->setRange(0, 0);
                            window->importProgress_->setFormat(
                                QCoreApplication::translate("OpenKeys", "Loading soundfont…"));
                        }
                    },
                    Qt::QueuedConnection);
            return true;
        };
        if (importing)
            return importSoundfont(path, directory, rate, progress);
        try {
            auto loaded = AudioEngine::loadSoundfont(path, rate);
            return ImportedSoundfont{std::move(loaded.font), info, {}, loaded.error, {}};
        } catch (const std::exception &error) {
            return ImportedSoundfont{{}, info, {}, QString::fromUtf8(error.what()), "soundfont_load_failed"};
        }
    }));
}
void MainWindow::startAudio(bool allowReload) {
    if (diagnostic_ || !audio_.status().ready)
        return;
    const auto desired = settings_.value("audio/deviceName").toString();
    config_.device = -1;
    if (!desired.isEmpty())
        for (const auto &device : audio_.devices())
            if (device.name == desired) {
                config_.device = device.id;
                break;
            }
    if (allowReload && std::abs(audio_.status().sampleRate - audio_.preferredSampleRate(config_)) > .5) {
        beginFontTask(fontPath_, false);
        return;
    }
    const auto notice = audio_.start(config_);
    audio_.setVolume(settings_.value("volume", 80).toDouble() / 100.);
    audio_.setSpeed(speed_->value());
    if (!notice.isEmpty())
        statusBar()->showMessage(notice, 20000);
}
void MainWindow::cancelSeekDrag() {
    if (progress_) {
        QSignalBlocker block(progress_);
        progress_->setSliderDown(false);
    }
}
void MainWindow::seekPlayback(int value) {
    if (recordingActive_ || song_.events.empty() || song_.durationUs() <= 0)
        return;
    audio_.seek(song_.durationUs() * value / 1000);
    pendingPlayRequest_ = 0;
    playbackConfiguration_ = !song_.configurations.empty();
    visualTime_ = -1;
    visualCursor_ = 0;
    view_->playbackKeys.clear();
    for (auto &notes : view_->playbackNotes)
        notes.fill(0);
    tick();
}
void MainWindow::startPlayback() {
    const auto status = audio_.status();
    pendingPlayPosition_ = status.positionUs >= song_.durationUs() ? 0 : status.positionUs;
    pendingPlayRequest_ = audio_.play();
    playbackConfiguration_ = pendingPlayRequest_ && !song_.configurations.empty();
    syncPerformance(pendingPlayPosition_);
}
void MainWindow::syncPerformance(int64_t positionUs) {
    const auto *config = playbackConfiguration_ ? song_.configurationAt(positionUs) : nullptr;
    const auto sync = [this, config](const char *name, std::optional<int> value, int live,
                                     bool percent = false) {
        auto *control = findChild<QSpinBox *>(QLatin1String(name));
        QSignalBlocker blocker(control);
        control->setEnabled(!config);
        const int raw = config ? value.value_or(0) : live;
        control->setValue(percent ? velocityPercent(raw) : raw);
        auto *edit = control->findChild<QLineEdit *>();
        // Force refresh even when restoring a value equal to the previous one.
        edit->setText(config && !value ? QCoreApplication::translate("OpenKeys", "Unknown")
                                       : static_cast<PerformanceSpin *>(control)->displayText());
    };
    sync("transpose", config ? std::optional<int>(config->transpose) : std::nullopt,
         keyboard_.keymap.transpose);
    sync("leftOctave", config ? config->octave[0] : std::nullopt, keyboard_.keymap.octave[0]);
    sync("rightOctave", config ? config->octave[1] : std::nullopt, keyboard_.keymap.octave[1]);
    sync("leftVelocity", config ? config->velocity[0] : std::nullopt, keyboard_.keymap.velocity[0], true);
    sync("rightVelocity", config ? config->velocity[1] : std::nullopt, keyboard_.keymap.velocity[1], true);
}
void MainWindow::tick() {
    const auto status = audio_.status();
    if (status.overflows != lastOverflow_) {
        lastOverflow_ = status.overflows;
        emergencyStop();
        statusBar()->showMessage(
            QCoreApplication::translate(
                "OpenKeys",
                "Performance event overload. All notes silenced and background playing disabled."),
            12000);
    }
    if (status.underruns != lastUnderruns_) {
        lastUnderruns_ = status.underruns;
        statusBar()->showMessage(
            QCoreApplication::translate(
                "OpenKeys", "Audio underrun detected. Try increasing the buffer in Audio Settings."),
            8000);
    }
    if (wasAudioActive_ && !status.active && !loading_ && !diagnostic_) {
        if (recordingActive_)
            finishRecord();
        releaseHeld();
        audio_.shutdown();
        playbackConfiguration_ = false;
        cancelSeekDrag();
        pendingPlayRequest_ = 0;
        statusBar()->showMessage(
            QCoreApplication::translate(
                "OpenKeys",
                "The audio device stopped. Performance state cleared; reconnect in Audio Settings."),
            15000);
    }
    wasAudioActive_ = status.active;
    recordButton_->setEnabled(recordingActive_ || (status.ready && status.active && !loading_));
    playButton_->setText(status.playing ? QCoreApplication::translate("OpenKeys", "Ⅱ Pause")
                                        : QCoreApplication::translate("OpenKeys", "▶ Play"));
    playButton_->setEnabled(!recordingActive_ && !song_.events.empty() && status.active);
    const auto pos = recordingActive_ ? recordingClock_.nsecsElapsed() / 1000 : status.positionUs;
    progress_->setEnabled(!recordingActive_ && !song_.events.empty() && song_.durationUs() > 0);
    if (!progress_->isSliderDown()) {
        timeLabel_->setText(timeText(pos) + " / " + timeText(song_.durationUs()));
        QSignalBlocker block(progress_);
        progress_->setValue(song_.durationUs() > 0
                                ? int(std::min<int64_t>(1000, status.positionUs * 1000 / song_.durationUs()))
                                : 0);
    }
    if (recordingActive_)
        recordLabel_->setText(QCoreApplication::translate("OpenKeys", "● Recording · ") + timeText(pos));
    if (recordingActive_ && pos >= MaxSongUs - 1000000)
        finishRecord();
    if (status.positionUs < visualTime_) {
        visualCursor_ = 0;
        view_->playbackKeys.clear();
        for (auto &h : view_->playbackNotes)
            h.fill(0);
    }
    while (visualCursor_ < song_.events.size() && song_.events[visualCursor_].timeUs <= status.positionUs)
        applyVisual(view_, song_.events[visualCursor_++], true);
    visualTime_ = status.positionUs;
    if (pendingPlayRequest_ && status.processedPlayRequest >= pendingPlayRequest_)
        pendingPlayRequest_ = 0;
    if (!pendingPlayRequest_ && !status.playing && status.positionUs >= song_.durationUs())
        playbackConfiguration_ = false;
    syncPerformance(pendingPlayRequest_ ? pendingPlayPosition_ : status.positionUs);
    view_->showPlayback = status.playing;
    view_->update();
    deviceLabel_->setText(
        status.active
            ? QCoreApplication::translate("OpenKeys", "%1 Hz · Reported buffer latency %2 ms · Underruns %3")
                  .arg(int(status.sampleRate))
                  .arg(status.reportedLatencyMs, 0, 'f', 1)
                  .arg(status.underruns)
            : QCoreApplication::translate("OpenKeys", "Audio inactive · Reconnect in settings"));
}
void MainWindow::audioSettings() {
    if (recordingActive_)
        finishRecord();
    releaseHeld();
    audio_.refreshDevices();
    QDialog dialog(this);
    dialog.setWindowTitle(QCoreApplication::translate("OpenKeys", "Audio Settings"));
    auto *form = new QFormLayout(&dialog);
    auto *devices = new QComboBox;
    devices->addItem(QCoreApplication::translate("OpenKeys", "System Default"), -1);
    for (const auto &device : audio_.devices()) {
        devices->addItem(device.name, device.id);
        if (device.name == settings_.value("audio/deviceName").toString())
            devices->setCurrentIndex(devices->count() - 1);
    }
    form->addRow(QCoreApplication::translate("OpenKeys", "Output Device"), devices);
    auto *frames = new QComboBox;
    for (int n : {64, 128, 256, 512, 1024, 2048}) {
        frames->addItem(QString::number(n) + QCoreApplication::translate("OpenKeys", " frames"), n);
        if (n == config_.frames)
            frames->setCurrentIndex(frames->count() - 1);
    }
    form->addRow(QCoreApplication::translate("OpenKeys", "Requested Buffer"), frames);
    auto *exclusive = new QCheckBox(QCoreApplication::translate(
        "OpenKeys", "WASAPI exclusive mode (falls back to shared if unavailable)"));
    exclusive->setChecked(config_.exclusive);
#ifndef Q_OS_WIN
    exclusive->setEnabled(false);
#endif
    form->addRow(exclusive);
    auto *hint = new QLabel(QCoreApplication::translate(
        "OpenKeys",
        "Smaller buffers can reduce latency. Increase the buffer if you hear clicks or get underruns.\nThe "
        "displayed latency is reported by the driver, not measured key-to-sound latency."));
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("OpenKeys", "OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("OpenKeys", "Cancel"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        startAudio();
        return;
    }
    config_.frames = frames->currentData().toInt();
    config_.exclusive = exclusive->isChecked();
    settings_.setValue("audio/deviceName", devices->currentIndex() ? devices->currentText() : QString());
    settings_.setValue("audio/frames", config_.frames);
    settings_.setValue("audio/exclusive", config_.exclusive);
    startAudio();
    view_->setFocus();
}
void MainWindow::editKey(uint16_t code) {
    releaseHeld();
    QDialog dialog(this);
    dialog.setWindowTitle(QCoreApplication::translate("OpenKeys", "Edit Key · ") + keyName(code));
    auto *form = new QFormLayout(&dialog);
    Binding binding{code, 60, Hand::Left, false};
    auto it = keyboard_.keymap.bindings.find(code);
    if (it != keyboard_.keymap.bindings.end())
        binding = it->second;
    auto *type = new QComboBox;
    type->setObjectName("bindingType");
    type->addItems({QCoreApplication::translate("OpenKeys", "Note"),
                    QCoreApplication::translate("OpenKeys", "Sustain Pedal (Both Hands)"),
                    QCoreApplication::translate("OpenKeys", "Unassigned")});
    type->setCurrentIndex(it == keyboard_.keymap.bindings.end() ? 2 : binding.sustain ? 1 : 0);
    form->addRow(QCoreApplication::translate("OpenKeys", "Function"), type);
    static constexpr int semitones[] = {0, 2, 4, 5, 7, 9, 11};
    const QStringList names = {"C", "D", "E", "F", "G", "A", "B"};
    auto *notes = new QButtonGroup(&dialog);
    auto *noteRow = new QHBoxLayout;
    int initialNote = 0;
    for (int i = 0; i < 7; ++i) {
        auto *choice = new QPushButton(names[i]);
        choice->setObjectName("note" + names[i]);
        choice->setCheckable(true);
        choice->setAutoDefault(false);
        notes->addButton(choice, i);
        noteRow->addWidget(choice);
        if (semitones[i] <= binding.note % 12)
            initialNote = i;
    }
    notes->button(initialNote)->setChecked(true);
    form->addRow(QCoreApplication::translate("OpenKeys", "Note Name"), noteRow);
    auto *octave = spin(-1, 9, binding.note / 12 - 1);
    octave->setObjectName("noteOctave");
    form->addRow(QCoreApplication::translate("OpenKeys", "Octave"), octave);
    auto *accidental = new QComboBox;
    accidental->setObjectName("noteAccidental");
    accidental->addItems({QCoreApplication::translate("OpenKeys", "Flat ♭"),
                          QCoreApplication::translate("OpenKeys", "Natural ♮"),
                          QCoreApplication::translate("OpenKeys", "Sharp ♯")});
    accidental->setCurrentIndex(binding.note % 12 == semitones[initialNote] ? 1 : 2);
    form->addRow(QCoreApplication::translate("OpenKeys", "Accidental"), accidental);
    auto *name = new QLabel;
    name->setObjectName("notePreview");
    form->addRow(QCoreApplication::translate("OpenKeys", "Resulting Note"), name);
    form->addRow(new QLabel(QCoreApplication::translate(
        "OpenKeys", "C4 is middle C. The octave identifies the note's register.")));
    const auto pitch = [=] {
        return 12 * (octave->value() + 1) + semitones[notes->checkedId()] + accidental->currentIndex() - 1;
    };
    auto *hand = new QComboBox;
    hand->setObjectName("bindingHand");
    hand->addItems({QCoreApplication::translate("OpenKeys", "Left"),
                    QCoreApplication::translate("OpenKeys", "Right"),
                    QCoreApplication::translate("OpenKeys", "Either")});
    hand->setCurrentIndex(int(binding.hand));
    form->addRow(QCoreApplication::translate("OpenKeys", "Hand"), hand);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("OpenKeys", "OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("OpenKeys", "Cancel"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto refresh = [=] {
        const bool isNote = type->currentIndex() == 0;
        for (auto *choice : notes->buttons())
            choice->setEnabled(isNote);
        octave->setEnabled(isNote);
        accidental->setEnabled(isNote);
        const int n = pitch();
        const bool valid = n >= 0 && n <= 127;
        const QString suffix = accidental->currentIndex() == 0   ? QStringLiteral("♭")
                               : accidental->currentIndex() == 2 ? QStringLiteral("♯")
                                                                 : QString();
        name->setText(!isNote ? QStringLiteral("—")
                      : valid ? names[notes->checkedId()] + suffix + QString::number(octave->value())
                              : QCoreApplication::translate("OpenKeys", "Outside the supported pitch range"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled((!isNote || valid) && code != 42 && code != 54);
    };
    connect(notes, &QButtonGroup::idClicked, &dialog, refresh);
    connect(octave, &QSpinBox::valueChanged, &dialog, refresh);
    connect(accidental, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(type, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();
    if (code == 42 || code == 54) {
        auto *hint = new QLabel(QCoreApplication::translate(
            "OpenKeys", "Shift is reserved for velocity modifiers and cannot be reassigned."));
        form->addRow(hint);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    if (dialog.exec() == QDialog::Accepted) {
        if (type->currentIndex() == 2)
            keyboard_.keymap.bindings.erase(code);
        else
            keyboard_.keymap.bindings[code] = {code, type->currentIndex() == 0 ? pitch() : binding.note,
                                               Hand(hand->currentIndex()), type->currentIndex() == 1};
        updateProfile();
    }
    view_->setFocus();
}
void MainWindow::profileSettings() {
    releaseHeld();
    QDialog dialog(this);
    dialog.setWindowTitle(QCoreApplication::translate("OpenKeys", "Keymap"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *load = button(QCoreApplication::translate("OpenKeys", "Import Keymap JSON"));
    auto *save = button(QCoreApplication::translate("OpenKeys", "Export Current Keymap"));
    auto *reset = button(QCoreApplication::translate("OpenKeys", "Restore Default Two-Hand Layout"));
    layout->addWidget(load);
    layout->addWidget(save);
    layout->addWidget(reset);
    connect(load, &QPushButton::clicked, &dialog, [&] {
        auto path =
            QFileDialog::getOpenFileName(&dialog, QCoreApplication::translate("OpenKeys", "Import Keymap"),
                                         {}, QCoreApplication::translate("OpenKeys", "Keymaps (*.json)"));
        if (path.isEmpty())
            return;
        try {
            keyboard_.keymap = Keymap::read(path);
            updateProfile();
            dialog.accept();
        } catch (const FileError &e) {
            showError(QCoreApplication::translate("OpenKeys", "Import Failed"), e.what());
        }
    });
    connect(save, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getSaveFileName(
            &dialog, QCoreApplication::translate("OpenKeys", "Export Keymap"), "keyboard.json",
            QCoreApplication::translate("OpenKeys", "Keymaps (*.json)"));
        if (path.isEmpty())
            return;
        try {
            keyboard_.keymap.save(path);
            dialog.accept();
        } catch (const FileError &e) {
            showError(QCoreApplication::translate("OpenKeys", "Export Failed"), e.what());
        }
    });
    connect(reset, &QPushButton::clicked, &dialog, [&] {
        keyboard_.keymap = Keymap::defaults();
        updateProfile();
        dialog.accept();
    });
    dialog.exec();
    const auto sync = [this](const char *name, int value) {
        auto *control = findChild<QSpinBox *>(QLatin1String(name));
        QSignalBlocker block(control);
        control->setValue(value);
    };
    sync("transpose", keyboard_.keymap.transpose);
    sync("leftOctave", keyboard_.keymap.octave[0]);
    sync("rightOctave", keyboard_.keymap.octave[1]);
    sync("leftVelocity", velocityPercent(keyboard_.keymap.velocity[0]));
    sync("rightVelocity", velocityPercent(keyboard_.keymap.velocity[1]));
    view_->setFocus();
}
void MainWindow::updateProfile() {
    view_->map = keyboard_.keymap;
    view_->update();
    if (!diagnostic_)
        try {
            keyboard_.keymap.save(profilePath_);
        } catch (const FileError &e) {
            statusBar()->showMessage(
                QCoreApplication::translate("OpenKeys", "Keymap settings were not saved: %1").arg(e.what()),
                10000);
        }
}
void MainWindow::showError(const QString &title, const QString &detail) {
    releaseHeld();
    QMessageBox::warning(this, title, detail);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (recordingActive_)
        finishRecord();
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    releaseHeld();
    audio_.shutdown();
    event->accept();
}
} // namespace piano
