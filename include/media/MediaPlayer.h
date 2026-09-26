#pragma once

#include <Arduino.h>

namespace MediaPlayer {
bool begin();
bool playFile(const char* path);
void loop();
void stop();
bool toggle();
bool isPlaying();
bool dirty();
const char* status();
uint32_t elapsedSeconds();
uint8_t progress();
const char* currentName();
void setVolume(uint8_t percent);
uint8_t volume();
}
