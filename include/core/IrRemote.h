#pragma once

#include <Arduino.h>

namespace IrRemote {
void begin();
bool ready();
void sendCommand(unsigned char command);
void sendButton(uint8_t index);
const char* profileName();
const char* status();
uint8_t buttonCount();
const char* buttonLabel(uint8_t index);
void nextProfile();
bool loadProfile();
}
