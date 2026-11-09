#pragma once
#include "core/model.h"
#include <map>
#include <optional>
namespace piano {
struct Binding {
    uint16_t key = 0;
    int note = 60;
    Hand hand = Hand::Left;
    bool sustain = false;
};
struct PhysicalKey {
    uint16_t code;
    const char *label;
    int qtKey;
    int row;
    double column;
    double width;
};
const std::vector<PhysicalKey> &physicalKeys();
QString keyName(uint16_t code);
uint16_t scanFromQt(int qtKey, quint32 nativeScanCode);
struct Keymap {
    std::map<uint16_t, Binding> bindings;
    int transpose = 0;
    std::array<int, 2> octave{0, 0};
    std::array<int, 2> velocity{80, 100};
    static Keymap defaults();
    static Keymap read(const QString &path);
    void save(const QString &path) const;
};
class KeyboardState {
  public:
    Keymap keymap = Keymap::defaults();
    std::vector<Event> change(uint16_t key, bool down, int64_t timeUs = 0);
    std::vector<Event> releaseAll(int64_t timeUs = 0);
    bool held(uint16_t key) const {
        return pressed_.contains(key);
    }
    bool soft = false, hard = false;

  private:
    std::map<uint16_t, std::vector<Event>> pressed_;
    std::array<int, 3> pedals_{};
};
} // namespace piano
