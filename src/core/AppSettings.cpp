#include "core/AppSettings.h"
#include <M5Cardputer.h>
#include <Preferences.h>
namespace {
uint8_t light = 160, sound = 60, fps = 12;
bool pending = false;
uint32_t changedAt = 0;
void changed() { pending = true; changedAt = millis(); }
}
namespace AppSettings {
void begin() {
    Preferences prefs;
    if (prefs.begin("hub-settings", true)) {
        light = constrain(prefs.getUChar("brightness", 160), 16, 255);
        sound = constrain(prefs.getUChar("volume", 60), 0, 100);
        fps = constrain(prefs.getUChar("video-fps", 12), 1, 20);
        prefs.end();
    }
    M5Cardputer.Display.setBrightness(light);
}
void loop() {
    if (!pending || millis() - changedAt < 1500) return;
    Preferences prefs;
    if (prefs.begin("hub-settings", false)) {
        const bool ok = prefs.putUChar("brightness", light) == 1 && prefs.putUChar("volume", sound) == 1 && prefs.putUChar("video-fps", fps) == 1;
        prefs.end(); if (ok) pending = false; else changedAt = millis();
    } else changedAt = millis();
}
uint8_t brightness() { return light; }
uint8_t volume() { return sound; }
uint8_t videoFps() { return fps; }
void setBrightness(int v) { light = constrain(v, 16, 255); M5Cardputer.Display.setBrightness(light); changed(); }
void setVolume(int v) { sound = constrain(v, 0, 100); changed(); }
void setVideoFps(int v) { fps = constrain(v, 1, 20); changed(); }
}
