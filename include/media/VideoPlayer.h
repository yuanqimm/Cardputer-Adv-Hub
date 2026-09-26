#pragma once

#include <Arduino.h>

namespace VideoPlayer {
bool begin();
bool playFile(const char* path);
void loop();
void stop();
const char* status();
bool toggle();
bool isPlaying();
bool dirty();
const char* currentName();
void render();
}
