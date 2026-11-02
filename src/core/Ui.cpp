#include "core/Ui.h"
#include <WiFi.h>
#include <algorithm>
#include <cstring>

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

uint16_t background() { static const uint16_t value = rgb565(24, 27, 25); return value; }
uint16_t surface() { static const uint16_t value = rgb565(38, 43, 40); return value; }
uint16_t surfaceAlt() { static const uint16_t value = rgb565(69, 77, 72); return value; }
uint16_t selected() { return accent(); }
uint16_t onSelected() { return background(); }
uint16_t accent() { static const uint16_t value = rgb565(245, 124, 0); return value; }
uint16_t accentSoft() { static const uint16_t value = rgb565(175, 91, 16); return value; }
uint16_t text() { static const uint16_t value = rgb565(237, 241, 242); return value; }
uint16_t muted() { static const uint16_t value = rgb565(168, 176, 182); return value; }
uint16_t info() { return text(); }
uint16_t success() { static const uint16_t value = rgb565(128, 185, 148); return value; }
uint16_t warning() { static const uint16_t value = rgb565(255, 180, 91); return value; }
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
    frame.setTextWrap(false);
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

void header(const char* title, Icon mark, bool highlighted) {
    const uint16_t bg = highlighted ? selected() : surface();
    const uint16_t fg = highlighted ? onSelected() : text();
    frame.fillRect(0, 0, 240, 18, bg);
    frame.fillRect(0, 17, 240, 1, surfaceAlt());
    if (mark != Icon::None) icon(mark, 5, 1, highlighted ? fg : accent());
    label(mark == Icon::None ? 6 : 25, 5, mark == Icon::None ? 202 : 183, title, fg, bg);
    const bool connected = WiFi.status() == WL_CONNECTED;
    const uint16_t color = highlighted ? fg : connected ? text() : muted();
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
    frame.fillRect(0, 121, 240, 1, surfaceAlt());
    frame.fillRect(0, 122, 240, 13, surface());
    label(6, 124, 228, text, Ui::text(), surface());
}

void item(int index, const char* label, bool selected) {
    const int y = 25 + index * 18;
    const uint16_t bg = selected ? Ui::selected() : Ui::surface();
    frame.fillRoundRect(4, y - 1, 232, 16, 3, bg);
    if (selected) frame.drawRoundRect(4, y - 1, 232, 16, 3, Ui::accent());
    Ui::label(10, y + 2, 220, label, selected ? onSelected() : text(), bg);
}

void line(int y, const char* text, uint16_t color) {
    label(8, y, 224, text, color == TFT_WHITE ? Ui::text() : color, Ui::background());
}

void panel(int x, int y, int width, int height, uint16_t color) {
    frame.fillRoundRect(x, y, width, height, 4, color == 0 ? surface() : color);
}

void divider(int y, uint16_t color) { frame.fillRect(6, y, 228, 1, color == 0 ? surfaceAlt() : color); }

void label(int x, int y, int width, const char* value, uint16_t fg, uint16_t bg, uint8_t scale) {
    if (!value || width <= 0) return;
    frame.setTextSize(scale);
    frame.setTextColor(fg, bg);
    if (frame.textWidth(value) <= width) frame.drawString(value, x, y);
    else {
        char clipped[96];
        size_t length = std::min(strlen(value), sizeof(clipped)-4);
        memcpy(clipped, value, length);
        // Back up to a complete UTF-8 codepoint, even when the buffer filled.
        while (length && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) --length;
        for (;;) {
            memcpy(clipped+length, "...", 4);
            if (frame.textWidth(clipped) <= width) { frame.drawString(clipped,x,y); break; }
            if (!length) break;
            --length;
            while (length && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) --length;
        }
    }
    frame.setTextSize(1);
}

void card(int x, int y, int width, int height, bool active) {
    frame.fillRoundRect(x,y,width,height,3,active ? selected() : surface());
}

