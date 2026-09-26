#include "core/FileManager.h"
#include "core/FilePath.h"
#include "core/TextCursor.h"
#include <SD.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <set>

namespace { bool access = true, card = true; }
namespace Storage {
bool available() { return card; }
bool appAccessAllowed() { return access; }
}
static void put(const std::string& path, const std::string& data) {
    std::ofstream file(SD.resolve(path.c_str()), std::ios::binary); file << data;
}
static std::string get(const std::string& path) {
    std::ifstream file(SD.resolve(path.c_str()), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
int main() {
    using namespace FileManager;
    assert(FilePath::join("/", "a.txt") == "/a.txt");
    assert(FilePath::join("/a", "../secret").empty());
    assert(!FilePath::valid("/a//b") && !FilePath::valid("/a/../b"));
    assert(!FilePath::validName("CON.txt") && !FilePath::validName("a "));
    assert(FilePath::parent("/a/b/c") == "/a/b");
    assert(FilePath::kind("/nested/SONG.MP3") == FilePath::Kind::Audio);
    assert(FilePath::kind("/nested/clip.MP4") == FilePath::Kind::Other);
    assert(FilePath::within("/Music/a", "/music") && !FilePath::within("/music2", "/music"));
    assert(!FilePath::within("", "/"));
    const std::string unicode = "A\xE4\xB8\xAD\r\nB";
    assert(TextCursor::next(unicode.c_str(),unicode.size(),1) == 4);
    assert(TextCursor::previous(unicode.c_str(),4) == 1);
    assert(TextCursor::next(unicode.c_str(),unicode.size(),4) == 6);
    assert(TextCursor::previous(unicode.c_str(),6) == 4);
    assert(TextCursor::at(unicode.c_str(),unicode.size(),4,38).column == 2);
    assert(TextCursor::vertical("abc\nx\nlong",10,3,38,1) == 5);
    assert(TextCursor::vertical("abc\nx\nlong",10,5,38,-1) == 1);
    assert(TextCursor::vertical("abcdef",6,1,3,1) == 4);
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    SD.root = std::filesystem::temp_directory_path() / ("cardputer-fs-test-"+std::to_string(id));
    assert(std::filesystem::create_directory(SD.root));
    assert(mkdir("/", "folder"));
    assert(!saveText("/folder", "cannot replace directory"));
    assert(createText("/folder", "notes.txt"));
    assert(saveText("/folder/notes.txt", "Original\ntext"));
    assert(!createText("/folder", "notes.txt"));
    assert(get("/folder/notes.txt") == "Original\ntext");
    assert(!mkdir("/", "../escape"));
    assert(!FileManager::remove("/folder"));
    // Failed replacement must restore original data; neither failure truncates it.
    MockSD::renameCalls = 0; MockSD::failRenameAt = 2;
    assert(!saveText("/folder/notes.txt", "replacement"));
    assert(get("/folder/notes.txt") == "Original\ntext");
    MockSD::failRenameAt = 0; MockSD::failWrites = true;
    assert(!saveText("/folder/notes.txt", "replacement"));
    assert(get("/folder/notes.txt") == "Original\ntext");
    MockSD::failWrites = false;
    put("/folder/notes.txt.hub-bak", "recovery data");
    assert(!saveText("/folder/notes.txt", "replacement"));
    assert(get("/folder/notes.txt") == "Original\ntext");
    assert(SD.remove("/folder/notes.txt.hub-bak"));
    assert(saveText("/folder/notes.txt", "replacement"));
    assert(!SD.exists("/folder/notes.txt.hub-bak"));
    String loaded;
    assert(readText("/folder/notes.txt", loaded) && loaded == "replacement");
    put("/big.txt",std::string(4097,'x')); assert(!readText("/big.txt",loaded));
    put("/binary.txt",std::string("a\0b",3)); assert(!readText("/binary.txt",loaded));
    // Copy larger than one loop budget; cancelling leaves no destination or temp.
    const std::string payload(16001,'z'); put("/data.bin",payload);
    assert(copy("/data.bin", "/folder/data.bin")); loop();
    assert(busy() && progress() > 0 && progress() < 100);
    assert(!createText("/", "busy.txt"));
    cancel(); assert(!busy() && !SD.exists("/folder/data.bin") && !SD.exists("/folder/data.bin.hub-tmp"));
    assert(copy("/data.bin", "/folder/data.bin")); while(busy()) loop();
    assert(get("/folder/data.bin") == payload);
    assert(!copy("/data.bin", "/folder/data.bin"));
    assert(copy("/data.bin", "/failed.bin")); MockSD::failWrites = true; loop();
    assert(!busy() && !SD.exists("/failed.bin") && !SD.exists("/failed.bin.hub-tmp"));
    MockSD::failWrites = false;
    assert(!move("/folder", "/folder/child"));
    assert(!move("/folder", "/FOLDER/child"));
    assert(FileManager::rename("/folder/notes.txt", "renamed.txt"));
    assert(FileManager::remove("/folder/renamed.txt"));
    // Every file is reachable across bounded listing batches.
    assert(mkdir("/", "many")); assert(mkdir("/many", "z-folder"));
    for (int i=0;i<145;++i) put("/many/f"+std::to_string(i)+".txt","x");
    std::set<std::string> names; String marker; bool directory = false;
    do {
        assert(list("/many",marker,directory)); assert(entries().size() <= 64);
        for (const auto& e: entries()) assert(names.insert(e.name.c_str()).second);
        assert(!entries().empty());
        if (marker.isEmpty()) assert(entries().front().directory);
        marker=entries().back().name; directory=entries().back().directory;
    } while (more());
    assert(names.size() == 146);
    access = false;
    assert(!list("/") && !mkdir("/", "blocked") && !createText("/", "blocked.txt"));
    assert(!FileManager::remove("/data.bin") && !copy("/data.bin","/blocked.bin"));
    assert(!move("/data.bin","/renamed.bin") && !readText("/big.txt",loaded));
    assert(!saveText("/big.txt","truncated"));
    assert(get("/data.bin") == payload && get("/big.txt").size() == 4097);
    access = true; card = false; assert(!list("/")); card = true;
    releaseList();
    // Only this freshly created temporary fixture directory is removed.
    std::filesystem::remove_all(SD.root);
    std::cout << "PASS: SD ownership, directory paging, file operations, copy cancellation, save recovery, path rules\n";
}
