#pragma once
#include "core/InputManager.h"
namespace SshService {
void begin(); void loop(); void connect(); void disconnect(); void cancel(); void scanWifi(); void showSaved(); void moveSelection(int delta); bool selectCurrent(); void sendInput(const InputEvent& event);
bool enteringPassword(); void editPassword(const InputEvent& event); void changeWifiPassword(); const char* passwordDisplay(); const char* passwordSsid();
bool editingSsh(); void beginSshSetup(); void editSsh(const InputEvent& event); const char* sshFieldName(); const char* sshEditDisplay();
bool dirty(); bool connected(); const char* status(); const char* terminalRow(uint8_t row); bool awaitingTrust(); const char* fingerprint(); void trustServer();
uint8_t view(); uint8_t wifiCount(); uint8_t wifiSelected(); const char* wifiName(uint8_t index); int32_t wifiRssi(uint8_t index); bool wifiSecured(uint8_t index);
uint8_t savedCount(); uint8_t savedSelected(); const char* savedSsid(uint8_t index); const char* savedTarget(uint8_t index);
}
