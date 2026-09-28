#include "app/Recorder.h"

#include "core/Storage.h"
#include "core/Ui.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
constexpr uint32_t SampleRate = 16000;
constexpr size_t ChunkSamples = 240;
constexpr size_t SpectrumBars = 24;
constexpr const char* RecordingDir = "/recordings";

enum class State : uint8_t { Idle, Recording, Paused, Playing, Saved, Error };
State state = State::Idle;
File file;
File playback;
char tempPath[48] = {};
char savedPath[48] = {};
uint32_t dataBytes = 0;
uint32_t startedAt = 0;
uint32_t pausedAt = 0;
uint32_t pausedMs = 0;
uint32_t savedSeconds = 0;
uint32_t playbackDataBytes = 0;
uint32_t playbackOffset = 0;
uint8_t spectrum[SpectrumBars] = {};
bool changed = true;
bool micReady = false;
bool speakerReady = false;
int16_t samples[ChunkSamples] = {};
const char* message = "Press Enter to record";

struct __attribute__((packed)) WavHeader {
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

void mark(const char* text) { message = text; changed = true; }

void closeMic() {
    if (micReady) { M5Cardputer.Mic.end(); micReady = false; }
}

void closeSpeaker() {
    if (speakerReady) { M5Cardputer.Speaker.end(); speakerReady = false; }
}

void updateSpectrum(const int16_t* data, size_t count) {
    for (size_t bar = 0; bar < SpectrumBars; ++bar) {
        const size_t begin = bar * count / SpectrumBars;
        const size_t end = (bar + 1) * count / SpectrumBars;
        int32_t peak = 0;
        for (size_t i = begin; i < end; ++i) {
            const int32_t value = data[i] < 0 ? -static_cast<int32_t>(data[i]) : data[i];
            peak = std::max(peak, value);
        }
        spectrum[bar] = static_cast<uint8_t>(std::min<int32_t>(46, peak / 700));
    }
}

void clearSpectrum() { memset(spectrum, 0, sizeof(spectrum)); }

WavHeader makeHeader(uint32_t bytes) {
    WavHeader header = {{'R','I','F','F'}, 36u + bytes, {'W','A','V','E'}, {'f','m','t',' '},
        16, 1, 1, SampleRate, SampleRate * 2, 2, 16, {'d','a','t','a'}, bytes};
    return header;
}

bool writeHeader(File& target, uint32_t bytes) {
    const WavHeader header = makeHeader(bytes);
    if (!target.seek(0)) return false;
    return target.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
}

bool findPaths() {
    if (!Storage::available() || !Storage::appAccessAllowed()) return false;
    SD.mkdir(RecordingDir);
    for (uint16_t index = 0; index < 1000; ++index) {
        snprintf(tempPath, sizeof(tempPath), "%s/rec%03u.tmp", RecordingDir, index);
        snprintf(savedPath, sizeof(savedPath), "%s/rec%03u.wav", RecordingDir, index);
        if (!SD.exists(tempPath) && !SD.exists(savedPath)) return true;
    }
    return false;
}

void resetPaths() { tempPath[0] = 0; savedPath[0] = 0; }

void discardTemp() {
    if (file) file.close();
    if (tempPath[0]) SD.remove(tempPath);
    dataBytes = 0;
    resetPaths();
}

void stopPlayback() {
    if (playback) playback.close();
    if (speakerReady) M5Cardputer.Speaker.stop();
    closeSpeaker();
    playbackDataBytes = 0;
    playbackOffset = 0;
    state = (savedPath[0] && SD.exists(savedPath)) ? State::Saved : State::Paused;
}

bool startPlayback() {
    const char* path = (savedPath[0] && SD.exists(savedPath)) ? savedPath : tempPath;
    if (!path[0] || !Storage::available() || !Storage::appAccessAllowed()) {
        mark("SD unavailable"); state = State::Error; return false;
    }
    if (state == State::Recording) return false;
    if (state == State::Playing) { stopPlayback(); mark("Playback stopped"); return true; }
    if (state == State::Paused && file) file.flush();
    File source = SD.open(path, FILE_READ);
    if (!source || source.size() <= sizeof(WavHeader)) {
        if (source) source.close(); mark("No recording to play"); state = State::Error; return false;
    }
    WavHeader header = {};
    if (source.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
        source.close(); mark("Invalid WAV file"); state = State::Error; return false;
    }
    source.close();
    playback = SD.open(path, FILE_READ);
    if (!playback || !playback.seek(sizeof(WavHeader))) {
        if (playback) playback.close(); mark("Cannot open recording"); state = State::Error; return false;
    }
    playbackDataBytes = header.dataSize;
    const uint32_t available = static_cast<uint32_t>(playback.size() - sizeof(WavHeader));
    if (!playbackDataBytes || playbackDataBytes > available) playbackDataBytes = available;
    playbackOffset = 0;
    closeMic();
    M5Cardputer.Speaker.begin(); speakerReady = true;
    state = State::Playing;
    mark("Playing");
    return true;
}

bool startRecording() {
    if (!Storage::available() || !Storage::appAccessAllowed()) { mark("SD unavailable"); state = State::Error; return false; }
    MediaPlayer::stop(); VideoPlayer::stop();
    closeSpeaker();
    // M5Cardputer's microphone and speaker share the I2S peripheral.
    M5Cardputer.Speaker.end();
    if (!findPaths()) { mark("No recording name available"); state = State::Error; return false; }
    file = SD.open(tempPath, FILE_WRITE);
    if (!file || !writeHeader(file, 0)) {
        discardTemp(); mark("Cannot create recording"); state = State::Error; return false;
    }
    M5Cardputer.Mic.begin(); micReady = true;
    dataBytes = 0; pausedMs = 0; startedAt = millis(); pausedAt = 0; clearSpectrum();
    state = State::Recording; mark("Recording");
    return true;
}

bool pauseRecording() {
    if (state != State::Recording) return false;
    pausedAt = millis(); state = State::Paused; mark("Paused"); return true;
}

bool resumeRecording() {
    if (state != State::Paused) return false;
    if (!micReady) { M5Cardputer.Mic.begin(); micReady = true; }
    pausedMs += millis() - pausedAt; pausedAt = 0; state = State::Recording; mark("Recording"); return true;
}

bool finishRecording() {
    if (state != State::Recording && state != State::Paused) return false;
    if (state == State::Recording && !pausedAt) {
        pausedMs += 0;
    }
    if (state == State::Paused && pausedAt) pausedMs += millis() - pausedAt;
    closeMic();
    if (file) {
        if (!writeHeader(file, dataBytes)) { file.close(); discardTemp(); mark("Cannot finalize WAV"); state = State::Error; return false; }
        file.flush(); file.close();
    }
    if (!SD.rename(tempPath, savedPath)) { discardTemp(); mark("Cannot save recording"); state = State::Error; return false; }
    savedSeconds = (millis() - startedAt - pausedMs) / 1000;
    tempPath[0] = 0;
    state = State::Saved; mark("Saved to SD"); return true;
}

uint32_t elapsedMs() {
    if (state == State::Idle || state == State::Error || state == State::Saved) return savedSeconds * 1000;
    const uint32_t now = state == State::Paused ? pausedAt : millis();
    return now - startedAt - pausedMs;
}
}

