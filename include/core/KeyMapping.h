#pragma once
#include <cstdint>

namespace KeyMapping {
inline uint8_t code(uint8_t key, bool fn) {
    if (!fn) return key;
    switch (key) {
    case 0x33: return 0x52; // Fn+; up
    case 0x37: return 0x51; // Fn+. down
    case 0x36: return 0x50; // Fn+, left
    case 0x38: return 0x4f; // Fn+/ right
    case 0x35: return 0x29; // Fn+` escape
    case 0x2a: return 0x4c; // Fn+backspace delete
    default: return 0;     // remaining Fn combinations are local controls
    }
}
inline char text(uint8_t key, bool shift) {
    if (key >= 4 && key <= 29) return (shift ? 'A' : 'a') + key - 4;
    if (key >= 30 && key <= 39) return (shift ? "!@#$%^&*()" : "1234567890")[key-30];
    switch (key) {
    case 0x28: return '\n'; case 0x29: return 27;
    case 0x2a: return '\b'; case 0x2b: return '\t'; case 0x2c: return ' ';
    default:
        if (key >= 0x2d && key <= 0x38)
            return (shift ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[key-0x2d];
        return 0;
    }
}
}
