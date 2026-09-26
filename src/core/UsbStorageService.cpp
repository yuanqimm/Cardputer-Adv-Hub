#include "core/UsbStorageService.h"
#include "core/Storage.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include <SD.h>
#include "USB.h"
#include "USBMSC.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <algorithm>
#include <cstring>

extern USBMSC usbStorageMsc;
namespace {
SemaphoreHandle_t ioMutex = nullptr;
std::atomic<bool> releaseRequested{false};
// Protected by ioMutex; shared and UI state belong to the Arduino loop.
bool ioEnabled = false;
bool shared = false, mscReady = false, changed = false;
uint32_t cardSectorCount = 0;
constexpr uint16_t SectorSize = 512;
const char* stateText = "USB SD: off";
class IoLock {
public:
    IoLock() { xSemaphoreTake(ioMutex, portMAX_DELAY); }
    ~IoLock() { xSemaphoreGive(ioMutex); }
};
int32_t transfer(uint32_t lba, uint32_t offset, void* buffer, uint32_t size, bool write) {
    if (!ioMutex || !buffer) return -1;
    IoLock lock;
    const uint64_t address = uint64_t(lba) * SectorSize + offset;
    if (!ioEnabled || releaseRequested.load() || address + size > uint64_t(cardSectorCount) * SectorSize) return -1;
    auto* bytes = static_cast<uint8_t*>(buffer);
    uint8_t sector[SectorSize];
    uint32_t block = address / SectorSize;
    uint32_t within = address % SectorSize;
    for (uint32_t done = 0; done < size; ++block) {
        const uint32_t count = std::min<uint32_t>(size - done, SectorSize - within);
        if (!write || within || count != SectorSize) {
            if (!SD.readRAW(sector, block)) return -1;
        }
        if (write) {
            memcpy(sector + within, bytes + done, count);
            if (!SD.writeRAW(sector, block)) return -1;
        } else memcpy(bytes + done, sector + within, count);
        done += count;
        within = 0;
    }
    return size;
}
int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t size) {
    return transfer(lba, offset, buffer, size, false);
}
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t size) {
    return transfer(lba, offset, buffer, size, true);
}
bool onStartStop(uint8_t, bool start, bool loadEject) {
    // Host start/load requests cannot enable sharing without user input.
    if (loadEject && !start) {
        releaseRequested.store(true);
        usbStorageMsc.mediaPresent(false);
    }
    return true;
}
void usbEvent(void*, esp_event_base_t, int32_t eventId, void*) {
    if (eventId == ARDUINO_USB_STOPPED_EVENT) releaseRequested.store(true);
}
}
namespace UsbStorageService {
void begin() {
    ioMutex = xSemaphoreCreateMutex();
    USB.onEvent(usbEvent);
    usbStorageMsc.vendorID("M5Stack");
    usbStorageMsc.productID("Cardputer SD");
    usbStorageMsc.productRevision("1.0");
    usbStorageMsc.onStartStop(onStartStop);
    usbStorageMsc.onRead(onRead);
    usbStorageMsc.onWrite(onWrite);
    mscReady = ioMutex && usbStorageMsc.begin(1, SectorSize);
    // Keep the interface registered, but report no medium until selected.
    usbStorageMsc.mediaPresent(false);
    if (!mscReady) stateText = "USB SD: init failed";
}
bool setEnabled(bool enabled) {
    if (enabled == shared) return true;
    changed = true;
    if (enabled) {
        if (!mscReady || !Storage::available() || SD.sectorSize() != SectorSize || !SD.numSectors()) {
            stateText = "USB SD: card unavailable";
            return false;
        }
        // All application SD access runs on the Arduino loop. Close files before
        // allowing the USB task to access raw sectors.
        MediaPlayer::stop();
        VideoPlayer::stop();
        Storage::suspendAppAccess();
        IoLock lock;
        cardSectorCount = SD.numSectors();
        usbStorageMsc.begin(cardSectorCount, SectorSize);
        releaseRequested.store(false);
        ioEnabled = shared = true;
        usbStorageMsc.mediaPresent(true);
        stateText = "USB SD: sharing";
    } else {
        // Drain in-flight I/O before invalidating the device-side FAT cache.
        {
            IoLock lock;
            ioEnabled = false;
            usbStorageMsc.mediaPresent(false);
        }
        const bool mounted = Storage::remount();
        shared = false;
        stateText = mounted ? "USB SD: off" : "USB SD: remount failed";
    }
    return true;
}
void loop() {
    if (releaseRequested.exchange(false) && shared) setEnabled(false);
}
bool available() { return mscReady && Storage::available(); }
bool hostActive() { return shared; }
bool dirty() { const bool result = changed; changed = false; return result; }
const char* status() { return stateText; }
}
