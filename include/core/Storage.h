#pragma once

#include <Arduino.h>

namespace Storage {
bool begin();
bool available();
uint64_t totalBytes();
uint64_t usedBytes();
void refresh();
bool remount();
// USB mass-storage 使用期间暂停应用层文件访问，避免和电脑同时操作 FAT。
void suspendAppAccess();
// Resume via remount() after USB ownership ends, to discard cached FAT data.
bool appAccessAllowed();
uint16_t mediaCount(const char* directory);
bool mediaName(const char* directory, uint16_t index, char* output, size_t outputSize);
}
