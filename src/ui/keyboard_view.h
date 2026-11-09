#pragma once
#include "core/keyboard.h"
#include <QWidget>
#include <functional>
namespace piano {
class KeyboardView : public QWidget {
  public:
    explicit KeyboardView(QWidget *parent = nullptr);
    Keymap map = Keymap::defaults();
    std::map<uint16_t, int> liveKeys, playbackKeys;
    std::array<std::array<int, 128>, 3> liveNotes{}, playbackNotes{};
    bool showPlayback = false;
    std::function<void(uint16_t)> editKey;
    std::function<void(int, bool)> mouseNote;
    QSize minimumSizeHint() const override {
        return {880, 440};
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    QRectF keyRect(const PhysicalKey &) const;
    QRectF pianoRect() const;
    int hitNote(QPointF point) const;
    int mouseNote_ = -1;
};
} // namespace piano
