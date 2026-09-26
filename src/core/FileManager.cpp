#include "core/FileManager.h"
#include "core/FilePath.h"
#include "core/Storage.h"
#include <SD.h>
#include <algorithm>

namespace {
constexpr size_t BatchSize = 64;
std::vector<FileManager::Entry> files;
bool hasMore = false, copying = false, changed = false;
String message = "Ready", destination, temporary;
File sourceFile, targetFile;
uint32_t copied = 0, copySize = 0;
bool fail(const char* text) { message = text; changed = true; return false; }
bool ok(const char* text) { message = text; changed = true; return true; }
bool accessible() {
    if (!Storage::appAccessAllowed()) return fail("USB owns SD: eject on PC first");
    if (!Storage::available()) return fail("No SD card; insert and reboot");
    return true;
}
bool ready() { return accessible() && (!copying || fail("Wait for copy or cancel")); }
bool pathOk(const String& p) { return FilePath::valid(p.c_str()) || fail("Invalid/too long path"); }
String child(const String& dir, const String& name) { return FilePath::join(dir.c_str(), name.c_str()).c_str(); }
bool freeTarget(const String& path) {
    if (path.isEmpty() || !pathOk(path)) return fail("Invalid name or path");
    if (SD.exists(path)) return fail("Name already exists");
    return true;
}
bool lessEntry(const FileManager::Entry& a, const FileManager::Entry& b) {
    if (a.directory != b.directory) return a.directory;
    const int c = a.name.compareTo(b.name);
    return c < 0;
}
void finishCopy(bool success, const char* text) {
    sourceFile.close(); targetFile.close(); copying = false;
    if (!success && temporary.length() && Storage::appAccessAllowed()) SD.remove(temporary);
    temporary = "";
    ok(text);
}
}
namespace FileManager {
bool list(const String& directory, const String& afterName, bool afterDirectory) {
    releaseList();
    if (!ready() || !pathOk(directory)) return false;
    File root = SD.open(directory, FILE_READ);
    if (!root || !root.isDirectory()) return fail("Cannot open folder");
    const Entry marker{afterName, 0, afterDirectory};
    size_t skipped = 0;
    for (File item = root.openNextFile(); item; item = root.openNextFile()) {
        String name = FilePath::basename(item.name()).c_str();
        if (!FilePath::validName(name.c_str())) { ++skipped; item.close(); continue; }
        Entry entry{name, static_cast<uint32_t>(item.size()), item.isDirectory()};
        item.close();
        if (afterName.length() && !lessEntry(marker, entry)) continue;
        // Keep only the next sorted batch; every entry remains reachable via More.
        const auto pos = std::lower_bound(files.begin(), files.end(), entry, lessEntry);
        if (files.size() < BatchSize) files.insert(pos, entry);
        else {
            hasMore = true;
            if (pos != files.end()) { files.insert(pos, entry); files.pop_back(); }
        }
        yield();
    }
    return ok(skipped ? "Some names too long/unsupported" : "Ready");
}
const std::vector<Entry>& entries() { return files; }
bool more() { return hasMore; }
void releaseList() { std::vector<Entry>().swap(files); hasMore = false; }
const char* status() { return message.c_str(); }
bool mkdir(const String& dir, const String& name) {
    if (!ready()) return false;
    const String path = child(dir, name);
    if (!freeTarget(path)) return false;
    return SD.mkdir(path) ? ok("Folder created") : fail("Cannot create folder");
}
bool createText(const String& dir, const String& name) {
    if (!ready()) return false;
    const String path = child(dir, name);
    if (!freeTarget(path)) return false;
    if (FilePath::kind(path.c_str()) != FilePath::Kind::Text) return fail("Use .txt, .json, .ini or .md");
    File f = SD.open(path, FILE_WRITE);
    if (!f) return fail("Cannot create file");
    f.close(); return ok("Text file created");
}
bool move(const String& from, const String& to) {
    if (!ready() || !pathOk(from) || from == "/" || !freeTarget(to)) return false;
    if (FilePath::within(to.c_str(), from.c_str())) return fail("Cannot move into itself");
    return SD.rename(from, to) ? ok("Moved") : fail("Move failed");
}
bool rename(const String& path, const String& name) {
    return move(path, child(FilePath::parent(path.c_str()).c_str(), name));
}
bool remove(const String& path) {
    if (!ready() || !pathOk(path) || path == "/") return false;
    File item = SD.open(path, FILE_READ);
    if (!item) return fail("File no longer exists");
    const bool dir = item.isDirectory(); item.close();
    const bool removed = dir ? SD.rmdir(path) : SD.remove(path);
    return removed ? ok("Deleted") : fail(dir ? "Folder not empty / delete failed" : "Delete failed");
}
bool readText(const String& path, String& text) {
    text = "";
    if (!ready() || !pathOk(path)) return false;
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return fail("Cannot open text");
    if (file.size() > MaxText) return fail("Text larger than 4KB");
    if (!text.reserve(file.size()+1)) return fail("Not enough text memory");
    const size_t expected = file.size();
    while (file.available()) {
        const int c = file.read();
        if (c < 0) return fail("Text read failed");
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) { text = ""; return fail("Binary file: cannot edit"); }
        text += static_cast<char>(c);
    }
    return text.length() == expected ? ok("Text loaded") : fail("Incomplete text read");
}
bool saveText(const String& path, const String& text) {
    if (!ready() || !pathOk(path) || path == "/") return false;
    if (text.length() > MaxText) return fail("Text limit: 4KB");
    if (SD.exists(path)) {
        File original = SD.open(path, FILE_READ);
        if (!original || original.isDirectory()) return fail("Cannot replace folder with text");
    }
    const String tmp = path + ".hub-tmp", backup = path + ".hub-bak";
    if (SD.exists(tmp) || SD.exists(backup)) return fail("Recovery file exists; check on PC");
    File out = SD.open(tmp, FILE_WRITE);
    if (!out) return fail("Cannot create temporary file");
    const bool written = out.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()) == text.length();
    out.flush(); out.close();
    if (!written) { SD.remove(tmp); return fail("Write failed; original kept"); }
    const bool existed = SD.exists(path);
    if (existed && !SD.rename(path, backup)) { SD.remove(tmp); return fail("Cannot back up original"); }
    if (!SD.rename(tmp, path)) {
        const bool restored = !existed || SD.rename(backup, path);
        if (restored) SD.remove(tmp);
        return fail(restored ? "Save failed; original kept" : "Recover .hub-bak on PC");
    }
    if (existed && !SD.remove(backup)) return ok("Saved; backup retained on SD");
    return ok("Saved");
}
bool copy(const String& from, const String& to) {
    if (!ready() || !pathOk(from) || !freeTarget(to)) return false;
    sourceFile = SD.open(from, FILE_READ);
    if (!sourceFile || sourceFile.isDirectory()) { sourceFile.close(); return fail("Copy supports files only"); }
    temporary = to + ".hub-tmp";
    if (SD.exists(temporary)) { sourceFile.close(); temporary = ""; return fail("Temporary file already exists"); }
    targetFile = SD.open(temporary, FILE_WRITE);
    if (!targetFile) { sourceFile.close(); temporary = ""; return fail("Cannot create copy"); }
    destination = to; copySize = sourceFile.size(); copied = 0; copying = true;
    return ok("Copying...");
}
void loop() {
    if (!copying) return;
    if (!accessible()) { cancel(); return; }
    const uint8_t previous = progress();
    uint8_t buffer[1024];
    for (unsigned i=0; i<4 && copied < copySize; ++i) {
        const size_t count = sourceFile.read(buffer, std::min<uint32_t>(sizeof(buffer), copySize-copied));
        if (!count || targetFile.write(buffer, count) != count) { finishCopy(false, "Copy failed; destination kept"); return; }
        copied += count;
    }
    if (previous != progress()) changed = true;
    if (copied == copySize) {
        targetFile.flush(); sourceFile.close(); targetFile.close();
        if (SD.exists(destination) || !SD.rename(temporary, destination)) finishCopy(false, "Cannot finish copy");
        else finishCopy(true, "Copied");
    }
}
void cancel() { if (copying) finishCopy(false, "Copy cancelled"); }
bool busy() { return copying; }
bool dirty() { const bool result = changed; changed = false; return result; }
uint8_t progress() { return copySize ? uint64_t(copied)*100/copySize : 0; }
}
