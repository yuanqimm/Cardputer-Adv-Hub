#pragma once
#include <Arduino.h>
namespace AppSettings {
void begin();
void loop();
uint8_t brightness();
uint8_t volume();
uint8_t videoFps();
void setBrightness(int value);
void setVolume(int value);
void setVideoFps(int value);
}
