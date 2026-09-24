#pragma once

#include <Arduino.h>

namespace Storage {
bool begin();
bool available();
uint64_t totalBytes();
uint64_t usedBytes();
void refresh();
// USB mass-storage 使用期间暂停应用层文件访问，避免和电脑同时操作 FAT。
void suspendAppAccess();
void resumeAppAccess();
bool appAccessAllowed();
uint16_t mediaCount(const char* directory);
bool mediaName(const char* directory, uint16_t index, char* output, size_t outputSize);
}
