#pragma once

namespace KeyboardManager {
void begin();
void update();
bool bleConnected();
enum class Mode { Usb, Bluetooth, Both };
void setActive(bool active);
void cycleMode();
Mode mode();
const char* modeName();
}
