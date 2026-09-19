#include "core/InputManager.h"
#include "core/KeyMapping.h"
#include <M5Cardputer.h>
#include <cstring>

void InputManager::begin() {}
InputEvent InputManager::poll() {
    static uint8_t previous[16] = {};
    static uint8_t previousCount = 0, repeating = 0;
    static bool previousFn = false;
    static uint32_t repeatAt = 0;
    static InputEvent queue[16];
    static uint8_t head = 0, tail = 0;
    const auto& keys = M5Cardputer.Keyboard.keysState();
    auto enqueue = [&](uint8_t raw, bool repeat) {
        InputEvent e;
        e.type = InputType::Key; e.pressed = true;
        e.fn = keys.fn; e.ctrl = keys.ctrl; e.alt = keys.alt; e.repeat = repeat;
        e.code = KeyMapping::code(raw, keys.fn);
        e.key = KeyMapping::text(e.code ? e.code : raw, keys.shift);
        const uint8_t next = (tail + 1) % 16;
        if (next != head) { queue[tail] = e; tail = next; }
    };
    for (uint8_t raw : keys.hid_keys) {
        bool wasDown = false;
        for (uint8_t i = 0; i < previousCount; ++i) if (previous[i] == raw) wasDown = true;
        if (!wasDown || previousFn != keys.fn) {
            enqueue(raw, false); repeating = raw; repeatAt = millis() + 450;
        }
    }
    bool stillDown = false;
    for (uint8_t raw : keys.hid_keys) if (raw == repeating) stillDown = true;
    if (!stillDown) repeating = 0;
    if (repeating && static_cast<int32_t>(millis() - repeatAt) >= 0) {
        enqueue(repeating, true); repeatAt = millis() + 65;
    }
    previousCount = 0;
    for (uint8_t raw : keys.hid_keys) if (previousCount < 16) previous[previousCount++] = raw;
    previousFn = keys.fn;
    if (head == tail) return {};
    const InputEvent result = queue[head]; head = (head + 1) % 16;
    return result;
}