namespace Recorder {
bool begin() {
    MediaPlayer::stop(); VideoPlayer::stop();
    state = State::Idle; message = "Press Enter to record"; changed = true;
    clearSpectrum(); resetPaths(); dataBytes = 0; savedSeconds = 0;
    return Storage::available();
}

void end() {
    if (state == State::Recording || state == State::Paused) finishRecording();
    else if (state == State::Playing) stopPlayback();
    closeMic(); closeSpeaker();
    // MediaPlayer and the SD audio page expect the speaker to be initialized.
    M5Cardputer.Speaker.begin();
    if (file) file.close();
}

void update() {
    if (state == State::Recording && micReady && file) {
        if (M5Cardputer.Mic.record(samples, ChunkSamples, SampleRate, false)) {
            file.write(reinterpret_cast<const uint8_t*>(samples), sizeof(samples));
            dataBytes += sizeof(samples);
            updateSpectrum(samples, ChunkSamples);
            changed = true;
        }
    } else if (state == State::Playing && playback && speakerReady) {
        if (!M5Cardputer.Speaker.isPlaying() && playbackOffset < playbackDataBytes) {
            const size_t bytes = std::min<size_t>(sizeof(samples), playbackDataBytes - playbackOffset);
            const size_t read = playback.read(reinterpret_cast<uint8_t*>(samples), bytes);
            if (read) {
                M5Cardputer.Speaker.playRaw(samples, read / sizeof(int16_t), SampleRate);
                playbackOffset += read; changed = true;
            }
        } else if (playbackOffset >= playbackDataBytes && !M5Cardputer.Speaker.isPlaying()) {
            stopPlayback(); mark("Playback finished");
        }
    }
}

bool onInput(const InputEvent& event) {
    if (event.type != InputType::Key || event.repeat) return false;
    const char key = static_cast<char>(tolower(static_cast<unsigned char>(event.key)));
    if (event.fn && key == 'q') { if (state == State::Recording || state == State::Paused) finishRecording(); return true; }
    if (key == '\b' || key == 27) { if (state == State::Recording || state == State::Paused) finishRecording(); return true; }
    if (event.key == '\n') {
        if (state == State::Idle || state == State::Saved || state == State::Error) return startRecording();
        if (state == State::Paused) return resumeRecording();
    }
    if (key == 'p' || event.key == ' ') {
        if (state == State::Recording) return pauseRecording();
        if (state == State::Playing) { stopPlayback(); mark("Playback stopped"); return true; }
    }
    if (key == 'v' && (state == State::Paused || state == State::Saved)) return startPlayback();
    if (key == 's' && (state == State::Recording || state == State::Paused)) return finishRecording();
    return false;
}

void draw() {
    Ui::header("Recorder");
    const uint32_t seconds = elapsedMs() / 1000;
    char row[40];
    snprintf(row, sizeof(row), "Time %02lu:%02lu", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
    Ui::line(24, row, TFT_CYAN);
    const char* stateName = state == State::Recording ? "Recording" : state == State::Paused ? "Paused" : state == State::Playing ? "Playing" : state == State::Saved ? "Saved" : state == State::Error ? "Error" : "Ready";
    Ui::line(40, stateName, state == State::Error ? TFT_RED : state == State::Recording ? TFT_RED : TFT_GREEN);
    Ui::canvas().drawRect(7, 56, 226, 50, TFT_DARKGREY);
    for (size_t i = 0; i < SpectrumBars; ++i) {
        const int height = spectrum[i];
        Ui::canvas().fillRect(10 + static_cast<int>(i) * 9, 103 - height, 6, height, state == State::Playing ? TFT_CYAN : TFT_GREEN);
    }
    Ui::line(109, message, TFT_YELLOW);
    if (savedPath[0]) { const char* name = strrchr(savedPath, '/'); snprintf(row, sizeof(row), "File: %.28s", name ? name + 1 : savedPath); Ui::line(119, row, TFT_WHITE); }
    Ui::footer("Enter:rec/resume  P:pause  V:listen  S:save");
}

bool dirty() { const bool result = changed; changed = false; return result; }
}
