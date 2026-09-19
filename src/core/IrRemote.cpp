#define DISABLE_CODE_FOR_RECEIVER
#define SEND_PWM_BY_TIMER
#define IR_TX_PIN 44
#include "core/IrRemote.h"
#include <IRremote.hpp>
#include <ArduinoJson.h>
#include <SD.h>
#include <vector>
namespace {
enum class Protocol { NEC, Samsung, Sony, RC5 };
struct Button { String label; uint16_t command; };
struct Profile { String name; Protocol protocol = Protocol::NEC; uint16_t address = 0; uint8_t bits = 12, repeats = 0; std::vector<Button> buttons; };
std::vector<Profile> profiles;
size_t selected = 0;
String message = "Demo codes: configure SD";
bool initialized = false;
bool number(JsonVariantConst v, uint32_t max, uint32_t& result) {
    if (v.is<uint32_t>()) result = v.as<uint32_t>();
    else if (v.is<const char*>()) {
        const char* text = v.as<const char*>(); char* end = nullptr;
        if (!text || !text[0] || text[0] == '-') return false;
        result = strtoul(text, &end, (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10);
        if (!end || *end) return false;
    } else return false;
    return result <= max;
}
bool parse(JsonObjectConst obj, Profile& p) {
    p.name = obj["name"] | "Remote"; p.name = p.name.substring(0, 24);
    String protocol = obj["protocol"] | "NEC"; protocol.toUpperCase();
    if (protocol == "NEC") p.protocol = Protocol::NEC;
    else if (protocol == "SAMSUNG") p.protocol = Protocol::Samsung;
    else if (protocol == "SONY") p.protocol = Protocol::Sony;
    else if (protocol == "RC5") p.protocol = Protocol::RC5;
    else return false;
    uint32_t value = 0;
    if (!obj["address"].isNull() && !number(obj["address"], 65535, value)) return false;
    p.address = value;
    value = 0;
    if (!obj["repeats"].isNull() && !number(obj["repeats"], 3, value)) return false;
    p.repeats = value;
    value = 12;
    if (!obj["bits"].isNull() && !number(obj["bits"], 20, value)) return false;
    p.bits = value;
    if (p.protocol == Protocol::RC5 && p.address > 31) return false;
    if (p.protocol == Protocol::Sony && ((p.bits != 12 && p.bits != 15 && p.bits != 20) ||
        p.address > (p.bits == 12 ? 31 : p.bits == 15 ? 255 : 8191))) return false;
    const uint32_t maxCommand = p.protocol == Protocol::Samsung ? 65535 : p.protocol == Protocol::Sony || p.protocol == Protocol::RC5 ? 127 : 255;
    if (obj["buttons"].is<JsonArrayConst>()) {
        for (JsonObjectConst button : obj["buttons"].as<JsonArrayConst>()) {
            if (p.buttons.size() >= 9 || !number(button["command"], maxCommand, value)) return false;
            String label = button["label"] | "Button";
            p.buttons.push_back({label.substring(0, 18), static_cast<uint16_t>(value)});
        }
    } else {
        const char* names[] = {"power", "vol_minus", "vol_plus"};
        const char* labels[] = {"Power", "Vol-", "Vol+"};
        for (unsigned i = 0; i < 3; ++i) {
            if (!number(obj["buttons"][names[i]], maxCommand, value)) return false;
            String label = obj["buttons"][String(names[i]) + "_label"] | labels[i];
            p.buttons.push_back({label.substring(0, 18), static_cast<uint16_t>(value)});
        }
    }
    return !p.buttons.empty();
}
}
namespace IrRemote {
void begin() {
    IrSender.begin(DISABLE_LED_FEEDBACK); IrSender.setSendPin(IR_TX_PIN); initialized = true;
    Profile demo; demo.name = "Demo NEC (address 0)";
    demo.buttons = {{"Power", 0x45}, {"Vol-", 0x46}, {"Vol+", 0x47}};
    profiles.push_back(demo); loadProfile();
}
bool ready() { return initialized; }
const char* profileName() { return profiles[selected].name.c_str(); }
const char* status() { return message.c_str(); }
uint8_t buttonCount() { return profiles[selected].buttons.size(); }
const char* buttonLabel(uint8_t index) { return index < buttonCount() ? profiles[selected].buttons[index].label.c_str() : ""; }
void nextProfile() { selected = (selected + 1) % profiles.size(); message = "1-9 send   R reload"; }
void sendCommand(unsigned char command) { if (initialized) IrSender.sendNEC(0, command, 0); }
void sendButton(uint8_t index) {
    if (!initialized || index >= buttonCount()) return;
    const auto& p = profiles[selected]; const auto& b = p.buttons[index];
    switch (p.protocol) {
    case Protocol::NEC: IrSender.sendNEC(p.address, b.command, p.repeats); break;
    case Protocol::Samsung: IrSender.sendSamsung(p.address, b.command, p.repeats); break;
    case Protocol::Sony: IrSender.sendSony(p.address, b.command, p.repeats, p.bits); break;
    case Protocol::RC5: IrSender.sendRC5(p.address, b.command, p.repeats); break;
    }
    message = "Sent: " + b.label;
}
bool loadProfile() {
    File file = SD.open("/config/ir.json", FILE_READ);
    if (!file) { message = "No /config/ir.json (demo)"; return false; }
    if (file.size() > 16384) { message = "IR config exceeds 16KB"; return false; }
    JsonDocument doc;
    if (deserializeJson(doc, file)) { message = "Invalid IR JSON"; return false; }
    std::vector<Profile> incoming;
    auto append = [&](JsonObjectConst obj) {
        if (obj.isNull() || incoming.size() >= 8) return false;
        Profile p; if (!parse(obj, p)) return false;
        incoming.push_back(p); return true;
    };
    bool ok = true;
    if (doc.is<JsonArray>()) { for (JsonObjectConst obj : doc.as<JsonArrayConst>()) if (!append(obj)) { ok = false; break; } }
    else ok = append(doc.as<JsonObjectConst>());
    if (!ok || incoming.empty()) { message = "Invalid IR profile/range"; return false; }
    profiles.swap(incoming); selected = 0; message = "Profiles loaded"; return true;
}
}
