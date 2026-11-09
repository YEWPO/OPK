#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <cstdint>
#include <functional>
class QWidget;
namespace piano {
class InputRouter : public QObject, public QAbstractNativeEventFilter {
  public:
    explicit InputRouter(QWidget *window);
    ~InputRouter() override;
    bool setBackground(bool enabled);
    bool background() const {
        return background_;
    }
    bool globalAvailable() const {
        return hotkey_ && raw_;
    }
    QString error() const {
        return error_;
    }
    std::function<void(uint16_t, bool)> keyChanged;
    std::function<void()> suspended, emergency;
    std::function<bool(uint16_t)> isBound;
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override;

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    bool blocked() const;
    bool acceptsInput() const;
    QWidget *window_;
    bool background_ = false, raw_ = false, hotkey_ = false;
    QString error_;
};
} // namespace piano
