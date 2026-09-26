#include "core/Storage.h"
#include "core/FileManager.h"
#include <SD.h>
#include <SPI.h>
namespace {
bool initialized = false;
bool appAccess = true;
}
namespace Storage {
bool begin() {
    SPI.begin(40, 39, 14, 12);
    initialized = SD.begin(12, SPI, 25000000);
    appAccess = true;
    if (available()) {
        SD.mkdir("/music"); SD.mkdir("/video"); SD.mkdir("/config");
    }
    return available();
}
bool available() { return initialized && SD.cardType() != CARD_NONE; }
bool remount() {
    SD.end();
    FileManager::releaseList();
    initialized = SD.begin(12, SPI, 25000000);
    appAccess = true;
    return available();
}
void suspendAppAccess() { FileManager::cancel(); FileManager::releaseList(); appAccess = false; }
bool appAccessAllowed() { return appAccess; }
uint64_t totalBytes() { return available() ? SD.cardSize() : 0; }
uint64_t usedBytes() { return available() && appAccess ? SD.usedBytes() : 0; }
}
