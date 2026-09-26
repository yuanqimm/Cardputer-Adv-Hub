#include "media/VideoPlayer.h"
#include "media/JpegFrame.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "core/AppSettings.h"
#include "core/FilePath.h"
#include <SD.h>
#include <memory>
namespace {
bool playing = false, changed = false, validFrame = false, mjpeg = false;
uint32_t nextFrame = 0;
constexpr size_t FrameCapacity = 40 * 1024;
std::unique_ptr<uint8_t[]> frame;
size_t frameSize = 0;
File stream;
uint8_t chunk[1024]; size_t chunkPos = 0, chunkSize = 0;
char path[FilePath::MaxPath+1] = {}, name[144] = "No video";
String directPath;
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
    if (!Storage::available() || directPath.isEmpty()) { mark("Select a JPEG/MJPEG file"); return false; }
    directPath.toCharArray(path, sizeof(path));
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
    } else {
        File image = SD.open(path, FILE_READ);
        if (!image || image.isDirectory()) { mark("Cannot open image"); return false; }
        validFrame = true;
    }
    nextFrame = millis() + 1000 / AppSettings::videoFps(); mark("Paused"); return true;
}
}
namespace VideoPlayer {
bool playFile(const char* file) {
    if (!file || !FilePath::valid(file) || (FilePath::kind(file) != FilePath::Kind::Video && FilePath::kind(file) != FilePath::Kind::Image)) {
        mark("Use JPEG or raw MJPEG"); return false;
    }
    directPath = file;
    const bool opened = openCurrent();
    playing = opened && mjpeg;
    if (opened) mark(mjpeg ? "Playing" : "Image");
    return opened;
}
bool begin() { return Storage::available(); }
void stop() { playing = false; stream.close(); frame.reset(); validFrame = false; mark("Stopped"); }
void loop() {
    if (!playing || static_cast<int32_t>(millis() - nextFrame) < 0) return;
    if (mjpeg) readFrame();
    else playing = false;
    nextFrame = millis() + (mjpeg ? 1000 / AppSettings::videoFps() : 1000);
}
bool toggle() {
    if (!validFrame || strcmp(message, "Finished") == 0) { if (!openCurrent()) return false; }
    if (!mjpeg) { mark("Image"); return true; }
    playing = !playing; nextFrame = millis() + 1000 / AppSettings::videoFps(); mark(playing ? "Playing" : "Paused"); return true;
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
