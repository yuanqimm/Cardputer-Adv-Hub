#pragma once
#include <cstdint>
#include <string>
class String {
    std::string value;
public:
    String(const char* s = "") : value(s ? s : "") {}
    String(const std::string& s) : value(s) {}
    const char* c_str() const { return value.c_str(); }
    bool isEmpty() const { return value.empty(); }
    size_t length() const { return value.size(); }
    bool reserve(size_t n) { value.reserve(n); return true; }
    int compareTo(const String& other) const { return value.compare(other.value); }
    String& operator+=(char c) { value += c; return *this; }
    friend String operator+(const String& a, const String& b) { return a.value+b.value; }
    friend bool operator==(const String& a, const String& b) { return a.value == b.value; }
    friend bool operator!=(const String& a, const String& b) { return !(a==b); }
};
inline void yield() {}
