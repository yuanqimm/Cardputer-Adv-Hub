#pragma once

#include <Arduino.h>
#include "core/InputManager.h"

enum class AppId : uint8_t {
    Launcher,
    Ssh,
    Keyboard,
    Infrared,
    SdStorage,
    Settings,
};

class App {
public:
    virtual ~App() = default;
    virtual const char* title() const = 0;
    virtual void begin() {}
    virtual void update() = 0;
    virtual void draw() = 0;
    virtual void onInput(const InputEvent&) {}
    virtual void end() {}
};
