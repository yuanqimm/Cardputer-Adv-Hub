#pragma once

#include <Arduino.h>

namespace VideoPlayer {
bool begin();
void loop();
void stop();
const char* status();
bool toggle();
bool next();
bool previous();
bool isPlaying();
bool dirty();
const char* currentName();
void render();
}
