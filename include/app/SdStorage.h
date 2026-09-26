#pragma once
#include "core/InputManager.h"
namespace SdStorage {
void begin();
void end();
void update();
void draw();
// Returns true only when navigation requests the launcher home page.
bool onInput(const InputEvent& event);
}
