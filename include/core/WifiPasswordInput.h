#pragma once
#include <cstddef>
#include <cstring>

// Bounded WPA personal password editor, independent of display and radio state.
class WifiPasswordInput {
public:
    void clear() { memset(text_, 0, sizeof(text_)); size_ = 0; }
    void append(char c) {
        if (c >= 32 && c <= 126 && size_ < 64) { text_[size_++] = c; text_[size_] = 0; }
    }
    void backspace() { if (size_) text_[--size_] = 0; }
    const char* text() const { return text_; }
    size_t size() const { return size_; }
    bool valid() const {
        if (size_ >= 8 && size_ <= 63) return true;
        if (size_ != 64) return false;
        for (size_t i = 0; i < size_; ++i) {
            const char c = text_[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        return true;
    }
private:
    char text_[65] = {};
    size_t size_ = 0;
};
