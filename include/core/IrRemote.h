#pragma once

#include <Arduino.h>

namespace IrRemote {
void begin();
bool ready();
void sendCommand(unsigned char command);
void sendButton(uint8_t index);
const char* profileName();
const char* protocolName();
uint16_t profileAddress();
uint8_t profileFrequency();
uint8_t profileIndex();
uint8_t profileCount();
const char* status();
uint8_t buttonCount();
const char* buttonLabel(uint8_t index);
void nextProfile();
void previousProfile();
bool loadProfile();
}
