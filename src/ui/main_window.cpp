#include "ui/main_window.h"
#include "platform/input.h"
#include "ui/keyboard_view.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
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
    void keyPressEvent(QKeyEvent *event) override { event->ignore(); }
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
    void paintEvent(QPaintEvent *event) override {
        Base::paintEvent(event);
        QStyleOptionSpinBox option;
        this->initStyleOption(&option);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        for (bool up : {true, false}) {
            const auto part = up ? QStyle::SC_SpinBoxUp : QStyle::SC_SpinBoxDown;
            const auto center = this->style()->subControlRect(QStyle::CC_SpinBox, &option, part, this).center();
            const bool enabled = option.stepEnabled & (up ? QAbstractSpinBox::StepUpEnabled
                                                         : QAbstractSpinBox::StepDownEnabled);
            painter.setBrush(QColor(enabled ? "#e6edf7" : "#758295"));
            const double x = center.x(), y = center.y(), direction = up ? -1. : 1.;
            painter.drawPolygon(QPolygonF{QPointF(x, y + direction * 3),
                                          QPointF(x - 4, y - direction * 2),
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

  protected:
    QString textFromValue(int value) const override {
        if (display_ == Key) {
            static const char *names[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
            const int index = (value % 12 + 12) % 12;
            const int octave = (value - index) / 12;
            const auto name = QString::fromLatin1(names[index]);
            return octave == 0 ? name : name + (octave > 0 ? QStringLiteral("（高 1 个八度）")
                                                          : QStringLiteral("（低 1 个八度）"));
        }
        if (display_ == Octave)
            return value == 0 ? QStringLiteral("原音区")
                              : QStringLiteral("%1 %2 个八度")
                                    .arg(value > 0 ? QStringLiteral("升高") : QStringLiteral("降低"))
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
        auto &k = keys[e.key];
        k = std::max(0, k + delta);
        if (!k)
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
    setWindowTitle(QStringLiteral("OpenKeys Piano · 键盘钢琴"));
    resize(1200, 860);
    setMinimumSize(980, 760);
    const auto configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!diagnostic_)
        QDir().mkpath(configDir);
    profilePath_ = configDir + "/keyboard.json";
    if (QFileInfo::exists(profilePath_))
        try {
            keyboard_.keymap = Keymap::read(profilePath_);
        } catch (const FileError &e) {
            statusBar()->showMessage(QStringLiteral("键位设置读取失败，已使用默认布局：%1").arg(e.what()),
                                     20000);
        }
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
            fontLabel_->setText(audio_.status().ready ? QStringLiteral("● ") + fontName_
                                                      : QStringLiteral("○ 导入音源后开始演奏"));
            if (audio_.status().ready)
                startAudio(false);
            showError(importing_ ? "音源导入失败" : "音源加载失败", result.error);
            return;
        }
        releaseHeld();
        audio_.setSoundfont(std::move(result.font));
        importedInfo_ = result.info;
        fontPath_ = result.info.path;
        fontName_ = result.info.name;
        fontLabel_->setText(QStringLiteral("● %1").arg(fontName_));
        fontLabel_->setToolTip("已保存应用副本，下次启动自动加载");
        if (importing_) {
            settings_.remove("soundfont");
            legacyFontPath_.clear();
            if (!removeOldSoundfont(soundfontDirectory_, result.previousId, result.info.id))
                statusBar()->showMessage("新音源已导入，旧副本暂未清理。", 10000);
            else
                statusBar()->showMessage("音源已导入并保存副本，可以清理原下载文件。", 8000);
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
                statusBar()->showMessage(importedInfo_.id.isEmpty() && legacyFontPath_.isEmpty()
                                             ? "请点击「导入音源」开始；「导入教程」可离线查看。"
                                             : "请重新导入音源，以便保存可用的应用副本。");
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
        QMainWindow,QDialog {background:#0d141d;color:#e6edf5;}
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
        QCheckBox {spacing:8px;}
        QProgressBar {border:0;background:#263449;border-radius:3px;max-height:6px;}
        QProgressBar::chunk {background:#83dfc1;border-radius:3px;}
        QSlider::groove:horizontal {height:4px;background:#354761;}
        QSlider::handle:horizontal {background:#83dfc1;width:12px;margin:-4px 0;border-radius:6px;}
        QStatusBar {background:#101824;color:#93a4bc;}
        QToolTip {background:#263449;color:white;border:1px solid #354761;}
    )");
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    setCentralWidget(central);
    auto *header = new QHBoxLayout;
    auto *brand = new QLabel("OpenKeys Piano");
    brand->setObjectName("brand");
    header->addWidget(brand);
    auto *subtitle = new QLabel("键盘上的钢琴  /  自由演奏");
    subtitle->setObjectName("muted");
    header->addWidget(subtitle);
    header->addStretch();
    auto *about = button("关于");
    header->addWidget(about);
    auto *demo = button("演奏示例");
    demo->setObjectName("demoSong");
    header->addWidget(demo);
    connect(demo, &QPushButton::clicked, this, [this] { openPath(":/assets/demo.eopiano.json"); });
    auto *settings = button("音频设置");
    header->addWidget(settings);
    root->addLayout(header);
    connect(about, &QPushButton::clicked, this, [this] {
        releaseHeld();
        QMessageBox::about(
            this, "关于 OpenKeys Piano",
            QStringLiteral("OpenKeys Piano 0.1.0\n免费开源的电脑键盘钢琴 · GPL-3.0-or-later\n\nQt 6 · "
                           "FluidSynth · PortAudio\n音源由用户自行导入，软件不附带或下载音源。\n\nEOP "
                           "支持已知 v200/v201/v301 "
                           "容器；未知控制效果会提示。\nCtrl+Alt+F12：关闭后台演奏并立即止音。"));
    });
    connect(settings, &QPushButton::clicked, this, [this] { audioSettings(); });
    auto *status = new QHBoxLayout;
    fontLabel_ = new QLabel("○ 导入音源后开始演奏");
    fontLabel_->setObjectName("soundfontStatus");
    status->addWidget(fontLabel_, 1);
    fontButton_ = button("导入音源");
    fontButton_->setObjectName("importSoundfont");
    status->addWidget(fontButton_);
    auto *guide = button("导入教程");
    guide->setObjectName("soundfontGuide");
    status->addWidget(guide);
    connect(guide, &QPushButton::clicked, this, [this] { showSoundfontGuide(); });
    background_ = new QCheckBox("后台演奏");
    status->addWidget(background_);
    auto *panic = button("立即止音");
    panic->setToolTip("Ctrl+Alt+F12 · 同时关闭后台演奏");
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
    titleLabel_ = new QLabel("自由演奏");
    titleLabel_->setObjectName("trackTitle");
    titleLabel_->setStyleSheet("font-size:16px;font-weight:600");
    track->addWidget(titleLabel_, 1);
    recordLabel_ = new QLabel("本地演奏 · 无需联网");
    recordLabel_->setObjectName("muted");
    track->addWidget(recordLabel_);
    transportLayout->addLayout(track);
    auto *row = new QHBoxLayout;
    playButton_ = button("▶ 播放");
    playButton_->setObjectName("primary");
    playButton_->setEnabled(false);
    row->addWidget(playButton_);
    auto *stop = button("■ 停止");
    row->addWidget(stop);
    recordButton_ = button("● 录制");
    recordButton_->setEnabled(false);
    recordButton_->setObjectName("record");
    row->addWidget(recordButton_);
    auto *open = button("打开 EOP / 演奏");
    row->addWidget(open);
    auto *save = button("保存演奏");
    row->addWidget(save);
    row->addStretch();
    row->addWidget(new QLabel("速度"));
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
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    transportLayout->addWidget(progress_);
    root->addWidget(transport);
    connect(playButton_, &QPushButton::clicked, this, [this] {
        if (recordingActive_ || !audio_.status().ready || !audio_.status().active)
            return;
        if (audio_.status().playing)
            audio_.pause();
        else
            audio_.play();
        view_->setFocus();
    });
    connect(stop, &QPushButton::clicked, this, [this] {
        if (recordingActive_)
            finishRecord();
        else
            audio_.stop();
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
        auto path = QFileDialog::getOpenFileName(this, "打开演奏文件", {},
                                                 "演奏文件 (*.eop *.eopiano.json);;所有文件 (*)");
        if (!path.isEmpty())
            openPath(path);
    });
    connect(save, &QPushButton::clicked, this, [this] { saveSong(); });
    connect(speed_, &QDoubleSpinBox::valueChanged, this, [this](double value) { audio_.setSpeed(value); });
    auto *performance = new QHBoxLayout;
    auto *keyGroup = new QGroupBox("演奏设置");
    auto *keyRow = new QGridLayout(keyGroup);
    keyGroup->setToolTip("演奏调名和音区只影响之后按下的现场琴键；文件回放使用文件音高。");
    keyRow->addWidget(new QLabel("演奏调名"), 0, 0);
    auto *transpose = performanceSpin(-12, 12, keyboard_.keymap.transpose, PerformanceSpin::Key);
    transpose->setObjectName("transpose");
    keyRow->addWidget(transpose, 0, 1);
    for (int hand = 0; hand < 2; ++hand) {
        const int column = hand * 4;
        keyRow->addWidget(new QLabel(hand == 0 ? "左手音区" : "右手音区"), 1, column);
        auto *octave = performanceSpin(-3, 3, keyboard_.keymap.octave[size_t(hand)], PerformanceSpin::Octave);
        octave->setObjectName(hand == 0 ? "leftOctave" : "rightOctave");
        keyRow->addWidget(octave, 1, column + 1);
        keyRow->addWidget(new QLabel("力度"), 1, column + 2);
        auto *velocity = performanceSpin(1, 127, keyboard_.keymap.velocity[size_t(hand)], PerformanceSpin::Number);
        velocity->setObjectName(hand == 0 ? "leftVelocity" : "rightVelocity");
        keyRow->addWidget(velocity, 1, column + 3);
        connect(octave, &QSpinBox::valueChanged, this, [this, hand](int value) {
            keyboard_.keymap.octave[size_t(hand)] = value;
            updateProfile();
        });
        connect(velocity, &QSpinBox::valueChanged, this, [this, hand](int value) {
            keyboard_.keymap.velocity[size_t(hand)] = value;
            updateProfile();
        });
    }
    connect(transpose, &QSpinBox::valueChanged, this, [this](int value) {
        keyboard_.keymap.transpose = value;
        updateProfile();
    });
    auto *profile = button("键位方案");
    keyRow->addWidget(profile, 0, 7);
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
    auto *hint = new QLabel("左键点击或拖动演奏 · 右键编辑键位  ·  左右手独立力度  ·  EOP 按原始键位回放");
    hint->setObjectName("muted");
    foot->addWidget(hint, 1);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(settings_.value("volume", 80).toInt());
    volume->setFixedWidth(120);
    volume->setToolTip("主音量");
    foot->addWidget(new QLabel("音量"));
    foot->addWidget(volume);
    root->addLayout(foot);
    connect(volume, &QSlider::valueChanged, this, [this](int value) {
        audio_.setVolume(value / 100.);
        settings_.setValue("volume", value);
    });
    deviceLabel_ = new QLabel("音频尚未启动");
    statusBar()->addPermanentWidget(deviceLabel_);
}
bool MainWindow::canPlayLive(bool notify) {
    const auto status = audio_.status();
    if (status.ready && status.active && !loading_)
        return true;
    if (notify)
        statusBar()->showMessage(loading_        ? "音源正在加载，请稍候再演奏。"
                                 : !status.ready ? "请先点击「导入音源」，导入 SF2 后即可演奏。"
                                                 : "音频未启动，请在音频设置中连接设备。",
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
            statusBar()->showMessage("录制达到时长或事件上限，已结束录制。", 10000);
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
    audio_.panic();
    statusBar()->showMessage("已止音，后台演奏已关闭。", 5000);
    view_->setFocus();
}
void MainWindow::setSong(Song song) {
    releaseHeld();
    audio_.setSong(song);
    song_ = std::move(song);
    dirty_ = false;
    visualCursor_ = 0;
    visualTime_ = -1;
    view_->playbackKeys.clear();
    for (auto &h : view_->playbackNotes)
        h.fill(0);
    titleLabel_->setText(song_.title.isEmpty() ? QStringLiteral("未命名演奏") : song_.title);
    playButton_->setEnabled(!song_.events.empty() && audio_.status().ready && audio_.status().active &&
                            !loading_);
    recordLabel_->setText("已载入 · " + QString::number(song_.events.size()) + " 个演奏事件");
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
        if (song.title.isEmpty())
            song.title = QFileInfo(path).completeBaseName();
        const auto warnings = song.warnings;
        setSong(std::move(song));
        if (!warnings.empty())
            QMessageBox::information(this, "EOP 兼容提示", warnings.join('\n'));
        statusBar()->showMessage(path.startsWith(":/") ? QStringLiteral("已打开演奏示例") : "已打开：" + path,
                                 10000);
    } catch (const FileError &e) {
        showError("无法打开演奏", QString::fromUtf8(e.what()));
    }
}
void MainWindow::toggleRecord() {
    if (recordingActive_) {
        finishRecord();
        return;
    }
    if (!audio_.status().ready || !audio_.status().active) {
        showError("暂时无法录制", "请先导入音源并启用音频设备。");
        return;
    }
    if (!maybeSave())
        return;
    releaseHeld();
    audio_.setSong({});
    song_ = {};
    recording_ = {};
    recording_.title = "未命名演奏";
    recordingActive_ = true;
    recordingClock_.start();
    titleLabel_->setText("正在录制");
    recordButton_->setText("■ 结束录制");
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
    recordButton_->setText("● 录制");
    recordButton_->setProperty("active", false);
    recordButton_->setStyleSheet({});
    try {
        setSong(recording_);
        dirty_ = !song_.events.empty();
        recordLabel_->setText(dirty_ ? "录制完成 · 尚未保存" : "录制为空");
    } catch (const FileError &e) {
        showError("录制数据错误", e.what());
    }
}
bool MainWindow::saveSong() {
    if (recordingActive_)
        finishRecord();
    releaseHeld();
    if (song_.events.empty()) {
        statusBar()->showMessage("尚无可保存的演奏。", 5000);
        return false;
    }
    QFileDialog dialog(this, "保存演奏");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter("OpenKeys 演奏 (*.eopiano.json)");
    dialog.setDefaultSuffix("eopiano.json");
    dialog.selectFile(song_.title + ".eopiano.json");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    const auto path = dialog.selectedFiles().first();
    try {
        saveRecording(path, song_);
        dirty_ = false;
        recordLabel_->setText("已保存");
        statusBar()->showMessage("已保存：" + path, 8000);
        return true;
    } catch (const FileError &e) {
        showError("保存失败", e.what());
        return false;
    }
}
bool MainWindow::maybeSave() {
    if (!dirty_)
        return true;
    releaseHeld();
    const auto answer = QMessageBox::question(this, "保存演奏", "当前录制尚未保存。是否保存？",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Save);
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
        QFileDialog::getOpenFileName(this, "导入 SF2 音源", legacyFontPath_, "SoundFont (*.sf2 *.SF2)");
    if (!path.isEmpty())
        loadFont(path);
}
void MainWindow::showSoundfontGuide() {
    releaseHeld();
    QDialog dialog(this);
    dialog.setObjectName("soundfontGuideDialog");
    dialog.setWindowTitle("音源导入教程");
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
    connect(browser, &QTextBrowser::anchorClicked, &dialog, [](const QUrl &link) {
        if (link.scheme() == "https")
            QDesktopServices::openUrl(link);
    });
    QFile file(":/docs/soundfont.md");
    if (file.open(QIODevice::ReadOnly))
        browser->setMarkdown(QString::fromUtf8(file.readAll()));
    else
        browser->setPlainText("教程读取失败，请查看安装目录 docs 中的 soundfont.md。");
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
    importProgress_->setFormat(importing ? "正在复制音源 %p%" : "正在加载音源…");
    if (!importing)
        importProgress_->setRange(0, 0);
    fontLabel_->setText(importing ? "◌ 正在导入音源…" : "◌ 正在加载音源…");
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
                            window->importProgress_->setFormat("正在复制音源 %p%");
                        } else {
                            window->importProgress_->setRange(0, 0);
                            window->importProgress_->setFormat("正在加载音源…");
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
void MainWindow::tick() {
    const auto status = audio_.status();
    if (status.overflows != lastOverflow_) {
        lastOverflow_ = status.overflows;
        emergencyStop();
        statusBar()->showMessage("演奏事件过载，已自动止音并关闭后台演奏。", 12000);
    }
    if (status.underruns != lastUnderruns_) {
        lastUnderruns_ = status.underruns;
        statusBar()->showMessage("检测到音频欠载，可在音频设置中增大缓冲。", 8000);
    }
    if (wasAudioActive_ && !status.active && !loading_ && !diagnostic_) {
        if (recordingActive_)
            finishRecord();
        releaseHeld();
        audio_.shutdown();
        statusBar()->showMessage("音频设备已停止，已清除演奏状态。请在音频设置中重连设备。", 15000);
    }
    wasAudioActive_ = status.active;
    recordButton_->setEnabled(recordingActive_ || (status.ready && status.active && !loading_));
    playButton_->setText(status.playing ? "Ⅱ 暂停" : "▶ 播放");
    playButton_->setEnabled(!recordingActive_ && !song_.events.empty() && status.active);
    const auto pos = recordingActive_ ? recordingClock_.nsecsElapsed() / 1000 : status.positionUs;
    timeLabel_->setText(timeText(pos) + " / " + timeText(song_.durationUs()));
    progress_->setValue(song_.durationUs() > 0
                            ? int(std::min<int64_t>(1000, status.positionUs * 1000 / song_.durationUs()))
                            : 0);
    if (recordingActive_)
        recordLabel_->setText("● 录制中 · " + timeText(pos));
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
    view_->showPlayback = status.playing;
    view_->update();
    deviceLabel_->setText(status.active ? QString("%1 Hz · 报告缓冲延迟 %2 ms · 欠载 %3")
                                              .arg(int(status.sampleRate))
                                              .arg(status.reportedLatencyMs, 0, 'f', 1)
                                              .arg(status.underruns)
                                        : QStringLiteral("音频未启动 · 可在设置中重连"));
}
void MainWindow::audioSettings() {
    if (recordingActive_)
        finishRecord();
    releaseHeld();
    audio_.refreshDevices();
    QDialog dialog(this);
    dialog.setWindowTitle("音频设置");
    auto *form = new QFormLayout(&dialog);
    auto *devices = new QComboBox;
    devices->addItem("系统默认设备", -1);
    for (const auto &device : audio_.devices()) {
        devices->addItem(device.name, device.id);
        if (device.name == settings_.value("audio/deviceName").toString())
            devices->setCurrentIndex(devices->count() - 1);
    }
    form->addRow("输出设备", devices);
    auto *frames = new QComboBox;
    for (int n : {64, 128, 256, 512, 1024, 2048}) {
        frames->addItem(QString::number(n) + " 帧", n);
        if (n == config_.frames)
            frames->setCurrentIndex(frames->count() - 1);
    }
    form->addRow("请求缓冲", frames);
    auto *exclusive = new QCheckBox("WASAPI 独占模式（设备不支持时退回共享）");
    exclusive->setChecked(config_.exclusive);
#ifndef Q_OS_WIN
    exclusive->setEnabled(false);
#endif
    form->addRow(exclusive);
    auto *hint = new QLabel(
        "较小缓冲可降低延迟；出现爆音或欠载时增大缓冲。\n界面报告的是驱动缓冲延迟，不是按键到出声的实测值。");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
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
    dialog.setWindowTitle("编辑键位 · " + keyName(code));
    auto *form = new QFormLayout(&dialog);
    Binding binding{code, 60, Hand::Left, false};
    auto it = keyboard_.keymap.bindings.find(code);
    if (it != keyboard_.keymap.bindings.end())
        binding = it->second;
    auto *type = new QComboBox;
    type->addItems({"音符", "延音踏板（双手）", "未分配"});
    type->setCurrentIndex(it == keyboard_.keymap.bindings.end() ? 2 : binding.sustain ? 1 : 0);
    form->addRow("功能", type);
    auto *note = spin(0, 127, binding.note);
    auto *name = new QLabel(noteName(binding.note));
    auto *noteRow = new QHBoxLayout;
    noteRow->addWidget(note);
    noteRow->addWidget(name);
    form->addRow("MIDI 音高", noteRow);
    connect(note, &QSpinBox::valueChanged, &dialog, [name](int n) { name->setText(noteName(n)); });
    auto *hand = new QComboBox;
    hand->addItems({"左手", "右手", "不区分"});
    hand->setCurrentIndex(int(binding.hand));
    form->addRow("手部", hand);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (code == 42 || code == 54) {
        auto *hint = new QLabel("Shift 是固定的力度修饰键，不支持重新分配。");
        form->addRow(hint);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    if (dialog.exec() == QDialog::Accepted) {
        if (type->currentIndex() == 2)
            keyboard_.keymap.bindings.erase(code);
        else
            keyboard_.keymap.bindings[code] = {code, note->value(), Hand(hand->currentIndex()),
                                               type->currentIndex() == 1};
        updateProfile();
    }
    view_->setFocus();
}
void MainWindow::profileSettings() {
    releaseHeld();
    QDialog dialog(this);
    dialog.setWindowTitle("键位方案");
    auto *layout = new QVBoxLayout(&dialog);
    auto *load = button("导入键位 JSON");
    auto *save = button("导出当前键位");
    auto *reset = button("恢复默认双手布局");
    layout->addWidget(load);
    layout->addWidget(save);
    layout->addWidget(reset);
    connect(load, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getOpenFileName(&dialog, "导入键位", {}, "键位 (*.json)");
        if (path.isEmpty())
            return;
        try {
            keyboard_.keymap = Keymap::read(path);
            updateProfile();
            dialog.accept();
        } catch (const FileError &e) {
            showError("导入失败", e.what());
        }
    });
    connect(save, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getSaveFileName(&dialog, "导出键位", "keyboard.json", "键位 (*.json)");
        if (path.isEmpty())
            return;
        try {
            keyboard_.keymap.save(path);
            dialog.accept();
        } catch (const FileError &e) {
            showError("导出失败", e.what());
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
    sync("leftVelocity", keyboard_.keymap.velocity[0]);
    sync("rightVelocity", keyboard_.keymap.velocity[1]);
    view_->setFocus();
}
void MainWindow::updateProfile() {
    view_->map = keyboard_.keymap;
    view_->update();
    if (!diagnostic_)
        try {
            keyboard_.keymap.save(profilePath_);
        } catch (const FileError &e) {
            statusBar()->showMessage(QStringLiteral("键位设置未保存：%1").arg(e.what()), 10000);
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
