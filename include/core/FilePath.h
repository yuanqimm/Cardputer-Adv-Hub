#pragma once
#include <string>
#include <cctype>

// Pure path/format rules shared by firmware and native regression tests.
namespace FilePath {
constexpr size_t MaxPath = 240;
constexpr size_t MaxName = 120;
enum class Kind { Audio, Video, Image, Text, Other };
inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool validName(const std::string& s) {
    if (s.empty() || s.size() > MaxName || s == "." || s == ".." || s.back() == '.' || s.back() == ' ') return false;
    for (unsigned char c : s) if (c < 32 || c == 127 || std::string("/\\:*?\"<>|").find(c) != std::string::npos) return false;
    const auto base = lower(s.substr(0, s.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return false;
    if (base.size() == 4 && (base.substr(0,3) == "com" || base.substr(0,3) == "lpt") && base[3] >= '1' && base[3] <= '9') return false;
    return lower(s).find(".hub-tmp") == std::string::npos && lower(s).find(".hub-bak") == std::string::npos;
}
inline bool valid(const std::string& p) {
    if (p.empty() || p[0] != '/' || p.size() > MaxPath) return false;
    if (p == "/") return true;
    size_t pos = 1;
    while (pos < p.size()) {
        const auto end = p.find('/', pos);
        if (!validName(p.substr(pos, end == std::string::npos ? end : end-pos))) return false;
        if (end == std::string::npos) return true;
        pos = end + 1;
    }
    return false;
}
inline std::string parent(const std::string& p) {
    const auto slash = p.find_last_of('/');
    return slash == 0 || slash == std::string::npos ? "/" : p.substr(0, slash);
}
inline std::string basename(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }
inline std::string join(const std::string& dir, const std::string& name) {
    if (!valid(dir) || !validName(name)) return {};
    const auto p = (dir == "/" ? dir : dir + "/") + name;
    return valid(p) ? p : std::string();
}
inline bool within(const std::string& path, const std::string& dir) {
    if (path.empty() || dir.empty()) return false;
    const auto p = lower(path), d = lower(dir);
    return p == d || (d == "/" ? p[0] == '/' : p.compare(0, d.size()+1, d+"/") == 0);
}
inline Kind kind(const std::string& path) {
    const auto name = lower(basename(path));
    const auto dot = name.find_last_of('.');
    const auto ext = dot == std::string::npos ? "" : name.substr(dot);
    if (ext == ".mp3" || ext == ".wav") return Kind::Audio;
    if (ext == ".mjpeg" || ext == ".mjpg") return Kind::Video;
    if (ext == ".jpg" || ext == ".jpeg") return Kind::Image;
    if (ext == ".txt" || ext == ".json" || ext == ".ini" || ext == ".cfg" || ext == ".csv" || ext == ".md" || ext == ".log") return Kind::Text;
    return Kind::Other;
}
}
