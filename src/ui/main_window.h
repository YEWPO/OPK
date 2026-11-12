#pragma once
#include "audio/engine.h"
#include "audio/soundfont_import.h"
#include "core/keyboard.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>
#include <atomic>
#include <memory>
class QLabel;
class QPushButton;
class QCheckBox;
class QProgressBar;
class QSlider;
class QDoubleSpinBox;
class QCloseEvent;
class UiTests;
namespace piano {
class InputRouter;
class KeyboardView;
class MainWindow : public QMainWindow {
  public:
    explicit MainWindow(bool diagnostic = false, const QString &soundfontDirectory = {});
    ~MainWindow() override;
    void openPath(const QString &path);
    void loadFont(const QString &path);

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    friend class ::UiTests;
    AudioEngine audio_;
    KeyboardState keyboard_;
    KeyHolds keyHolds_;
    Song song_, recording_;
    QElapsedTimer recordingClock_;
    QFutureWatcher<ImportedSoundfont> loader_;
    QTimer timer_;
    QSettings settings_;
    std::unique_ptr<InputRouter> input_;
    KeyboardView *view_ = nullptr;
    QLabel *fontLabel_ = nullptr, *deviceLabel_ = nullptr, *titleLabel_ = nullptr, *timeLabel_ = nullptr,
           *recordLabel_ = nullptr;
    QPushButton *playButton_ = nullptr, *recordButton_ = nullptr, *fontButton_ = nullptr;
    QCheckBox *background_ = nullptr;
    QProgressBar *importProgress_ = nullptr;
    QSlider *progress_ = nullptr;
    QDoubleSpinBox *speed_ = nullptr;
    bool recordingActive_ = false, dirty_ = false, diagnostic_ = false, loading_ = false;
    int mouseHeld_ = -1;
    size_t visualCursor_ = 0;
    int64_t visualTime_ = -1;
    uint64_t lastOverflow_ = 0, lastUnderruns_ = 0;
    bool wasAudioActive_ = false;
    AudioConfig config_;
    QString fontPath_, profilePath_;
    QString soundfontDirectory_, fontName_, legacyFontPath_;
    ImportedFontInfo importedInfo_;
    bool importing_ = false;
    std::shared_ptr<std::atomic_bool> cancelImport_;
    void buildUi();
    void key(uint16_t, bool, KeySource source = KeySource::Physical);
    bool canPlayLive(bool notify);
    std::vector<Event> takeLiveReleases();
    void dispatchLive(const Event &);
    void releaseHeld();
    void emergencyStop();
    void tick();
    void syncPerformance(int64_t positionUs);
    bool playbackConfiguration_ = false;
    uint64_t pendingPlayRequest_ = 0;
    int64_t pendingPlayPosition_ = 0;
    void startPlayback();
    void seekPlayback(int value);
    void cancelSeekDrag();
    void toggleRecord();
    void finishRecord();
    bool maybeSave();
    bool saveSong();
    void audioSettings();
    void editKey(uint16_t);
    void profileSettings();
    void updateProfile();
    void setSong(Song song);
    void startAudio(bool allowReload = true);
    void beginFontTask(const QString &path, bool importing);
    void chooseSoundfont();
    void showSoundfontGuide();
    void showError(const QString &title, const QString &detail);
};
} // namespace piano
