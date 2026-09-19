#pragma once
#include <cstddef>
#include <cstdint>
// Bounded incremental parser for concatenated JPEG images (raw MJPEG).
class JpegFrame {
public:
    enum class Result { More, Complete, TooLarge };
    JpegFrame(uint8_t* buffer, size_t capacity) : data_(buffer), capacity_(capacity) {}
    void reset() { length_ = 0; previous_ = 0; started_ = false; }
    size_t size() const { return length_; }
    Result push(uint8_t byte) {
        if (!started_) {
            if (previous_ == 0xff && byte == 0xd8) {
                if (capacity_ < 2) return Result::TooLarge;
                data_[0] = 0xff; data_[1] = 0xd8; length_ = 2; started_ = true;
            }
        } else {
            if (length_ == capacity_) { reset(); return Result::TooLarge; }
            data_[length_++] = byte;
            if (previous_ == 0xff && byte == 0xd9) { started_ = false; previous_ = 0; return Result::Complete; }
        }
        previous_ = byte;
        return Result::More;
    }
private:
    uint8_t* data_; size_t capacity_, length_ = 0;
    uint8_t previous_ = 0; bool started_ = false;
};
