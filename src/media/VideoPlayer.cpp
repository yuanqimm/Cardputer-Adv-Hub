#include "media/VideoPlayer.h"
#include "media/JpegFrame.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "core/AppSettings.h"
#include <SD.h>
#include <memory>
namespace {
bool playing = false, changed = false, validFrame = false, mjpeg = false;
uint16_t selected = 0;
uint32_t nextFrame = 0;
constexpr size_t FrameCapacity = 40 * 1024;
std::unique_ptr<uint8_t[]> frame;
size_t frameSize = 0;
File stream;
uint8_t chunk[1024]; size_t chunkPos = 0, chunkSize = 0;
char path[160] = {}, name[144] = "No video";
const char* message = "Stopped";
void mark(const char* text) { message = text; changed = true; }
int readByte() {
    if (chunkPos == chunkSize) { chunkSize = stream.read(chunk, sizeof(chunk)); chunkPos = 0; }
    return chunkSize ? chunk[chunkPos++] : -1;
}
bool readFrame() {
    JpegFrame parser(frame.get(), FrameCapacity);
    for (size_t work = 0; work < FrameCapacity * 2; ++work) {
        const int byte = readByte();
        if (byte < 0) {
            playing = false;
            if (parser.size()) { validFrame = false; mark("Truncated MJPEG frame"); }
            else mark("Finished");
            return false;
        }
        const auto result = parser.push(byte);
        if (result == JpegFrame::Result::TooLarge) { validFrame = false; playing = false; mark("Frame >40KB: recompress"); return false; }
        if (result == JpegFrame::Result::Complete) { frameSize = parser.size(); validFrame = true; changed = true; return true; }
    }
    validFrame = false; playing = false; mark("Invalid raw MJPEG stream"); return false;
}
bool openCurrent() {
    stream.close(); frame.reset(); validFrame = false; playing = false;
    chunkSize = chunkPos = 0;
    if (!Storage::appAccessAllowed()) { mark("USB computer is using SD"); return false; }
    const uint16_t count = Storage::mediaCount("/video");
    if (!count) { mark("No JPEG/MJPEG files"); return false; }
    selected %= count;
    if (!Storage::mediaName("/video", selected, path, sizeof(path))) { mark("File unavailable"); return false; }
    snprintf(name, sizeof(name), "%s", strrchr(path, '/') + 1);
    String lower(path); lower.toLowerCase();
    mjpeg = lower.endsWith(".mjpeg") || lower.endsWith(".mjpg");
    if (mjpeg) {
        if (ESP.getFreeHeap() < FrameCapacity + 35000) { mark("Not enough free memory"); return false; }
        frame.reset(new (std::nothrow) uint8_t[FrameCapacity]);
        if (!frame) { mark("Video buffer unavailable"); return false; }
        stream = SD.open(path, FILE_READ);
        if (!stream) { frame.reset(); mark("Cannot open video"); return false; }
        if (!readFrame()) return false;
    } else validFrame = true;
    nextFrame = millis() + 1000 / AppSettings::videoFps(); mark("Paused"); return true;
}
}
namespace VideoPlayer {
bool begin() { return Storage::available(); }
void stop() { playing = false; stream.close(); frame.reset(); validFrame = false; mark("Stopped"); }
void loop() {
    if (!playing || static_cast<int32_t>(millis() - nextFrame) < 0) return;
    if (mjpeg) readFrame();
    else { ++selected; const bool ok = openCurrent(); playing = ok; if (ok) mark("Playing"); }
    nextFrame = millis() + (mjpeg ? 1000 / AppSettings::videoFps() : 1000);
}
bool toggle() {
    if (!validFrame || strcmp(message, "Finished") == 0) { if (!openCurrent()) return false; }
    playing = !playing; nextFrame = millis() + 1000 / AppSettings::videoFps(); mark(playing ? "Playing" : "Paused"); return true;
}
bool next() { ++selected; const bool ok = openCurrent(); playing = ok; if (ok) mark("Playing"); return ok; }
bool previous() {
    const uint16_t count = Storage::mediaCount("/video"); if (!count) return false;
    selected = (selected + count - 1) % count;
    const bool ok = openCurrent(); playing = ok; if (ok) mark("Playing"); return ok;
}
bool isPlaying() { return playing; }
bool dirty() { const bool result = changed; changed = false; return result; }
const char* currentName() { return name; }
const char* status() { return message; }
void render() {
    if (!Storage::appAccessAllowed()) { Ui::line(54, "USB computer is using SD", TFT_YELLOW); return; }
    if (!validFrame) { Ui::line(54, message, TFT_YELLOW); return; }
    if (mjpeg) Ui::canvas().drawJpg(frame.get(), frameSize, 0, 22, 240, 96);
    else {
        File image = SD.open(path, FILE_READ);
        if (image) Ui::canvas().drawJpg(&image, 0, 22, 240, 96);
        else { validFrame = false; playing = false; mark("Cannot read image"); Ui::line(54, message, TFT_YELLOW); }
    }
}
}
