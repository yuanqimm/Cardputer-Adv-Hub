#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <algorithm>
#include <vector>

#define FILE_READ "r"
#define FILE_WRITE "w"

// Host adapter: production FileManager.cpp runs unchanged against a temporary
// directory. Fault injection exercises partial writes and failed final renames.
namespace MockSD {
inline bool failWrites = false;
inline unsigned renameCalls = 0, failRenameAt = 0;
}
class File {
    struct Handle {
        std::filesystem::path path;
        std::fstream stream;
        std::vector<std::filesystem::path> children;
        size_t index = 0;
        bool directory = false, open = false;
        std::string name;
    };
    std::shared_ptr<Handle> h;
public:
    File() = default;
    explicit File(const std::filesystem::path& path, bool write = false) : h(std::make_shared<Handle>()) {
        h->path = path; h->name = path.filename().string();
        if (std::filesystem::is_directory(path)) {
            h->directory = h->open = true;
            for (auto& e : std::filesystem::directory_iterator(path)) h->children.push_back(e.path());
        } else {
            h->stream.open(path, std::ios::binary | (write ? (std::ios::out|std::ios::trunc) : std::ios::in));
            h->open = h->stream.is_open();
        }
    }
    explicit operator bool() const { return h && h->open; }
    bool isDirectory() const { return h && h->directory; }
    const char* name() const { return h ? h->name.c_str() : ""; }
    size_t size() const { return *this && !h->directory ? std::filesystem::file_size(h->path) : 0; }
    File openNextFile() { return *this && h->index < h->children.size() ? File(h->children[h->index++]) : File(); }
    size_t read(uint8_t* out, size_t n) {
        if (!*this) return 0;
        h->stream.read(reinterpret_cast<char*>(out), n); return static_cast<size_t>(h->stream.gcount());
    }
    int read() { return *this ? h->stream.get() : -1; }
    bool available() { return *this && h->stream.peek() != std::char_traits<char>::eof(); }
    size_t write(const uint8_t* bytes, size_t n) {
        if (!*this || MockSD::failWrites) return 0;
        h->stream.write(reinterpret_cast<const char*>(bytes), n); return h->stream ? n : 0;
    }
    void flush() { if (*this && !h->directory) h->stream.flush(); }
    void close() { if (h) { h->stream.close(); h->open = false; } }
};
class HostSD {
public:
    std::filesystem::path root;
    std::filesystem::path resolve(const String& path) const { return root / std::filesystem::path(path.c_str()).relative_path(); }
    File open(const String& path, const char* mode = FILE_READ) { return File(resolve(path), std::string(mode) == FILE_WRITE); }
    bool exists(const String& path) const { return std::filesystem::exists(resolve(path)); }
    bool mkdir(const String& path) { std::error_code ec; return std::filesystem::create_directory(resolve(path), ec); }
    bool remove(const String& path) {
        const auto p = resolve(path); std::error_code ec;
        return !std::filesystem::is_directory(p) && std::filesystem::remove(p, ec);
    }
    bool rmdir(const String& path) {
        const auto p = resolve(path); std::error_code ec;
        return std::filesystem::is_directory(p) && std::filesystem::remove(p, ec);
    }
    bool rename(const String& from, const String& to) {
        ++MockSD::renameCalls;
        if (MockSD::failRenameAt == MockSD::renameCalls || exists(to)) return false;
        std::error_code ec; std::filesystem::rename(resolve(from), resolve(to), ec); return !ec;
    }
};
inline HostSD SD;
