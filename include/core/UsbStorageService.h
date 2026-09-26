#pragma once

namespace UsbStorageService {
void begin();
void loop();
// Call on the Arduino loop: transfers SD ownership and closes/remounts files.
bool setEnabled(bool enabled);
bool dirty();
bool available();
// Sharing is enabled, including while waiting for a computer to mount the SD.
bool hostActive();
const char* status();
}
