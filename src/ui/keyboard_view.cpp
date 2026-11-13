#include "ui/keyboard_view.h"
#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <utility>
namespace piano {
namespace {
const QColor ink("#e6edf5"), muted("#8493a8"), left("#67d9ba"), right("#b6a0ff"), play("#ffc875");
bool black(int note) {
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}
QColor handColor(size_t hand) {
    return hand == 0 ? left : hand == 1 ? right : play;
}
QColor keyColor(const std::map<uint16_t, std::array<int, 3>> &keys, uint16_t code) {
    const auto it = keys.find(code);
    if (it != keys.end())
        for (size_t hand = 0; hand < it->second.size(); ++hand)
            if (it->second[hand] > 0)
                return handColor(hand);
    return {};
}
QColor noteColor(const std::array<std::array<int, 128>, 3> &notes, int n) {
    for (int h = 0; h < 3; ++h)
        if (notes[size_t(h)][size_t(n)] > 0)
            return handColor(size_t(h));
    return {};
}
} // namespace
KeyboardView::KeyboardView(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(minimumSizeHint());
}
QRectF KeyboardView::keyRect(const PhysicalKey &key) const {
    const double unit = (width() - 24.) / 24.;
    const double h = std::clamp((height() - 220.) / 6., 34., 52.);
    return {12 + key.column * unit, 34 + key.row * h, unit * key.width - 5, h * key.height - 5};
}
QRectF KeyboardView::pianoRect() const {
    return {12., height() - 156., width() - 24., 138.};
}
void KeyboardView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#111923"));
    p.setPen(muted);
    p.setFont(QFont("sans-serif", 9));
    p.drawText(QRectF(16, 7, width() - 32, 20),
               QCoreApplication::translate("OpenKeys",
                                           "Computer keyboard · Click or drag to play · Right-click to "
                                           "edit   |   Left: green · Right: purple · Unknown: gold"));
    for (const auto &key : physicalKeys()) {
        const auto r = keyRect(key);
        const auto it = map.bindings.find(key.code);
        const bool live = key.code && (liveKeys.contains(key.code) || pressedKeys.contains(key.code) ||
                                       mouseKey_ == key.code),
                   hover = key.code && hoverKey_ == key.code,
                   playing = key.code && showPlayback && playbackKeys.contains(key.code);
        QColor accent = muted;
        if (it != map.bindings.end())
            accent = it->second.hand == Hand::Left ? left : it->second.hand == Hand::Right ? right : muted;
        const auto activeColor = live      ? keyColor(liveKeys, key.code)
                                 : playing ? keyColor(playbackKeys, key.code)
                                           : QColor();
        if (activeColor.isValid())
            accent = activeColor;
        p.setPen(
            QPen(live || playing || hover ? accent : QColor("#32404e"), live || playing || hover ? 2 : 1));
        p.setBrush(live      ? accent.darker(220)
                   : playing ? accent.darker(300)
                   : hover   ? QColor("#283b50")
                             : QColor("#1a2532"));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(ink);
        p.setFont(QFont("sans-serif", 9));
        p.drawText(r.adjusted(7, 3, -4, -4), Qt::AlignTop | Qt::AlignLeft,
                   QCoreApplication::translate("OpenKeys", key.label));
        QString label;
        if (it != map.bindings.end()) {
            const auto &binding = it->second;
            const int hand = binding.hand == Hand::Left ? 0 : 1;
            label = binding.sustain ? QCoreApplication::translate("OpenKeys", "Sustain")
                                    : noteName(binding.note + map.transpose + 12 * map.octave[size_t(hand)]);
        } else if (key.code == 42)
            label = QCoreApplication::translate("OpenKeys", "Soft");
        else if (key.code == 54)
            label = QCoreApplication::translate("OpenKeys", "Loud");
        p.setPen(accent);
        p.setFont(QFont("sans-serif", 9));
        p.drawText(r.adjusted(3, 12, -5, -3), Qt::AlignBottom | Qt::AlignRight, label);
    }
    const auto area = pianoRect();
    p.setPen(muted);
    p.drawText(QRectF(16, area.top() - 27, width() - 32, 22),
               QCoreApplication::translate("OpenKeys", "Piano · A0–C8 · Click or drag to play   |   Space: "
                                                       "sustain · Left Shift: soft · Right Shift: loud"));
    const double w = area.width() / 52.;
    int index = 0;
    for (int n = 21; n <= 108; ++n) {
        if (black(n))
            continue;
        auto color = noteColor(liveNotes, n);
        if (!color.isValid() && showPlayback)
            color = noteColor(playbackNotes, n);
        if (mouseNote_ == n)
            color = right;
        else if (!color.isValid() && hoverNote_ == n)
            color = QColor("#d8d0f5");
        const QRectF r(area.left() + index * w, area.top(), w - 1, area.height());
        ++index;
        p.setPen(QColor("#cad1d8"));
        p.setBrush(color.isValid() ? color : QColor("#eef0eb"));
        p.drawRoundedRect(r, 0, 0);
        if (n % 12 == 0) {
            p.setPen(QColor("#62707c"));
            p.setFont(QFont("sans-serif", 8));
            p.drawText(r.adjusted(1, 0, -1, -7), Qt::AlignBottom | Qt::AlignHCenter, noteName(n));
        }
    }
    index = 0;
    for (int n = 21; n <= 108; ++n) {
        if (!black(n)) {
            ++index;
            continue;
        }
        auto color = noteColor(liveNotes, n);
        if (!color.isValid() && showPlayback)
            color = noteColor(playbackNotes, n);
        if (mouseNote_ == n)
            color = right;
        else if (!color.isValid() && hoverNote_ == n)
            color = QColor("#62577e");
        const QRectF r(area.left() + index * w - w * .31, area.top(), w * .62, area.height() * .62);
        p.setPen(QColor("#0d121a"));
        p.setBrush(color.isValid() ? color : QColor("#18212c"));
        p.drawRoundedRect(r, 2, 2);
    }
}
int KeyboardView::hitNote(QPointF point) const {
    const auto area = pianoRect();
    if (!area.contains(point))
        return -1;
    const double w = area.width() / 52.;
    int index = 0;
    for (int n = 21; n <= 108; ++n) {
        if (!black(n)) {
            ++index;
            continue;
        }
        if (QRectF(area.left() + index * w - w * .31, area.top(), w * .62, area.height() * .62)
                .contains(point))
            return n;
    }
    index = 0;
    for (int n = 21; n <= 108; ++n)
        if (!black(n)) {
            if (QRectF(area.left() + index * w, area.top(), w, area.height()).contains(point))
                return n;
            ++index;
        }
    return -1;
}
uint16_t KeyboardView::hitKey(QPointF point, bool playableOnly) const {
    for (const auto &key : physicalKeys())
        if (key.code && keyRect(key).contains(point) &&
            (!playableOnly || map.bindings.contains(key.code) || key.code == 42 || key.code == 54))
            return key.code;
    return 0;
}
void KeyboardView::updateHover(QPointF point) {
    const bool overFn = std::any_of(physicalKeys().begin(), physicalKeys().end(), [&](const auto &key) {
        return !key.code && keyRect(key).contains(point);
    });
    setToolTip(overFn
                   ? QCoreApplication::translate(
                         "OpenKeys", "Fn is shown for layout reference only; it cannot be played or edited.")
                   : QString());
    hoverKey_ = hitKey(point);
    hoverNote_ = hitNote(point);
    setCursor(hoverKey_ || hoverNote_ >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}
void KeyboardView::releaseMouseTarget() {
    // Clear before callbacks: recording limits or dialogs can reset the gesture reentrantly.
    const auto key = std::exchange(mouseKey_, uint16_t(0));
    const int note = std::exchange(mouseNote_, -1);
    if (key && mouseKey)
        mouseKey(key, false);
    if (note >= 0 && mouseNote)
        mouseNote(note, false);
}
void KeyboardView::resetMouseInteraction() {
    dragging_ = false;
    mouseKey_ = hoverKey_ = 0;
    mouseNote_ = hoverNote_ = -1;
    unsetCursor();
    update();
}
void KeyboardView::cancelMouseInteraction() {
    dragging_ = false;
    releaseMouseTarget();
    resetMouseInteraction();
}
void KeyboardView::moveMouseTo(QPointF point) {
    const auto key = hitKey(point);
    const int note = hitNote(point);
    if (key == mouseKey_ && note == mouseNote_)
        return;
    releaseMouseTarget();
    if (!dragging_)
        return;
    mouseKey_ = key;
    mouseNote_ = note;
    if (key && mouseKey)
        mouseKey(key, true);
    if (note >= 0 && mouseNote)
        mouseNote(note, true);
    update();
}
void KeyboardView::mousePressEvent(QMouseEvent *e) {
    setFocus();
    if (e->button() == Qt::RightButton) {
        cancelMouseInteraction();
        const auto code = hitKey(e->position(), false);
        if (code && editKey)
            editKey(code);
    }
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        updateHover(e->position());
        moveMouseTo(e->position());
    }
}
void KeyboardView::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton)
        return;
    dragging_ = false;
    releaseMouseTarget();
    updateHover(e->position());
}
void KeyboardView::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_ && !e->buttons().testFlag(Qt::LeftButton))
        cancelMouseInteraction();
    updateHover(e->position());
    if (dragging_)
        moveMouseTo(e->position());
}
void KeyboardView::leaveEvent(QEvent *) {
    hoverKey_ = 0;
    hoverNote_ = -1;
    unsetCursor();
    if (dragging_)
        releaseMouseTarget();
    update();
}
bool KeyboardView::event(QEvent *event) {
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse)
        cancelMouseInteraction();
    return QWidget::event(event);
}
} // namespace piano
