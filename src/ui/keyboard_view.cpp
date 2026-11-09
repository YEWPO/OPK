#include "ui/keyboard_view.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
namespace piano {
namespace {
const QColor ink("#e6edf5"), muted("#8493a8"), left("#67d9ba"), right("#b6a0ff"), play("#ffc875");
bool black(int note) {
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}
QColor noteColor(const std::array<std::array<int, 128>, 3> &notes, int n) {
    for (int h = 0; h < 3; ++h)
        if (notes[size_t(h)][size_t(n)] > 0)
            return h == 0 ? left : h == 1 ? right : play;
    return {};
}
} // namespace
KeyboardView::KeyboardView(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(minimumSizeHint());
}
QRectF KeyboardView::keyRect(const PhysicalKey &key) const {
    const double unit = (width() - 24.) / 24.;
    const double h = std::clamp((height() - 220.) / 6., 34., 52.);
    return {12 + key.column * unit, 34 + key.row * h, unit * key.width - 5, h - 5};
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
    p.drawText(
        QRectF(16, 7, width() - 32, 20),
        "电脑键盘  /  右键编辑键位                          左手 · 绿色     右手 · 紫色     回放 · 金色");
    for (const auto &key : physicalKeys()) {
        const auto r = keyRect(key);
        const auto it = map.bindings.find(key.code);
        const bool live = liveKeys.contains(key.code),
                   playing = showPlayback && playbackKeys.contains(key.code);
        QColor accent = muted;
        if (it != map.bindings.end())
            accent = it->second.hand == Hand::Left ? left : it->second.hand == Hand::Right ? right : muted;
        p.setPen(QPen(live ? accent : playing ? play : QColor("#32404e"), live || playing ? 2 : 1));
        p.setBrush(live ? accent.darker(220) : playing ? play.darker(300) : QColor("#1a2532"));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(ink);
        p.setFont(QFont("sans-serif", 9));
        p.drawText(r.adjusted(7, 3, -4, -4), Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8(key.label));
        QString label;
        if (it != map.bindings.end()) {
            const auto &binding = it->second;
            const int hand = binding.hand == Hand::Left ? 0 : 1;
            label = binding.sustain ? QStringLiteral("延音")
                                    : noteName(binding.note + map.transpose + 12 * map.octave[size_t(hand)]);
        } else if (key.code == 42)
            label = "弱奏";
        else if (key.code == 54)
            label = "强奏";
        p.setPen(accent);
        p.setFont(QFont("sans-serif", 9));
        p.drawText(r.adjusted(3, 12, -5, -3), Qt::AlignBottom | Qt::AlignRight, label);
    }
    const auto area = pianoRect();
    p.setPen(muted);
    p.drawText(
        QRectF(16, area.top() - 27, width() - 32, 22),
        "钢琴  /  A0–C8                                       Space 延音 · 左 Shift 弱奏 · 右 Shift 强奏");
    const double w = area.width() / 52.;
    int index = 0;
    for (int n = 21; n <= 108; ++n) {
        if (black(n))
            continue;
        auto color = noteColor(liveNotes, n);
        if (!color.isValid() && showPlayback)
            color = noteColor(playbackNotes, n);
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
void KeyboardView::mousePressEvent(QMouseEvent *e) {
    setFocus();
    if (e->button() == Qt::RightButton) {
        for (const auto &key : physicalKeys())
            if (keyRect(key).contains(e->position())) {
                if (editKey)
                    editKey(key.code);
                return;
            }
    }
    if (e->button() == Qt::LeftButton) {
        mouseNote_ = hitNote(e->position());
        if (mouseNote_ >= 0 && mouseNote)
            mouseNote(mouseNote_, true);
    }
}
void KeyboardView::mouseReleaseEvent(QMouseEvent *) {
    if (mouseNote_ >= 0 && mouseNote)
        mouseNote(mouseNote_, false);
    mouseNote_ = -1;
}
} // namespace piano
