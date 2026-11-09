#include "platform/input.h"
#include "core/keyboard.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace piano {
InputRouter::InputRouter(QWidget *window) : QObject(window), window_(window) {
    qApp->installEventFilter(this);
    qApp->installNativeEventFilter(this);
#ifdef Q_OS_WIN
    const auto hwnd = reinterpret_cast<HWND>(window_->winId());
    hotkey_ = RegisterHotKey(hwnd, 0x4f50, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12);
    RAWINPUTDEVICE device{0x01, 0x06, RIDEV_DEVNOTIFY, hwnd};
    raw_ = RegisterRawInputDevices(&device, 1, sizeof(device));
    if (!raw_)
        error_ = QStringLiteral("原始键盘输入注册失败，使用窗口内兼容输入");
    else if (!hotkey_)
        error_ = QStringLiteral("Ctrl+Alt+F12 已被占用，后台演奏不可用");
#else
    error_ = QStringLiteral("后台演奏仅在 Windows 版本提供");
#endif
}
InputRouter::~InputRouter() {
    qApp->removeNativeEventFilter(this);
    qApp->removeEventFilter(this);
#ifdef Q_OS_WIN
    if (hotkey_)
        UnregisterHotKey(reinterpret_cast<HWND>(window_->winId()), 0x4f50);
    if (raw_) {
        RAWINPUTDEVICE device{0x01, 0x06, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
    }
#endif
}
bool InputRouter::setBackground(bool enabled) {
    if (suspended)
        suspended();
    if (!enabled)
        background_ = false; // stop accepting background events even if re-registration fails
#ifdef Q_OS_WIN
    if (enabled && !globalAvailable())
        return false;
    RAWINPUTDEVICE device{0x01, 0x06, DWORD(RIDEV_DEVNOTIFY | (enabled ? RIDEV_INPUTSINK : 0)),
                          reinterpret_cast<HWND>(window_->winId())};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        error_ = QStringLiteral("后台键盘注册失败");
        return false;
    }
    raw_ = true;
    background_ = enabled;
    return true;
#else
    Q_UNUSED(enabled);
    return false;
#endif
}
bool InputRouter::blocked() const {
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return true;
    auto *focus = QApplication::focusWidget();
    if (!focus || !window_->isAncestorOf(focus))
        return false;
    for (auto *w = focus; w; w = w->parentWidget())
        if (qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
            qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w))
            return true;
    return false;
}
bool InputRouter::acceptsInput() const {
    return !blocked() && (background_ || window_->isActiveWindow());
}
bool InputRouter::eventFilter(QObject *, QEvent *event) {
    if ((event->type() == QEvent::FocusIn && blocked()) ||
        (event->type() == QEvent::ApplicationDeactivate && !background_))
        if (suspended)
            suspended();
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
        event->type() == QEvent::KeyRelease) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_F12 && key->modifiers().testFlag(Qt::ControlModifier) &&
            key->modifiers().testFlag(Qt::AltModifier)) {
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat() && emergency)
                emergency();
            event->accept();
            return true;
        }
        const auto scan = scanFromQt(key->key(), key->nativeScanCode());
        const bool mapped = scan && (scan == 42 || scan == 54 || (isBound && isBound(scan)));
        if (acceptsInput() && mapped) {
            if (!raw_ && event->type() != QEvent::ShortcutOverride && !key->isAutoRepeat() && keyChanged)
                keyChanged(scan, event->type() == QEvent::KeyPress);
            event->accept();
            return true;
        }
        if (!raw_ && event->type() == QEvent::KeyRelease && !key->isAutoRepeat() && keyChanged)
            keyChanged(scan, false);
    }
    return false;
}
bool InputRouter::nativeEventFilter(const QByteArray &, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 0x4f50) {
        if (result)
            *result = 0;
        if (emergency)
            emergency();
        return true;
    }
    if (msg->message == WM_INPUT_DEVICE_CHANGE) {
        if (suspended)
            suspended();
    }
    if (msg->message == WM_POWERBROADCAST &&
        (msg->wParam == PBT_APMSUSPEND || msg->wParam == PBT_APMRESUMEAUTOMATIC)) {
        if (suspended)
            suspended();
    }
    if (msg->message == WM_INPUT) {
        RAWINPUT input{};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER)) == UINT(-1))
            return false;
        if (input.header.dwType != RIM_TYPEKEYBOARD)
            return false;
        const auto &key = input.data.keyboard;
        if (key.MakeCode == 0 || key.MakeCode == 0xff || (key.Flags & RI_KEY_E1))
            return false;
        const auto code = uint16_t(key.MakeCode | ((key.Flags & RI_KEY_E0) ? 0x100 : 0));
        const bool down = !(key.Flags & RI_KEY_BREAK);
        if ((!down || acceptsInput()) && keyChanged)
            keyChanged(code, down);
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false; // foreground WM_INPUT still reaches DefWindowProc for cleanup
}
} // namespace piano