void progress(int x, int y, int width, int percent, uint16_t color) {
    percent = std::max(0,std::min(100,percent));
    frame.fillRoundRect(x,y,width,4,2,surfaceAlt());
    const int filled = width*percent/100;
    if (filled) frame.fillRect(x,y,filled,4,color ? color : accent());
}

void scrollBar(int x, int y, int height, size_t first, size_t visible, size_t total) {
    if (total <= visible || !visible) return;
    frame.fillRect(x,y,2,height,surfaceAlt());
    const int thumb = std::max(5,static_cast<int>(height*visible/total));
    const int offset = (height-thumb)*std::min(first,total-visible)/(total-visible);
    frame.fillRect(x,y+offset,2,thumb,muted());
}

// Original 16x16 line icons, drawn directly; no image assets or extra buffers.
void icon(Icon mark, int x, int y, uint16_t color) {
    const auto line = [&](int a,int b,int c,int d) { frame.drawLine(x+a,y+b,x+c,y+d,color); };
    const auto rect = [&](int a,int b,int w,int h) { frame.drawRect(x+a,y+b,w,h,color); };
    switch (mark) {
    case Icon::Terminal:
        frame.drawRoundRect(x,y+2,16,12,2,color);
        line(3,5,6,8); line(6,8,3,11); line(8,11,12,11); break;
    case Icon::Keyboard:
        frame.drawRoundRect(x,y+3,16,10,2,color);
        for(int row=0;row<2;++row) for(int col=0;col<4;++col) frame.fillRect(x+2+col*3,y+5+row*3,2,1,color);
        line(5,11,11,11); break;
    case Icon::Infrared:
        frame.drawRoundRect(x+1,y+6,7,10,2,color);
        frame.fillCircle(x+4,y+9,1,color); line(3,12,5,12);
        line(8,4,11,7); line(10,1,15,6); break;
    case Icon::Storage:
        line(3,0,13,0); line(13,0,15,2); line(15,2,15,15); line(15,15,1,15); line(1,15,1,2); line(1,2,3,0);
        for(int i=0;i<3;++i) rect(4+i*3,2,2,5); rect(4,10,8,4); break;
    case Icon::Recorder:
        frame.drawRoundRect(x+5,y,6,10,3,color);
        line(2,7,2,10); line(2,10,5,13); line(5,13,11,13); line(11,13,14,10); line(14,10,14,7);
        line(8,13,8,15); line(5,15,11,15); break;
    case Icon::Settings:
        frame.drawCircle(x+8,y+8,5,color); frame.drawCircle(x+8,y+8,2,color);
        line(8,0,8,3); line(8,13,8,15); line(0,8,3,8); line(13,8,15,8);
        line(2,2,4,4); line(12,12,14,14); line(2,14,4,12); line(12,4,14,2); break;
    case Icon::Folder:
        line(1,3,6,3); line(6,3,8,5); line(8,5,15,5); line(15,5,15,13); line(15,13,1,13); line(1,13,1,3); break;
    case Icon::File:
        rect(3,1,11,14); line(6,5,11,5); line(6,8,11,8); line(6,11,10,11); break;
    case Icon::Audio:
        line(6,2,6,11); line(6,2,14,0); line(14,0,14,9); line(6,5,14,3);
        frame.fillCircle(x+3,y+12,3,color); frame.fillCircle(x+11,y+10,3,color); break;
    case Icon::Image:
        rect(0,2,16,12); frame.fillCircle(x+4,y+5,1,color);
        line(2,12,7,7); line(7,7,10,10); line(10,10,12,8); line(12,8,14,12); break;
    case Icon::Usb:
        line(8,0,8,15); line(5,3,8,0); line(8,0,11,3);
        line(8,11,3,7); line(3,7,3,4); line(8,9,13,5); rect(12,2,3,3);
        frame.fillCircle(x+3,y+3,1,color); break;
    default: break;
    }
}
}
