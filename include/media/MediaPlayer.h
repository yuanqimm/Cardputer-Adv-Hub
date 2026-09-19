#pragma once

#include <Arduino.h>

namespace MediaPlayer {
bool begin();
void loop();
void stop();
bool toggle();
bool next();
bool previous();
bool isPlaying();
bool dirty();
const char* status();
uint16_t index();
uint32_t elapsedSeconds();
uint8_t progress();
const char* currentName();
void setVolume(uint8_t percent);
uint8_t volume();
}
