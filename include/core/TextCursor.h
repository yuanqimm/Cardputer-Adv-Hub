#pragma once
#include <cstddef>

// UTF-8 codepoints occupy one display cell; CRLF is one editable newline.
// Non-ASCII glyphs use a placeholder, but edits preserve untouched bytes.
namespace TextCursor {
struct Position { size_t line = 0, column = 0; };
inline size_t next(const char* text, size_t length, size_t offset) {
    if (offset >= length) return length;
    if (text[offset] == '\r' && offset+1 < length && text[offset+1] == '\n') return offset+2;
    ++offset;
    while (offset < length && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) ++offset;
    return offset;
}
inline size_t previous(const char* text, size_t offset) {
    if (!offset) return 0;
    --offset;
    while (offset && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) --offset;
    if (offset && text[offset] == '\n' && text[offset-1] == '\r') --offset;
    return offset;
}
inline void advance(Position& p, char c, size_t columns) {
    if (c == '\n' || c == '\r' || ++p.column == columns) { ++p.line; p.column = 0; }
}
inline Position at(const char* text, size_t length, size_t offset, size_t columns) {
    Position p;
    for (size_t i=0; i<offset && i<length; i=next(text,length,i)) advance(p,text[i],columns);
    return p;
}
inline size_t vertical(const char* text, size_t length, size_t offset, size_t columns, int direction) {
    const Position current = at(text,length,offset,columns);
    if (direction < 0 && !current.line) return offset;
    const size_t target = direction < 0 ? current.line-1 : current.line+1;
    Position p;
    size_t best = offset;
    for (size_t i=0; i<=length; i=next(text,length,i)) {
        if (p.line > target) break;
        if (p.line == target) {
            best = i;
            if (p.column >= current.column) break;
        }
        if (i == length) break;
        advance(p,text[i],columns);
    }
    return best;
}
}
