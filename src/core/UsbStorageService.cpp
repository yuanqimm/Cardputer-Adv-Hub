#include "core/UsbStorageService.h"

#include "core/Storage.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include <SD.h>
#include "USB.h"
#include "USBMSC.h"

#include <algorithm>
#include <cstring>

namespace {
USBMSC msc;
volatile bool hostActiveFlag = false;
bool mscReady = false;
bool observedHostActive = false;
const char* stateText = "USB SD: idle";
constexpr uint16_t SectorSize = 512;

void claimHost() {
    hostActiveFlag = true;
    Storage::suspendAppAccess();
}

int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    claimHost();
    if (!buffer || offset >= SectorSize || bufsize > SectorSize - offset ||
        lba >= SD.numSectors()) return 0;
    uint8_t sector[SectorSize];
    if (!SD.readRAW(sector, lba)) return 0;
    memcpy(static_cast<uint8_t*>(buffer) + 0, sector + offset, bufsize);
    return static_cast<int32_t>(bufsize);
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    claimHost();
    if (!buffer || offset >= SectorSize || bufsize > SectorSize - offset ||
        lba >= SD.numSectors()) return 0;
    uint8_t sector[SectorSize];
    if (offset != 0 || bufsize != SectorSize) {
        if (!SD.readRAW(sector, lba)) return 0;
    }
    if (offset == 0 && bufsize == SectorSize) {
        memcpy(sector, buffer, SectorSize);
    } else {
        memcpy(sector + offset, buffer, bufsize);
    }
    return SD.writeRAW(sector, lba) ? static_cast<int32_t>(bufsize) : 0;
}

bool onStartStop(uint8_t, bool start, bool loadEject) {
    if (start && !loadEject) {
        claimHost();
        stateText = "USB SD: computer connected";
    } else if (loadEject || !start) {
        hostActiveFlag = false;
        stateText = "USB SD: safely ejected";
    }
    return true;
}

void usbEvent(void*, esp_event_base_t, int32_t eventId, void*) {
    if (eventId == ARDUINO_USB_STOPPED_EVENT) {
        // A cable removal may skip SCSI START/STOP with load_eject.
        hostActiveFlag = false;
        stateText = "USB SD: disconnected";
    }
}
}

namespace UsbStorageService {
void begin() {
    USB.onEvent(usbEvent);
    msc.vendorID("M5Stack");
    msc.productID("Cardputer SD");
    msc.productRevision("1.0");
    msc.onStartStop(onStartStop);
    msc.onRead(onRead);
    msc.onWrite(onWrite);

    const bool card = Storage::available() && SD.sectorSize() == SectorSize && SD.numSectors() > 0;
    const uint32_t sectors = card ? static_cast<uint32_t>(std::min<size_t>(SD.numSectors(), 0xFFFFFFFFu)) : 1u;
    mscReady = msc.begin(sectors, SectorSize);
    msc.mediaPresent(card);
    stateText = card ? "USB SD: ready" : "USB SD: no card";
}

void loop() {
    const bool active = hostActiveFlag;
    if (active == observedHostActive) return;
    observedHostActive = active;
    if (active) {
        // Close all open file handles before the computer starts changing FAT.
        MediaPlayer::stop();
        VideoPlayer::stop();
        stateText = "USB SD: computer connected";
    } else {
        Storage::resumeAppAccess();
        Storage::refresh();
        stateText = mscReady ? "USB SD: safely ejected" : "USB SD: no card";
    }
}

bool available() { return mscReady && Storage::available(); }
bool hostActive() { return hostActiveFlag; }
const char* status() { return stateText; }
}
