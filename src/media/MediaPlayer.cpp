#include "media/MediaPlayer.h"
#include "core/Storage.h"
#include "core/AppSettings.h"
#include "media/AudioOutputM5Speaker.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <cstring>
namespace {
AudioFileSourceFS* source = nullptr;
AudioGenerator* decoder = nullptr;
AudioOutputM5Speaker output(&M5Cardputer.Speaker, 0);
bool playing = false, changed = false;
uint16_t selected = 0;
uint32_t frameStart = 0, lastSecond = 0;
char name[144] = "No track";
const char* message = "Stopped";
void mark(const char* text) { message = text; changed = true; }
void release() {
    if (decoder) { decoder->stop(); delete decoder; decoder = nullptr; }
    output.stop();
    if (source) { source->close(); delete source; source = nullptr; }
    playing = false;
}
bool start() {
    release();
    const uint16_t count = Storage::mediaCount("/music");
    if (!count) { mark("No MP3/WAV files"); return false; }
    selected %= count;
    char path[160];
    if (!Storage::mediaName("/music", selected, path, sizeof(path))) { mark("File unavailable"); return false; }
    snprintf(name, sizeof(name), "%s", strrchr(path, '/') + 1);
    // Leave headroom for the BLE host, filesystem, decoder and display.
    if (ESP.getFreeHeap() < 55000) { mark("Not enough free memory"); return false; }
    source = new AudioFileSourceFS(SD);
    if (!source || !source->open(path)) { release(); mark("Cannot open audio"); return false; }
    String lower(path); lower.toLowerCase();
    decoder = lower.endsWith(".wav") ? static_cast<AudioGenerator*>(new AudioGeneratorWAV()) : new AudioGeneratorMP3();
    output.SetGain(AppSettings::volume() / 100.0f);
    frameStart = output.getFrames(); lastSecond = 0;
    if (!decoder || !decoder->begin(source, &output)) { release(); mark("Unsupported/bad audio"); return false; }
    playing = true; mark("Playing"); return true;
}
}
namespace MediaPlayer {
bool begin() { M5Cardputer.Speaker.begin(); output.setup(); output.SetGain(AppSettings::volume()/100.0f); return Storage::available(); }
void stop() { release(); mark("Stopped"); }
void loop() {
    if (!playing || !decoder) return;
    if (!decoder->loop()) {
        const bool eof = source && source->getPos() >= source->getSize();
        release();
        if (eof && Storage::mediaCount("/music") > 1) { ++selected; start(); }
        else mark(eof ? "Finished" : "Audio decode error");
    }
    const uint32_t second = elapsedSeconds();
    if (second != lastSecond) { lastSecond = second; changed = true; }
}
bool toggle() {
    if (!decoder) return start();
    playing = !playing;
    if (!playing) output.stop();
    mark(playing ? "Playing" : "Paused"); return true;
}
bool next() { ++selected; return start(); }
bool previous() {
    const uint16_t count = Storage::mediaCount("/music");
    if (!count) return false;
    selected = (selected + count - 1) % count; return start();
}
bool isPlaying() { return playing; }
bool dirty() { const bool result = changed; changed = false; return result; }
const char* currentName() { return name; }
const char* status() { return message; }
uint16_t index() { return selected; }
uint32_t elapsedSeconds() { return output.getRate() ? (output.getFrames() - frameStart) / output.getRate() : 0; }
uint8_t progress() { return source && source->getSize() ? static_cast<uint64_t>(source->getPos()) * 100 / source->getSize() : 0; }
void setVolume(uint8_t value) { AppSettings::setVolume(value); output.SetGain(AppSettings::volume()/100.0f); changed = true; }
uint8_t volume() { return AppSettings::volume(); }
}
