#pragma once

#include <Arduino.h>

enum class InputType : uint8_t { None, Key, EncoderPress, EncoderUp, EncoderDown };

struct InputEvent {
    InputType type = InputType::None;
    char key = 0;
    bool pressed = false;
    bool fn = false;
    bool ctrl = false;
    bool alt = false;
    bool repeat = false;
    uint8_t code = 0;
};

class InputManager {
public:
    void begin();
    InputEvent poll();
};
