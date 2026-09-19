#include "core/Ui.h"

namespace Ui {
namespace {
lgfx::LGFX_Sprite frame(&M5Cardputer.Display);
bool available = false;
bool presented = false;
uint32_t lastHash = 0;
}

void begin() {
    // Allocate before BLE starts, while a contiguous buffer is still available.
    frame.setColorDepth(8);
    available = frame.createSprite(240, 135) != nullptr;
    Serial.printf("[UI] framebuffer: %s (32400 bytes)\n", available ? "ready" : "allocation failed");
    if (!available) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.drawString("UI memory allocation failed", 6, 30);
    }
    frame.setTextDatum(TL_DATUM);
    frame.setTextFont(1);
    frame.setTextSize(1);
}

lgfx::LGFX_Sprite& canvas() { return frame; }

void present() {
    if (!available) return;
    // An ordinary HID key does not change the page: avoid an identical transfer.
    const auto* pixels = static_cast<const uint8_t*>(frame.getBuffer());
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < 240u * 135u; ++i) hash = (hash ^ pixels[i]) * 16777619u;
    if (!presented || hash != lastHash) {
        frame.pushSprite(0, 0);
        lastHash = hash;
        presented = true;
    }
}

void clear(uint16_t color) { frame.fillScreen(color); }

void header(const char* title) {
    frame.fillRect(0, 0, 240, 18, TFT_DARKGREY);
    frame.setTextColor(TFT_WHITE, TFT_DARKGREY);
    frame.drawString(title, 6, 5);
}

void footer(const char* text) {
    frame.fillRect(0, 122, 240, 13, TFT_DARKGREY);
    frame.setTextColor(TFT_WHITE, TFT_DARKGREY);
    frame.drawString(text, 6, 124);
}

void item(int index, const char* label, bool selected) {
    const int y = 25 + index * 18;
    const uint16_t bg = selected ? TFT_BLUE : TFT_BLACK;
    frame.fillRect(4, y - 1, 232, 16, bg);
    frame.setTextColor(TFT_WHITE, bg);
    frame.drawString(label, 10, y + 2);
}

void line(int y, const char* text, uint16_t color) {
    frame.setTextColor(color, TFT_BLACK);
    frame.drawString(text, 8, y);
}
}
