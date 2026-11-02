#pragma once

#include <M5Cardputer.h>

namespace Ui {
// Approved A theme: graphite, silver and solid orange focus cards.
enum class Icon { None, Terminal, Keyboard, Infrared, Storage, Recorder, Settings, Folder, File, Audio, Image, Usb };
uint16_t background();
uint16_t surface();
uint16_t surfaceAlt();
uint16_t selected();
uint16_t onSelected();
uint16_t accent();
uint16_t accentSoft();
uint16_t text();
uint16_t muted();
uint16_t info();
uint16_t success();
uint16_t warning();
uint16_t danger();
void begin();
lgfx::LGFX_Sprite& canvas();
void present();
void clear(uint16_t color = 0);
void header(const char* title, Icon mark = Icon::None, bool highlighted = false);
void footer(const char* text);
void item(int index, const char* label, bool selected);
void line(int y, const char* text, uint16_t color = TFT_WHITE);
void panel(int x, int y, int width, int height, uint16_t color = 0);
void divider(int y, uint16_t color = 0);
// Fixed-width labels use ellipsis; they never wrap into adjacent controls.
void label(int x, int y, int width, const char* value, uint16_t foreground,
           uint16_t background, uint8_t scale = 1);
void card(int x, int y, int width, int height, bool active = false);
void icon(Icon mark, int x, int y, uint16_t color);
void progress(int x, int y, int width, int percent, uint16_t color = 0);
void scrollBar(int x, int y, int height, size_t first, size_t visible, size_t total);
}
