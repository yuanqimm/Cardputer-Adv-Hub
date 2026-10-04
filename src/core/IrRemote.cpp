#define DISABLE_CODE_FOR_RECEIVER
#define SEND_PWM_BY_TIMER
#define IR_TX_PIN 44

#include "core/IrRemote.h"
#include "core/Storage.h"
#include <IRremote.hpp>
#include <ArduinoJson.h>
#include <SD.h>
#include <vector>

namespace {
enum class Protocol { NEC, Samsung, Sony, RC5, LG, JVC, Panasonic, Denon, Sharp, Raw };

struct Button {
    String label;
    uint16_t command = 0;
    std::vector<uint16_t> raw;
};

struct Profile {
    String name;
    Protocol protocol = Protocol::NEC;
    uint16_t address = 0;
    uint8_t bits = 12;
    uint8_t repeats = 0;
    uint8_t frequency = 38;
    std::vector<Button> buttons;
};

std::vector<Profile> profiles;
size_t selected = 0;
String message = "Demo codes: configure SD";
bool initialized = false;

bool number(JsonVariantConst value, uint32_t maximum, uint32_t& result) {
    if (value.is<uint32_t>()) {
        result = value.as<uint32_t>();
    } else if (value.is<const char*>()) {
        const char* text = value.as<const char*>();
        char* end = nullptr;
        if (!text || !text[0] || text[0] == '-') return false;
        result = strtoul(text, &end,
            (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10);
        if (!end || *end) return false;
    } else {
        return false;
    }
    return result <= maximum;
}

bool parseProtocol(const String& value, Protocol& protocol) {
    String name = value;
    name.toUpperCase();
    if (name == "NEC") protocol = Protocol::NEC;
    else if (name == "SAMSUNG") protocol = Protocol::Samsung;
    else if (name == "SONY") protocol = Protocol::Sony;
    else if (name == "RC5") protocol = Protocol::RC5;
    else if (name == "LG") protocol = Protocol::LG;
    else if (name == "JVC") protocol = Protocol::JVC;
    else if (name == "PANASONIC" || name == "KASEIKYO") protocol = Protocol::Panasonic;
    else if (name == "DENON") protocol = Protocol::Denon;
    else if (name == "SHARP") protocol = Protocol::Sharp;
    else if (name == "RAW" || name == "RAW_DATA" || name == "RAW DATA") protocol = Protocol::Raw;
    else return false;
    return true;
}

const char* protocolText(Protocol protocol) {
    switch (protocol) {
    case Protocol::NEC: return "NEC";
    case Protocol::Samsung: return "Samsung";
    case Protocol::Sony: return "Sony";
    case Protocol::RC5: return "RC5";
    case Protocol::LG: return "LG";
    case Protocol::JVC: return "JVC";
    case Protocol::Panasonic: return "Panasonic";
    case Protocol::Denon: return "Denon";
    case Protocol::Sharp: return "Sharp";
    case Protocol::Raw: return "RAW";
    }
    return "Unknown";
}

uint32_t maxCommandFor(Protocol protocol) {
    switch (protocol) {
    case Protocol::Samsung:
    case Protocol::LG:
    case Protocol::NEC:
        return 65535;
    case Protocol::Sony:
    case Protocol::RC5:
        return 127;
    case Protocol::Panasonic:
    case Protocol::Denon:
    case Protocol::Sharp:
    case Protocol::JVC:
        return 255;
    case Protocol::Raw:
        return 0;
    }
    return 0;
}

bool parseRaw(JsonVariantConst value, std::vector<uint16_t>& raw) {
    if (!value.is<JsonArrayConst>()) return false;
    JsonArrayConst values = value.as<JsonArrayConst>();
    if (values.size() < 2 || values.size() > 160) return false;
    raw.reserve(values.size());
    for (JsonVariantConst item : values) {
        uint32_t duration = 0;
        if (!number(item, 65535, duration) || duration < 100) return false;
        raw.push_back(static_cast<uint16_t>(duration));
    }
    return true;
}

bool validAddress(const Profile& profile) {
    if (profile.protocol == Protocol::RC5) return profile.address <= 31;
    if (profile.protocol == Protocol::Sony) {
        const uint16_t maximum = profile.bits == 12 ? 31 : profile.bits == 15 ? 255 : 8191;
        return profile.address <= maximum;
    }
    if (profile.protocol == Protocol::LG || profile.protocol == Protocol::JVC ||
        profile.protocol == Protocol::Denon || profile.protocol == Protocol::Sharp) {
        return profile.address <= 255;
    }
    return true;
}

bool parse(JsonObjectConst object, Profile& profile) {
    profile.name = object["name"] | "Remote";
    profile.name = profile.name.substring(0, 24);
    if (!parseProtocol(object["protocol"] | "NEC", profile.protocol)) return false;

    uint32_t value = 0;
    if (!object["address"].isNull() && !number(object["address"], 65535, value)) return false;
    profile.address = static_cast<uint16_t>(value);
    value = 0;
    if (!object["repeats"].isNull() && !number(object["repeats"], 3, value)) return false;
    profile.repeats = static_cast<uint8_t>(value);
    value = 12;
    if (!object["bits"].isNull() && !number(object["bits"], 20, value)) return false;
    profile.bits = static_cast<uint8_t>(value);
    if (profile.protocol == Protocol::Sony && profile.bits != 12 &&
        profile.bits != 15 && profile.bits != 20) return false;

    if (profile.protocol == Protocol::Raw) {
        value = 38;
        if (!object["frequency"].isNull() && !number(object["frequency"], 60, value)) return false;
        if (value < 30) return false;
        profile.frequency = static_cast<uint8_t>(value);
    }
    if (!validAddress(profile)) return false;

    JsonArrayConst buttons = object["buttons"].as<JsonArrayConst>();
    if (buttons.isNull() || buttons.size() == 0 || buttons.size() > 9) return false;
    for (JsonObjectConst objectButton : buttons) {
        if (objectButton.isNull()) return false;
        String label = objectButton["label"] | "Button";
        Button button;
        button.label = label.substring(0, 18);
        if (profile.protocol == Protocol::Raw) {
            if (!parseRaw(objectButton["raw"], button.raw)) return false;
        } else {
            if (!number(objectButton["command"], maxCommandFor(profile.protocol), value)) return false;
            button.command = static_cast<uint16_t>(value);
        }
        profile.buttons.push_back(button);
    }
    return !profile.buttons.empty();
}

bool parseLegacyButtons(JsonObjectConst object, Profile& profile) {
    const char* names[] = {"power", "vol_minus", "vol_plus"};
    const char* labels[] = {"Power", "Vol-", "Vol+"};
    uint32_t value = 0;
    const uint32_t maximum = maxCommandFor(profile.protocol);
    for (unsigned index = 0; index < 3; ++index) {
        if (!number(object["buttons"][names[index]], maximum, value)) return false;
        String label = object["buttons"][String(names[index]) + "_label"] | labels[index];
        Button button;
        button.label = label.substring(0, 18);
        button.command = static_cast<uint16_t>(value);
        profile.buttons.push_back(button);
    }
    return true;
}
}

namespace IrRemote {
void begin() {
    IrSender.begin(DISABLE_LED_FEEDBACK);
    IrSender.setSendPin(IR_TX_PIN);
    initialized = true;
    profiles.clear();
    Profile demo;
    demo.name = "Demo NEC (address 0)";
    Button power; power.label = "Power"; power.command = 0x45; demo.buttons.push_back(power);
    Button volumeDown; volumeDown.label = "Vol-"; volumeDown.command = 0x46; demo.buttons.push_back(volumeDown);
    Button volumeUp; volumeUp.label = "Vol+"; volumeUp.command = 0x47; demo.buttons.push_back(volumeUp);
    profiles.push_back(demo);
    loadProfile();
}

bool ready() { return initialized; }
const char* profileName() { return profiles.empty() ? "No profile" : profiles[selected].name.c_str(); }
const char* protocolName() { return profiles.empty() ? "-" : protocolText(profiles[selected].protocol); }
uint16_t profileAddress() { return profiles.empty() ? 0 : profiles[selected].address; }
uint8_t profileFrequency() { return profiles.empty() ? 38 : profiles[selected].frequency; }
uint8_t profileIndex() { return profiles.empty() ? 0 : static_cast<uint8_t>(selected); }
uint8_t profileCount() { return static_cast<uint8_t>(profiles.size() > 255 ? 255 : profiles.size()); }
const char* status() { return message.c_str(); }
uint8_t buttonCount() {
    return profiles.empty() ? 0 : static_cast<uint8_t>(profiles[selected].buttons.size());
}
const char* buttonLabel(uint8_t index) {
    return !profiles.empty() && index < buttonCount() ? profiles[selected].buttons[index].label.c_str() : "";
}

void nextProfile() {
    if (profiles.empty()) return;
    selected = (selected + 1) % profiles.size();
    message = "1-9 send  B/N device";
}

void previousProfile() {
    if (profiles.empty()) return;
    selected = selected == 0 ? profiles.size() - 1 : selected - 1;
    message = "1-9 send  B/N device";
}

void sendCommand(unsigned char command) {
    if (initialized) IrSender.sendNEC(0, command, 0);
}

void sendButton(uint8_t index) {
    if (!initialized || profiles.empty() || index >= buttonCount()) return;
    const Profile& profile = profiles[selected];
    const Button& button = profile.buttons[index];
    switch (profile.protocol) {
    case Protocol::NEC: IrSender.sendNEC(profile.address, button.command, profile.repeats); break;
    case Protocol::Samsung: IrSender.sendSamsung(profile.address, button.command, profile.repeats); break;
    case Protocol::Sony: IrSender.sendSony(profile.address, button.command, profile.repeats, profile.bits); break;
    case Protocol::RC5: IrSender.sendRC5(static_cast<uint8_t>(profile.address), static_cast<uint8_t>(button.command), profile.repeats); break;
    case Protocol::LG: IrSender.sendLG(static_cast<uint8_t>(profile.address), button.command, profile.repeats); break;
    case Protocol::JVC: IrSender.sendJVC(static_cast<uint8_t>(profile.address), static_cast<uint8_t>(button.command), profile.repeats); break;
    case Protocol::Panasonic: IrSender.sendPanasonic(profile.address, static_cast<uint8_t>(button.command), profile.repeats); break;
    case Protocol::Denon: IrSender.sendDenon(static_cast<uint8_t>(profile.address), static_cast<uint8_t>(button.command), profile.repeats); break;
    case Protocol::Sharp: IrSender.sendSharp(static_cast<uint8_t>(profile.address), static_cast<uint8_t>(button.command), profile.repeats); break;
    case Protocol::Raw:
        for (uint8_t repeat = 0; repeat <= profile.repeats; ++repeat) {
            IrSender.sendRaw(button.raw.data(), button.raw.size(), profile.frequency);
            if (repeat < profile.repeats) delay(110);
        }
        break;
    }
    message = "Sent: " + button.label;
}

bool loadProfile() {
    if (!Storage::appAccessAllowed()) {
        message = "USB computer is using SD";
        return false;
    }
    File file = SD.open("/config/ir.json", FILE_READ);
    if (!file) {
        message = "No /config/ir.json (demo)";
        return false;
    }
    if (file.size() > 16384) {
        file.close();
        message = "IR config exceeds 16KB";
        return false;
    }
    JsonDocument document;
    DeserializationError error = deserializeJson(document, file);
    file.close();
    if (error) {
        message = "Invalid IR JSON";
        return false;
    }

    std::vector<Profile> incoming;
    auto append = [&](JsonObjectConst object) {
        if (object.isNull() || incoming.size() >= 8) return false;
        Profile profile;
        if (parse(object, profile)) {
            incoming.push_back(profile);
            return true;
        }
        // Preserve the original power/vol_minus/vol_plus object format.
        String protocol = object["protocol"] | "NEC";
        if (!parseProtocol(protocol, profile.protocol)) return false;
        profile.name = String(object["name"] | "Remote").substring(0, 24);
        uint32_t value = 0;
        if (!object["address"].isNull() && !number(object["address"], 65535, value)) return false;
        profile.address = static_cast<uint16_t>(value);
        value = 0;
        if (!object["repeats"].isNull() && !number(object["repeats"], 3, value)) return false;
        profile.repeats = static_cast<uint8_t>(value);
        if (!validAddress(profile) || !parseLegacyButtons(object, profile)) return false;
        incoming.push_back(profile);
        return true;
    };

    bool valid = true;
    if (document.is<JsonArray>()) {
        for (JsonObjectConst object : document.as<JsonArrayConst>()) {
            if (!append(object)) { valid = false; break; }
        }
    } else {
        valid = append(document.as<JsonObjectConst>());
    }
    if (!valid || incoming.empty()) {
        message = "Invalid IR profile/range";
        return false;
    }
    profiles.swap(incoming);
    selected = 0;
    message = "Profiles loaded";
    return true;
}
}
