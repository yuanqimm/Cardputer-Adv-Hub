#pragma once

namespace UsbStorageService {
void begin();
void loop();
bool available();
bool hostActive();
const char* status();
}
