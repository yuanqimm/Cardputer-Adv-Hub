#pragma once

#include "core/InputManager.h"

namespace SshService {
void begin();
void loop();
void connect();
void disconnect();
void sendInput(const InputEvent& event);
bool dirty();
bool connected();
const char* status();
const char* terminalRow(uint8_t row);
bool awaitingTrust();
const char* fingerprint();
void trustServer();
}
