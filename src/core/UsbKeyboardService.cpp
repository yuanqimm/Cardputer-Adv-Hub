#include "core/UsbKeyboardService.h"

#include <M5Cardputer.h>
#undef KEY_LEFT_CTRL
#undef KEY_LEFT_SHIFT
#undef KEY_LEFT_ALT
#undef KEY_LEFT_GUI
#undef KEY_RIGHT_CTRL
#undef KEY_RIGHT_SHIFT
#undef KEY_RIGHT_ALT
#undef KEY_RIGHT_GUI
#undef KEY_UP_ARROW
#undef KEY_DOWN_ARROW
#undef KEY_LEFT_ARROW
#undef KEY_RIGHT_ARROW
#undef KEY_BACKSPACE
#undef KEY_TAB
#undef KEY_RETURN
#undef KEY_ESC
#include "USB.h"
#include "USBHIDKeyboard.h"

#include "core/KeyMapping.h"
#include <cstring>
#include <tusb.h>
namespace { USBHIDKeyboard keyboard; }
namespace UsbKeyboardService {
void begin() { keyboard.begin(); USB.begin(); }
bool connected() { return tud_mounted(); }
void update(bool enabled) {
    static KeyReport previous = {};
    static bool sent = false;
    if (!connected()) { sent = false; return; }
    if (!tud_hid_ready()) return;
    const auto& keys = M5Cardputer.Keyboard.keysState();
    KeyReport report = {};
    if (enabled) {
        report.modifiers = keys.modifiers | (keys.opt ? 0x08 : 0);
        unsigned count = 0;
        for (uint8_t raw : keys.hid_keys) {
            const uint8_t mapped = KeyMapping::code(raw, keys.fn);
            if (mapped && count < 6) report.keys[count++] = mapped;
        }
    }
    if (!sent || memcmp(&previous, &report, sizeof(report))) {
        keyboard.sendReport(&report);
        previous = report; sent = true;
    }
}
}
