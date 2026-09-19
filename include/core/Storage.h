#pragma once

#include <Arduino.h>

namespace Storage {
bool begin();
bool available();
uint64_t totalBytes();
uint64_t usedBytes();
void refresh();
uint16_t mediaCount(const char* directory);
bool mediaName(const char* directory, uint16_t index, char* output, size_t outputSize);
}
