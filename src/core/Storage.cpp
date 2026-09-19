#include "core/Storage.h"
#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <vector>
namespace {
bool initialized = false;
std::vector<String> music, videos;
constexpr size_t MaxFiles = 128;
std::vector<String>& list(const char* directory) { return strcmp(directory, "/music") == 0 ? music : videos; }
void scan(const char* directory) {
    auto& files = list(directory); files.clear();
    File root = SD.open(directory);
    if (!root || !root.isDirectory()) return;
    for (File file = root.openNextFile(); file; file = root.openNextFile()) {
        if (file.isDirectory()) { file.close(); continue; }
        String name = file.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        String lower = name; lower.toLowerCase();
        const bool audio = strcmp(directory, "/music") == 0;
        const bool supported = audio ? (lower.endsWith(".mp3") || lower.endsWith(".wav")) :
            (lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".mjpeg") || lower.endsWith(".mjpg"));
        if (!name.startsWith(".") && supported && name.length() <= 140 && files.size() < MaxFiles)
            files.push_back(String(directory) + "/" + name);
        file.close();
    }
    std::sort(files.begin(), files.end(), [](const String& a, const String& b) { return a.compareTo(b) < 0; });
}
}
namespace Storage {
bool begin() {
    SPI.begin(40, 39, 14, 12);
    initialized = SD.begin(12, SPI, 25000000);
    if (available()) {
        SD.mkdir("/music"); SD.mkdir("/video"); SD.mkdir("/config"); refresh();
    }
    return available();
}
bool available() { return initialized && SD.cardType() != CARD_NONE; }
void refresh() { if (available()) { scan("/music"); scan("/video"); } }
uint64_t totalBytes() { return available() ? SD.cardSize() : 0; }
uint64_t usedBytes() { return available() ? SD.usedBytes() : 0; }
uint16_t mediaCount(const char* directory) { return available() ? list(directory).size() : 0; }
bool mediaName(const char* directory, uint16_t index, char* output, size_t size) {
    if (!output || !size) return false;
    output[0] = 0;
    const auto& files = list(directory);
    if (!available() || index >= files.size() || files[index].length() >= size) return false;
    files[index].toCharArray(output, size); return true;
}
}
