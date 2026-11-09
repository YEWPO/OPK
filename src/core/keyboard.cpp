#include "core/keyboard.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <Qt>
#include <algorithm>
#include <cmath>
namespace piano {
const std::vector<PhysicalKey> &physicalKeys() {
    static const std::vector<PhysicalKey> keys = [] {
        std::vector<PhysicalKey> keys = {{1, "Esc", Qt::Key_Escape, 0, 0, 1},
                                         {59, "F1", Qt::Key_F1, 0, 2, 1},
                                         {60, "F2", Qt::Key_F2, 0, 3, 1},
                                         {61, "F3", Qt::Key_F3, 0, 4, 1},
                                         {62, "F4", Qt::Key_F4, 0, 5, 1},
                                         {63, "F5", Qt::Key_F5, 0, 6.5, 1},
                                         {64, "F6", Qt::Key_F6, 0, 7.5, 1},
                                         {65, "F7", Qt::Key_F7, 0, 8.5, 1},
                                         {66, "F8", Qt::Key_F8, 0, 9.5, 1},
                                         {67, "F9", Qt::Key_F9, 0, 11, 1},
                                         {68, "F10", Qt::Key_F10, 0, 12, 1},
                                         {87, "F11", Qt::Key_F11, 0, 13, 1},
                                         {88, "F12", Qt::Key_F12, 0, 14, 1},
                                         {41, "`", Qt::Key_QuoteLeft, 1, 0, 1},
                                         {2, "1", Qt::Key_1, 1, 1, 1},
                                         {3, "2", Qt::Key_2, 1, 2, 1},
                                         {4, "3", Qt::Key_3, 1, 3, 1},
                                         {5, "4", Qt::Key_4, 1, 4, 1},
                                         {6, "5", Qt::Key_5, 1, 5, 1},
                                         {7, "6", Qt::Key_6, 1, 6, 1},
                                         {8, "7", Qt::Key_7, 1, 7, 1},
                                         {9, "8", Qt::Key_8, 1, 8, 1},
                                         {10, "9", Qt::Key_9, 1, 9, 1},
                                         {11, "0", Qt::Key_0, 1, 10, 1},
                                         {12, "−", Qt::Key_Minus, 1, 11, 1},
                                         {13, "=", Qt::Key_Equal, 1, 12, 1},
                                         {14, "⌫", Qt::Key_Backspace, 1, 13, 2},
                                         {15, "Tab", Qt::Key_Tab, 2, 0, 1.5},
                                         {16, "Q", Qt::Key_Q, 2, 1.5, 1},
                                         {17, "W", Qt::Key_W, 2, 2.5, 1},
                                         {18, "E", Qt::Key_E, 2, 3.5, 1},
                                         {19, "R", Qt::Key_R, 2, 4.5, 1},
                                         {20, "T", Qt::Key_T, 2, 5.5, 1},
                                         {21, "Y", Qt::Key_Y, 2, 6.5, 1},
                                         {22, "U", Qt::Key_U, 2, 7.5, 1},
                                         {23, "I", Qt::Key_I, 2, 8.5, 1},
                                         {24, "O", Qt::Key_O, 2, 9.5, 1},
                                         {25, "P", Qt::Key_P, 2, 10.5, 1},
                                         {26, "[", Qt::Key_BracketLeft, 2, 11.5, 1},
                                         {27, "]", Qt::Key_BracketRight, 2, 12.5, 1},
                                         {43, "\\", Qt::Key_Backslash, 2, 13.5, 1.5},
                                         {58, "Caps", Qt::Key_CapsLock, 3, 0, 1.8},
                                         {30, "A", Qt::Key_A, 3, 1.8, 1},
                                         {31, "S", Qt::Key_S, 3, 2.8, 1},
                                         {32, "D", Qt::Key_D, 3, 3.8, 1},
                                         {33, "F", Qt::Key_F, 3, 4.8, 1},
                                         {34, "G", Qt::Key_G, 3, 5.8, 1},
                                         {35, "H", Qt::Key_H, 3, 6.8, 1},
                                         {36, "J", Qt::Key_J, 3, 7.8, 1},
                                         {37, "K", Qt::Key_K, 3, 8.8, 1},
                                         {38, "L", Qt::Key_L, 3, 9.8, 1},
                                         {39, ";", Qt::Key_Semicolon, 3, 10.8, 1},
                                         {40, "'", Qt::Key_Apostrophe, 3, 11.8, 1},
                                         {28, "Enter", Qt::Key_Return, 3, 12.8, 2.2},
                                         {42, "Shift", Qt::Key_Shift, 4, 0, 2.3},
                                         {44, "Z", Qt::Key_Z, 4, 2.3, 1},
                                         {45, "X", Qt::Key_X, 4, 3.3, 1},
                                         {46, "C", Qt::Key_C, 4, 4.3, 1},
                                         {47, "V", Qt::Key_V, 4, 5.3, 1},
                                         {48, "B", Qt::Key_B, 4, 6.3, 1},
                                         {49, "N", Qt::Key_N, 4, 7.3, 1},
                                         {50, "M", Qt::Key_M, 4, 8.3, 1},
                                         {51, ",", Qt::Key_Comma, 4, 9.3, 1},
                                         {52, ".", Qt::Key_Period, 4, 10.3, 1},
                                         {53, "/", Qt::Key_Slash, 4, 11.3, 1},
                                         {54, "Shift", Qt::Key_Shift, 4, 12.3, 2.7},
                                         {29, "Ctrl", Qt::Key_Control, 5, 0, 1.5},
                                         {56, "Alt", Qt::Key_Alt, 5, 2.5, 1.3},
                                         {57, "Space · 延音", Qt::Key_Space, 5, 3.8, 6.5},
                                         {312, "Alt", Qt::Key_AltGr, 5, 10.3, 1.3},
                                         {285, "Ctrl", Qt::Key_Control, 5, 13.5, 1.5},
                                         {69, "Num", Qt::Key_NumLock, 1, 16, 1},
                                         {309, "/", Qt::Key_division, 1, 17, 1},
                                         {55, "*", Qt::Key_multiply, 1, 18, 1},
                                         {74, "−", Qt::Key_Minus, 1, 19, 1},
                                         {71, "7", Qt::Key_Home, 2, 16, 1},
                                         {72, "8", Qt::Key_Up, 2, 17, 1},
                                         {73, "9", Qt::Key_PageUp, 2, 18, 1},
                                         {78, "+", Qt::Key_Plus, 2, 19, 1},
                                         {75, "4", Qt::Key_Left, 3, 16, 1},
                                         {76, "5", Qt::Key_Clear, 3, 17, 1},
                                         {77, "6", Qt::Key_Right, 3, 18, 1},
                                         {79, "1", Qt::Key_End, 4, 16, 1},
                                         {80, "2", Qt::Key_Down, 4, 17, 1},
                                         {81, "3", Qt::Key_PageDown, 4, 18, 1},
                                         {284, "↵", Qt::Key_Enter, 4, 19, 1},
                                         {82, "0", Qt::Key_Insert, 5, 16, 2},
                                         {83, ".", Qt::Key_Delete, 5, 18, 1}};
        for (auto &k : keys)
            if (k.column >= 16)
                k.column += 4;
        const std::vector<PhysicalKey> navigation = {
            {338, "Ins", Qt::Key_Insert, 1, 16, 1},  {327, "Home", Qt::Key_Home, 1, 17, 1},
            {329, "PgUp", Qt::Key_PageUp, 1, 18, 1}, {339, "Del", Qt::Key_Delete, 2, 16, 1},
            {335, "End", Qt::Key_End, 2, 17, 1},     {337, "PgDn", Qt::Key_PageDown, 2, 18, 1},
            {328, "↑", Qt::Key_Up, 4, 17, 1},        {331, "←", Qt::Key_Left, 5, 16, 1},
            {336, "↓", Qt::Key_Down, 5, 17, 1},      {333, "→", Qt::Key_Right, 5, 18, 1}};
        keys.insert(keys.end(), navigation.begin(), navigation.end());
        return keys;
    }();
    return keys;
}
QString keyName(uint16_t code) {
    for (const auto &k : physicalKeys())
        if (k.code == code)
            return (k.column >= 20 ? QStringLiteral("Num ") : QString()) + QString::fromUtf8(k.label);
    return QString("0x%1").arg(code, 2, 16, QChar('0'));
}
uint16_t scanFromQt(int key, quint32 scan) {
#ifdef Q_OS_WIN
    if (scan > 0 && scan <= 511)
        return uint16_t(scan);
#endif
#ifdef Q_OS_LINUX
    // Qt/X11 nativeScanCode is evdev + 8; don't apply Windows Set-1 offsets.
    if (scan >= 9 && scan <= 96)
        return uint16_t(scan - 8);
    switch (scan) {
    case 104:
        return 284;
    case 105:
        return 285;
    case 106:
        return 309;
    case 108:
        return 312;
    case 110:
        return 327;
    case 111:
        return 328;
    case 112:
        return 329;
    case 113:
        return 331;
    case 114:
        return 333;
    case 115:
        return 335;
    case 116:
        return 336;
    case 117:
        return 337;
    case 118:
        return 338;
    case 119:
        return 339;
    default:
        break;
    }
#endif
    for (const auto &physical : physicalKeys())
        if (physical.qtKey == key)
            return physical.code;
    return 0;
}
Keymap Keymap::defaults() {
    Keymap result;
    auto add = [&](int key, int note, Hand hand) {
        result.bindings[uint16_t(key)] = {uint16_t(key), note, hand, false};
    };
    const std::array<int, 7> steps{0, 2, 4, 5, 7, 9, 11};
    // Diatonic rows retain the familiar keyboard-piano hand arrangement.
    const auto row = [&](const std::vector<int> &keys, int base, Hand hand) {
        for (size_t i = 0; i < keys.size(); ++i)
            add(keys[i], base + steps[i % 7] + 12 * int(i / 7), hand);
    };
    row({2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 72, Hand::Left);
    row({16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 43}, 60, Hand::Left);
    row({30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 28}, 48, Hand::Left);
    row({44, 45, 46, 47, 48, 49, 50, 51, 52, 53}, 36, Hand::Left);
    add(41, 71, Hand::Left);
    add(15, 59, Hand::Left);
    add(58, 47, Hand::Left);
    row({331, 336, 333, 328, 82, 83, 284}, 48, Hand::Right);
    row({79, 80, 81, 75, 76, 77, 71, 72, 73, 78, 69, 309, 55, 74}, 60, Hand::Right);
    row({339, 335, 337, 338, 327, 329}, 84, Hand::Right);
    result.bindings[57] = {57, 0, Hand::Unknown, true};
    return result;
}
namespace {
int jsonInt(const QJsonObject &o, const char *field, int lo, int hi) {
    const auto v = o.value(QLatin1String(field));
    double n = v.toDouble(-9999);
    if (!v.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < lo || n > hi)
        throw FileError("invalid_keymap", QString("Invalid %1").arg(field));
    return int(n);
}
} // namespace
Keymap Keymap::read(const QString &path) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(readBounded(path), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw FileError("invalid_keymap", "Expected JSON object");
    auto o = doc.object();
    if (o.value("format") != "openkeys-keymap" || jsonInt(o, "version", 1, 1) != 1)
        throw FileError("invalid_keymap", "Unknown format/version");
    Keymap map;
    map.transpose = jsonInt(o, "transpose", -12, 12);
    map.octave = {jsonInt(o, "leftOctave", -3, 3), jsonInt(o, "rightOctave", -3, 3)};
    map.velocity = {jsonInt(o, "leftVelocity", 1, 127), jsonInt(o, "rightVelocity", 1, 127)};
    if (!o.value("bindings").isArray() || o.value("bindings").toArray().size() > 511)
        throw FileError("invalid_keymap", "Invalid bindings");
    for (auto item : o.value("bindings").toArray()) {
        if (!item.isObject())
            throw FileError("invalid_keymap", "Invalid binding");
        auto b = item.toObject();
        const auto key = uint16_t(jsonInt(b, "key", 1, 511));
        if (key == 42 || key == 54)
            throw FileError("invalid_keymap", "Shift is reserved for velocity modifiers");
        if (map.bindings.contains(key))
            throw FileError("invalid_keymap", "Duplicate binding");
        if (!b.value("sustain").isBool())
            throw FileError("invalid_keymap", "Invalid sustain flag");
        map.bindings[key] = {key, jsonInt(b, "note", 0, 127), Hand(jsonInt(b, "hand", 0, 2)),
                             b.value("sustain").toBool()};
    }
    return map;
}
void Keymap::save(const QString &path) const {
    QJsonArray entries;
    for (const auto &[key, b] : bindings)
        entries.append(
            QJsonObject{{"key", key}, {"note", b.note}, {"hand", int(b.hand)}, {"sustain", b.sustain}});
    QJsonObject o{{"format", "openkeys-keymap"},  {"version", 1},
                  {"transpose", transpose},       {"leftOctave", octave[0]},
                  {"rightOctave", octave[1]},     {"leftVelocity", velocity[0]},
                  {"rightVelocity", velocity[1]}, {"bindings", entries}};
    auto bytes = QJsonDocument(o).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw FileError("io_error", file.errorString());
}
std::vector<Event> KeyboardState::change(uint16_t key, bool down, int64_t time) {
    if (key == 42) {
        soft = down;
        return {};
    }
    if (key == 54) {
        hard = down;
        return {};
    }
    auto found = pressed_.find(key);
    if (!down) {
        if (found == pressed_.end())
            return {};
        std::vector<Event> out;
        for (auto e : found->second) {
            e.timeUs = time;
            if (e.kind == Kind::Sustain) {
                if (--pedals_[size_t(e.hand)] > 0)
                    continue;
                e.velocity = 0;
            } else {
                e.kind = Kind::NoteOff;
                e.velocity = 0;
            }
            out.push_back(e);
        }
        pressed_.erase(found);
        return out;
    }
    if (found != pressed_.end())
        return {};
    const auto mapping = keymap.bindings.find(key);
    if (mapping == keymap.bindings.end())
        return {};
    const auto &b = mapping->second;
    std::vector<Event> out, heldEvents;
    if (b.sustain) {
        for (int hand = 0; hand < 3; ++hand) {
            Event e{time, Kind::Sustain, 0, 127, Hand(hand), key, 0};
            heldEvents.push_back(e);
            if (pedals_[size_t(hand)]++ == 0)
                out.push_back(e);
        }
    } else {
        const int hand = b.hand == Hand::Left ? 0 : 1;
        const int pitch = b.note + keymap.transpose + 12 * keymap.octave[size_t(hand)];
        if (pitch < 0 || pitch > 127)
            return {};
        int velocity = keymap.velocity[size_t(hand)];
        if (soft)
            velocity = int(std::lround(velocity * .65));
        if (hard)
            velocity = int(std::lround(velocity * 1.25));
        Event e{time, Kind::NoteOn, uint8_t(pitch), uint8_t(std::clamp(velocity, 1, 127)), b.hand, key, 0};
        out.push_back(e);
        heldEvents.push_back(e);
    }
    pressed_[key] = std::move(heldEvents);
    return out;
}
std::vector<Event> KeyboardState::releaseAll(int64_t time) {
    std::vector<Event> result;
    while (!pressed_.empty()) {
        auto events = change(pressed_.begin()->first, false, time);
        result.insert(result.end(), events.begin(), events.end());
    }
    soft = false;
    hard = false;
    return result;
}
} // namespace piano
