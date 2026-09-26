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
#include "USBCDC.h"
#include "USBHIDKeyboard.h"
#include "USBMSC.h"

#include "core/KeyMapping.h"
#include <cstring>
#include <tusb.h>
// Explicit HID registration mirrors the Arduino composite-device example.
// USBHIDKeyboard owns another USBHID helper, but the helper only registers the
// interface once; declaring it first makes the interface order deterministic.
#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC usbSerial;
#endif
USBHID usbHid;
namespace { USBHIDKeyboard keyboard; }
// Keep both interfaces registered before the CDC-on-boot core starts USB.
// UsbStorageService configures this object but exposes media only on request.
USBMSC usbStorageMsc;
namespace UsbKeyboardService {
void begin() {
    // The configured CDC-on-boot build has already started USB. begin() is
    // idempotent; the conditional CDC object supports manual-start builds.
#if !ARDUINO_USB_CDC_ON_BOOT
    usbSerial.begin();
#endif
    keyboard.begin();
    USB.begin();
}
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
