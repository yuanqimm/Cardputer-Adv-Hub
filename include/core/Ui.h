#pragma once

#include <M5Cardputer.h>

namespace Ui {
// Shared visual language: charcoal surfaces with the warm orange and cool cyan
// accents taken from the supplied avatar artwork.
uint16_t background();
uint16_t surface();
uint16_t surfaceAlt();
uint16_t selected();
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
void header(const char* title);
void footer(const char* text);
void item(int index, const char* label, bool selected);
void line(int y, const char* text, uint16_t color = TFT_WHITE);
void panel(int x, int y, int width, int height, uint16_t color = 0);
void divider(int y, uint16_t color = 0);
}
