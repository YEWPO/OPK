#pragma once
#include "core/keyboard.h"
#include <QWidget>
#include <functional>
#include <set>
namespace piano {
class KeyboardView : public QWidget {
  public:
    explicit KeyboardView(QWidget *parent = nullptr);
    Keymap map = Keymap::defaults();
    std::map<uint16_t, std::array<int, 3>> liveKeys, playbackKeys;
    std::set<uint16_t> pressedKeys;
    std::array<std::array<int, 128>, 3> liveNotes{}, playbackNotes{};
    bool showPlayback = false;
    std::function<void(uint16_t)> editKey;
    std::function<void(uint16_t, bool)> mouseKey;
    std::function<void(int, bool)> mouseNote;
    // Clears local gesture state; the owner releases any accepted musical events.
    void resetMouseInteraction();
    QSize minimumSizeHint() const override {
        return {880, 440};
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    bool event(QEvent *) override;

  private:
    QRectF keyRect(const PhysicalKey &) const;
    QRectF pianoRect() const;
    int hitNote(QPointF point) const;
    uint16_t hitKey(QPointF point, bool playableOnly = true) const;
    void updateHover(QPointF point);
    void moveMouseTo(QPointF point);
    void releaseMouseTarget();
    void cancelMouseInteraction();
    uint16_t mouseKey_ = 0, hoverKey_ = 0;
    int hoverNote_ = -1;
    int mouseNote_ = -1;
    bool dragging_ = false;
};
} // namespace piano
