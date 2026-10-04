#include "core/Ui.h"
#include <WiFi.h>

namespace Ui {
namespace {
lgfx::LGFX_Sprite frame(&M5Cardputer.Display);
bool available = false;
bool presented = false;
uint32_t lastHash = 0;
}

uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<uint16_t>(((red & 0xF8u) << 8) | ((green & 0xFCu) << 3) | (blue >> 3));
}

uint16_t background() { static const uint16_t value = rgb565(17, 21, 28); return value; }
uint16_t surface() { static const uint16_t value = rgb565(28, 36, 48); return value; }
uint16_t surfaceAlt() { static const uint16_t value = rgb565(38, 49, 64); return value; }
uint16_t selected() { static const uint16_t value = rgb565(80, 50, 31); return value; }
uint16_t accent() { static const uint16_t value = rgb565(255, 138, 31); return value; }
uint16_t accentSoft() { static const uint16_t value = rgb565(200, 99, 36); return value; }
uint16_t text() { static const uint16_t value = rgb565(242, 236, 226); return value; }
uint16_t muted() { static const uint16_t value = rgb565(138, 150, 163); return value; }
uint16_t info() { static const uint16_t value = rgb565(120, 217, 232); return value; }
uint16_t success() { static const uint16_t value = rgb565(121, 217, 154); return value; }
uint16_t warning() { static const uint16_t value = rgb565(255, 195, 89); return value; }
uint16_t danger() { static const uint16_t value = rgb565(237, 106, 98); return value; }

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

void clear(uint16_t color) { frame.fillScreen(color == 0 ? background() : color); }

void header(const char* title) {
    frame.fillRect(0, 0, 240, 18, surface());
    frame.fillRect(0, 17, 240, 1, accentSoft());
    frame.setTextColor(text(), surface());
    frame.drawString(title, 6, 5);
    const bool connected = WiFi.status() == WL_CONNECTED;
    const uint16_t color = connected ? success() : muted();
    frame.setColor(color);
    if (connected) {
        frame.fillRect(216, 13, 3, 3);
        frame.fillRect(221, 9, 3, 7);
        frame.fillRect(226, 5, 3, 11);
        frame.fillCircle(233, 15, 1, color);
    } else {
        frame.drawLine(216, 5, 230, 16);
        frame.drawLine(230, 5, 216, 16);
    }
}

void footer(const char* text) {
    frame.fillRect(0, 121, 240, 1, accentSoft());
    frame.fillRect(0, 122, 240, 13, surface());
    frame.setTextColor(muted(), surface());
    frame.drawString(text, 6, 124);
}

void item(int index, const char* label, bool selected) {
    const int y = 25 + index * 18;
    const uint16_t bg = selected ? Ui::selected() : Ui::surface();
    frame.fillRoundRect(4, y - 1, 232, 16, 3, bg);
    if (selected) frame.drawRoundRect(4, y - 1, 232, 16, 3, Ui::accent());
    frame.setTextColor(Ui::text(), bg);
    frame.drawString(label, 10, y + 2);
}

void line(int y, const char* text, uint16_t color) {
    frame.setTextColor(color == TFT_WHITE ? Ui::text() : color, Ui::background());
    frame.drawString(text, 8, y);
}

void panel(int x, int y, int width, int height, uint16_t color) {
    frame.fillRoundRect(x, y, width, height, 4, color == 0 ? surface() : color);
}

void divider(int y, uint16_t color) { frame.fillRect(6, y, 228, 1, color == 0 ? accentSoft() : color); }
}
