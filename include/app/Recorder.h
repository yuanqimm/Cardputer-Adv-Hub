#pragma once

#include "core/InputManager.h"

namespace Recorder {
bool begin();
void end();
void update();
bool onInput(const InputEvent& event);
void draw();
bool dirty();
}
