#include "core/KeyboardManager.h"
#include "core/BleKeyboardService.h"
#include "core/UsbKeyboardService.h"
#include <M5Cardputer.h>
#include <Preferences.h>
namespace {
bool active = false, waitRelease = true;
KeyboardManager::Mode selected = KeyboardManager::Mode::Both;
}
namespace KeyboardManager {
void begin() {
    Preferences prefs;
    if (prefs.begin("hub-settings", true)) {
        const uint8_t value = prefs.getUChar("kbd-mode", 2);
        if (value <= 2) selected = static_cast<Mode>(value);
        prefs.end();
    }
    UsbKeyboardService::begin(); BleKeyboardService::begin();
}
void setActive(bool value) {
    if (active != value) { active = value; waitRelease = true; }
}
void cycleMode() {
    selected = static_cast<Mode>((static_cast<unsigned>(selected) + 1) % 3);
    waitRelease = true;
    Preferences prefs;
    if (prefs.begin("hub-settings", false)) { prefs.putUChar("kbd-mode", static_cast<uint8_t>(selected)); prefs.end(); }
}
void update() {
    if (!M5Cardputer.Keyboard.isPressed()) waitRelease = false;
    const bool send = active && !waitRelease;
    UsbKeyboardService::update(send && selected != Mode::Bluetooth);
    BleKeyboardService::update(send && selected != Mode::Usb);
}
bool bleConnected() { return BleKeyboardService::connected(); }
Mode mode() { return selected; }
const char* modeName() { return selected == Mode::Usb ? "USB" : selected == Mode::Bluetooth ? "Bluetooth" : "USB + Bluetooth"; }
}
