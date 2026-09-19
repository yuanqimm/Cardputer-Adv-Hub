#pragma once

namespace BleKeyboardService {
void begin();
void update(bool enabled);
bool connected();
bool dirty();
const char* status();
const char* diagnostic();
const char* counters();
void resetPairings();
}
