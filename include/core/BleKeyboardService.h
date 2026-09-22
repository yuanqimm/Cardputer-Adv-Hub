#pragma once

namespace BleKeyboardService {
void begin();
void suspend();
void resume();
void update(bool enabled);
bool connected();
bool dirty();
const char* status();
const char* diagnostic();
const char* counters();
void resetPairings();
}
