#include "core/BleKeyboardService.h"
#include "core/HidKeyboardReport.h"
#include "core/KeyMapping.h"
#include <M5Cardputer.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <esp_system.h>
#include <atomic>
#include <cstring>

namespace {
constexpr char DeviceName[] = "Cardputer Hub N2";
constexpr uint32_t DiagnosticMagic = 0x484e3201;
char deviceAddress[18] = {};
// Keep one static-random identity across boots; a name change alone leaves host
// GATT/bond caches associated with the public address of previous firmware.
bool configureIdentity() {
    Preferences settings;
    if (!settings.begin("hub-ble", false)) return false;
    ble_addr_t address = {};
    address.type = BLE_ADDR_RANDOM;
    const bool saved = settings.getBytesLength("identity-v2") == sizeof(address.val) &&
        settings.getBytes("identity-v2", address.val, sizeof(address.val)) == sizeof(address.val);
    if (!saved) {
        if (ble_hs_id_gen_rnd(0, &address) != 0 ||
            settings.putBytes("identity-v2", address.val, sizeof(address.val)) != sizeof(address.val)) {
            settings.end();
            return false;
        }
    }
    settings.end();
    if (ble_hs_id_set_rnd(address.val) != 0) return false;
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    snprintf(deviceAddress, sizeof(deviceAddress), "%s", NimBLEAddress(address).toString().c_str());
    return true;
}
// RTC history survives software resets without writing flash on each event.
struct History { uint32_t magic, connects, disconnects; int reason; };
RTC_NOINIT_ATTR History history;
portMUX_TYPE historyMux = portMUX_INITIALIZER_UNLOCKED;
History readHistory() {
    portENTER_CRITICAL(&historyMux);
    History result = history;
    portEXIT_CRITICAL(&historyMux);
    return result;
}
NimBLEServer* server = nullptr;
NimBLECharacteristic* reportInput = nullptr;
NimBLECharacteristic* bootInput = nullptr;
std::atomic<int> state{0}; // 0 advertising, 1 connected, 2 encrypted, -1 auth failed
std::atomic<int> authError{0};
std::atomic<int> connectError{0};
std::atomic<unsigned> failedConnects{0};
std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<unsigned> subscriptions{0}; // report bit 0, boot bit 1
std::atomic<bool> bootMode{false};
std::atomic<bool> suspended{false};
std::atomic<uint32_t> revision{0};
bool resetting = false;
bool initialized = false;
bool sentReport = false;
HubHid::Report previousReport;
esp_reset_reason_t resetReason;

int gapEvent(ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status != 0) {
        connectError.store(event->connect.status);
        ++failedConnects;
        ++revision;
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        portENTER_CRITICAL(&historyMux);
        ++history.disconnects;
        history.reason = event->disconnect.reason;
        portEXIT_CRITICAL(&historyMux);
        Serial.printf("[N2] GAP disconnect=0x%X encrypted=%u\n", event->disconnect.reason,
                      event->disconnect.conn.sec_state.encrypted);
        ++revision;
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE && event->enc_change.status) {
        authError.store(event->enc_change.status);
        state.store(-1);
        Serial.printf("[N2] encryption failed=0x%X\n", event->enc_change.status);
        ++revision;
    }
    return 0;
}
class ServerCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
        connection.store(desc->conn_handle);
        state.store(1);
        subscriptions.store(0);
        bootMode.store(false);
        suspended.store(false);
        authError.store(0);
        connectError.store(0);
        portENTER_CRITICAL(&historyMux);
        ++history.connects;
        portEXIT_CRITICAL(&historyMux);
        Serial.println("[N2] link connected; discovering HID");
        ++revision;
    }
    void onDisconnect(NimBLEServer*) override {
        connection.store(BLE_HS_CONN_HANDLE_NONE);
        state.store(0);
        subscriptions.store(0);
        ++revision;
    }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        state.store(desc->sec_state.encrypted ? 2 : -1);
        Serial.printf("[N2] encrypted=%u bonded=%u\n", desc->sec_state.encrypted, desc->sec_state.bonded);
        ++revision;
    }
} serverCallbacks;
class InputCallbacks final : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* characteristic, ble_gap_conn_desc* desc, uint16_t value) override {
        const unsigned bit = characteristic == reportInput ? 1 : 2;
        if (value & 1) subscriptions.fetch_or(bit);
        else subscriptions.fetch_and(~bit);
        if (desc->sec_state.encrypted) state.store(2);
        Serial.printf("[N2] %s subscription=%u\n", bit == 1 ? "report" : "boot", value);
        ++revision;
    }
    void onRead(NimBLECharacteristic*, ble_gap_conn_desc*) override {
        Serial.println("[N2] input report read");
    }
} inputCallbacks;
class ProtocolCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic) override {
        const auto value = characteristic->getValue();
        if (value.size() == 1 && value[0] <= 1) {
            bootMode.store(value[0] == 0);
            Serial.printf("[N2] protocol=%u\n", value[0]);
            ++revision;
        }
    }
} protocolCallbacks;
class ControlCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic) override {
        const auto value = characteristic->getValue();
        if (value.size() == 1 && value[0] <= 1) {
            suspended.store(value[0] == 0);
            ++revision;
        }
    }
} controlCallbacks;
}
namespace BleKeyboardService {
void begin() {
    if (initialized) return;
    resetReason = esp_reset_reason();
    if (history.magic != DiagnosticMagic || resetReason == ESP_RST_POWERON) {
        history = {DiagnosticMagic, 0, 0, 0};
    }
    NimBLEDevice::init(DeviceName);
    if (!configureIdentity()) {
        Serial.println("[N2] could not configure persistent BLE identity");
        return;
    }
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setMTU(23);
    NimBLEDevice::setCustomGapHandler(gapEvent);
    server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCallbacks, false);
    server->advertiseOnDisconnect(true);
    auto* hid = new NimBLEHIDDevice(server);
    reportInput = hid->inputReport(HubHid::ReportId);
    auto* output = hid->outputReport(HubHid::ReportId);
    // Protocol Mode is advertised, so provide both Boot keyboard characteristics.
    bootInput = hid->hidService()->createCharacteristic(uint16_t(0x2a22),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC);
    auto* bootOutput = hid->hidService()->createCharacteristic(uint16_t(0x2a32),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
        NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);
    const HubHid::Report empty;
    reportInput->setValue(reinterpret_cast<const uint8_t*>(&empty), sizeof(empty));
    bootInput->setValue(reinterpret_cast<const uint8_t*>(&empty), sizeof(empty));
    const uint8_t leds = 0;
    output->setValue(&leds, 1);
    bootOutput->setValue(&leds, 1);
    reportInput->setCallbacks(&inputCallbacks);
    bootInput->setCallbacks(&inputCallbacks);
    hid->protocolMode()->setCallbacks(&protocolCallbacks);
    hid->hidControl()->setCallbacks(&controlCallbacks);
    hid->manufacturer()->setValue("M5Stack");
    hid->pnp(0x02, 0x303a, 0x1001, 0x0100);
    hid->hidInfo(0, 0x02);
    hid->reportMap(const_cast<uint8_t*>(HubHid::ReportMap), sizeof(HubHid::ReportMap));
    hid->setBatteryLevel(100);
    hid->startServices();
    NimBLEAdvertisementData advertisement;
    advertisement.setFlags(0x06);
    advertisement.setAppearance(0x03c1);
    advertisement.setCompleteServices(NimBLEUUID(uint16_t(0x1812)));
    NimBLEAdvertisementData response;
    response.setName(DeviceName);
    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->setAdvertisementData(advertisement);
    advertising->setScanResponseData(response);
    initialized = advertising->start();
    Serial.printf("[N2] advertising=%d reset=%d heap=%u\n", initialized, resetReason, ESP.getFreeHeap());
}
void suspend() {
    if (!initialized) return;
    const uint16_t handle = connection.load();
    if (handle != BLE_HS_CONN_HANDLE_NONE && server) server->disconnect(handle);
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);
    server = nullptr; reportInput = nullptr; bootInput = nullptr;
    initialized = false; resetting = false; sentReport = false;
    connection.store(BLE_HS_CONN_HANDLE_NONE); state.store(0); subscriptions.store(0);
    ++revision;
}
void resume() { begin(); }
void resetPairings() {
    if (!initialized || resetting) return;
    resetting = true;
    const uint16_t handle = connection.load();
    if (handle != BLE_HS_CONN_HANDLE_NONE) server->disconnect(handle);
    ++revision;
}
void update(bool enabled) {
    static bool serialAttached = false;
    // A host can request a snapshot even if its CDC control-line event was missed.
    bool snapshotRequested = false;
    while (Serial.available() > 0) {
        if (Serial.read() == '?') snapshotRequested = true;
    }
    if (!initialized) { serialAttached = false; return; }
    if (snapshotRequested || (Serial && !serialAttached)) {
        Serial.printf("[N2] %s; %s; %s\n", status(), diagnostic(), counters());
        Serial.printf("[N2] address=%s adv=%d peers=%u link_error=0x%X\n",
                      deviceAddress,
                      NimBLEDevice::getAdvertising()->isAdvertising(),
                      server ? server->getConnectedCount() : 0, connectError.load());
        Serial.printf("[Hub] heap=%u min_heap=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
        serialAttached = true;
    } else if (!Serial) serialAttached = false;
    if (resetting && connection.load() == BLE_HS_CONN_HANDLE_NONE) {
        NimBLEDevice::deleteAllBonds();
        resetting = false;
        NimBLEDevice::startAdvertising();
        Serial.println("[N2] saved pairings cleared by Fn+R");
        ++revision;
    }
    if (!connected()) { sentReport = false; return; }
    const auto& keys = M5Cardputer.Keyboard.keysState();
    HubHid::Report report;
    report.modifiers = enabled ? (keys.modifiers | (keys.opt ? 0x08 : 0)) : 0;
    unsigned count = 0;
    for (uint8_t key : keys.hid_keys) {
        const uint8_t mapped = KeyMapping::code(key, keys.fn);
        if (enabled && mapped && count < 6) report.keys[count++] = mapped;
    }
    const bool boot = bootMode.load();
    static bool previousBoot = false;
    if (sentReport && previousBoot == boot && !memcmp(&report, &previousReport, sizeof(report))) return;
    auto* characteristic = boot ? bootInput : reportInput;
    characteristic->setValue(reinterpret_cast<const uint8_t*>(&report), sizeof(report));
    characteristic->notify();
    previousReport = report;
    previousBoot = boot;
    sentReport = true;
}
bool connected() {
    return initialized && !resetting && state.load() == 2 && !suspended.load() &&
           (subscriptions.load() & (bootMode.load() ? 2 : 1));
}
bool dirty() {
    static uint32_t last = 0;
    const uint32_t current = revision.load();
    const bool changed = last != current;
    last = current;
    return changed;
}
const char* status() {
    if (!initialized) return "N2: advertising failed";
    if (resetting) return "N2: clearing pairings";
    if (connected()) return "N2: HID ready";
    if (state.load() == 0) return NimBLEDevice::getAdvertising()->isAdvertising()
        ? "N2: advertising" : "N2: advertising stopped";
    if (state.load() == 1) return "N2: connected / pairing";
    if (state.load() == 2) return suspended.load() ? "N2: suspended" : "N2: encrypted; wait HID";
    return "N2: authentication failed";
}
const char* diagnostic() {
    static char text[40];
    const History record = readHistory();
    snprintf(text, sizeof(text), "Last:0x%X Auth:0x%X Sub:%u", connectError.load() ? connectError.load() : record.reason,
             authError.load(), subscriptions.load());
    return text;
}
const char* counters() {
    static char text[40];
    const History record = readHistory();
    snprintf(text, sizeof(text), "C:%u D:%u F:%u Reset:%d", record.connects,
             record.disconnects, failedConnects.load(), resetReason);
    return text;
}
}
