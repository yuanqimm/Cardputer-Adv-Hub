#pragma once
#include <Arduino.h>
#include <vector>

namespace FileManager {
struct Entry { String name; uint32_t size; bool directory; };
// Lists use bounded memory and expose a continuation marker for large folders.
bool list(const String& directory, const String& afterName = "", bool afterDirectory = false);
const std::vector<Entry>& entries();
bool more();
void releaseList();
const char* status();
bool mkdir(const String& directory, const String& name);
bool createText(const String& directory, const String& name);
bool rename(const String& path, const String& newName);
bool remove(const String& path); // files and empty folders only
bool move(const String& source, const String& destination);
bool readText(const String& path, String& text);
bool saveText(const String& path, const String& text);
bool copy(const String& source, const String& destination);
void loop();
void cancel();
bool busy();
bool dirty();
uint8_t progress();
constexpr size_t MaxText = 4096;
}
