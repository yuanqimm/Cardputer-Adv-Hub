#pragma once

#include <M5Cardputer.h>

namespace Ui {
void begin();
lgfx::LGFX_Sprite& canvas();
void present();
void clear(uint16_t color = TFT_BLACK);
void header(const char* title);
void footer(const char* text);
void item(int index, const char* label, bool selected);
void line(int y, const char* text, uint16_t color = TFT_WHITE);
}
