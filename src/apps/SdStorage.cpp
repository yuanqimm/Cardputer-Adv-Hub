#include "app/SdStorage.h"
#include "core/FileManager.h"
#include "core/FilePath.h"
#include "core/TextCursor.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include <algorithm>
#include <cctype>

namespace {
enum class View { Browser, Menu, Name, Delete, Info, Text, Edit, Discard, Audio, Video, Copy };
enum class NameAction { Folder, Text, Rename };
struct Marker { String name; bool directory; };
View view = View::Browser;
NameAction nameAction = NameAction::Folder;
String directory = "/", selectedPath, selectedName, input, text, clipboard, notice;
Marker marker{"", false};
std::vector<Marker> previousBatches;
bool selectedDirectory = false, cut = false, modified = false, discardHome = false;
bool active = false, wasAccessible = true;
uint32_t selectedSize = 0;
size_t selected = 0, menuItem = 0, cursor = 0, textScroll = 0;
constexpr size_t Rows = 6, Columns = 38;
const char* actions[] = {"Open / Play", "Rename", "Copy", "Cut / Move", "Delete", "New folder", "New text file", "Paste here", "Properties"};
constexpr size_t ActionCount = sizeof(actions)/sizeof(actions[0]);
bool accessible() { return Storage::available() && Storage::appAccessAllowed(); }
bool up(const InputEvent& e, char key) { return e.code == 0x52 || key == 'w' || key == 'k'; }
bool down(const InputEvent& e, char key) { return e.code == 0x51 || key == 's' || key == 'j'; }
bool escape(const InputEvent& e) { return e.code == 0x29; }
String joined(const String& name) { return FilePath::join(directory.c_str(), name.c_str()).c_str(); }
void resetBatch() { marker = {"", false}; std::vector<Marker>().swap(previousBatches); selected = 0; }
void reload(bool first = false) {
    if (first) resetBatch();
    if (!FileManager::list(directory, marker.name, marker.directory)) {
        notice = FileManager::status();
        // A PC may have moved or removed this directory while sharing.
        if (accessible() && directory != "/") { directory = "/"; resetBatch(); FileManager::list(directory); }
    }
    const size_t count = FileManager::entries().size() + (FileManager::more() ? 1 : 0);
    selected = count ? std::min(selected, count-1) : 0;
    view = View::Browser;
}
bool captureSelection() {
    const auto& entries = FileManager::entries();
    if (selected >= entries.size()) { notice = "Select a file or folder first"; return false; }
    const auto& entry = entries[selected];
    selectedName = entry.name; selectedDirectory = entry.directory; selectedSize = entry.size;
    selectedPath = joined(entry.name);
    if (selectedPath.isEmpty()) { notice = "Path too long (max 240 bytes)"; return false; }
    return true;
}
void nextBatch() {
    const auto& entries = FileManager::entries();
    if (!FileManager::more() || entries.empty()) return;
    previousBatches.push_back(marker);
    marker = {entries.back().name, entries.back().directory};
    selected = 0; reload();
}
void previousBatch() {
    if (previousBatches.empty()) return;
    marker = previousBatches.back(); previousBatches.pop_back(); selected = 0; reload();
}
void backToBrowser() {
    MediaPlayer::stop(); VideoPlayer::stop();
    text = ""; modified = false; notice = ""; reload();
}
void openSelected() {
    if (selected == FileManager::entries().size() && FileManager::more()) { nextBatch(); return; }
    if (!captureSelection()) return;
    notice = "";
    if (selectedDirectory) { directory = selectedPath; reload(true); return; }
    const auto type = FilePath::kind(selectedPath.c_str());
    if (type == FilePath::Kind::Audio || type == FilePath::Kind::Video || type == FilePath::Kind::Image) {
        MediaPlayer::stop(); VideoPlayer::stop();
        // Cardputer has no PSRAM: don't keep directory strings during decoding.
        FileManager::releaseList();
        if (type == FilePath::Kind::Audio) { view = View::Audio; MediaPlayer::playFile(selectedPath.c_str()); }
        else { view = View::Video; VideoPlayer::playFile(selectedPath.c_str()); }
    } else if (type == FilePath::Kind::Text) {
        if (FileManager::readText(selectedPath, text)) { view = View::Text; textScroll = 0; cursor = 0; modified = false; }
        else { notice = FileManager::status(); view = View::Info; }
    } else { notice = "No preview for this file type"; view = View::Info; }
}
void paste() {
    if (clipboard.isEmpty()) { notice = "Clipboard is empty"; return; }
    const String destination = joined(FilePath::basename(clipboard.c_str()).c_str());
    if (destination.isEmpty()) { notice = "Destination path too long"; return; }
    if (cut) {
        if (FileManager::move(clipboard, destination)) { clipboard = ""; reload(true); }
        notice = FileManager::status();
    } else if (FileManager::copy(clipboard, destination)) view = View::Copy;
    else notice = FileManager::status();
}
void toClipboard(bool move) {
    if (!captureSelection()) return;
    if (selectedDirectory && !move) { notice = "Folder copy not supported; use Cut"; return; }
    clipboard = selectedPath; cut = move;
    notice = move ? "Cut: open destination, then V" : "Copied: open destination, then V";
    view = View::Browser;
}
void performAction() {
    if (menuItem == 0) { openSelected(); return; }
    if (menuItem == 2 || menuItem == 3) { toClipboard(menuItem == 3); return; }
    if (menuItem == 7) { view = View::Browser; paste(); return; }
    if (menuItem == 5 || menuItem == 6) {
        nameAction = menuItem == 5 ? NameAction::Folder : NameAction::Text;
        input = ""; view = View::Name; return;
    }
    if (!captureSelection()) return;
    if (menuItem == 1) { nameAction = NameAction::Rename; input = selectedName; view = View::Name; }
    if (menuItem == 4) view = View::Delete;
    if (menuItem == 8) view = View::Info;
}
size_t visualLineAt(size_t position) {
    return TextCursor::at(text.c_str(),text.length(),position,Columns).line;
}
void showText() {
    if (view == View::Edit) {
        const size_t line = visualLineAt(cursor);
        if (line < textScroll) textScroll = line;
        if (line >= textScroll+9) textScroll = line-8;
    }
    TextCursor::Position position;
    auto& canvas = Ui::canvas();
    for (size_t i=0; i<=text.length(); i=TextCursor::next(text.c_str(),text.length(),i)) {
        const size_t line = position.line, col = position.column;
        const bool visible = line >= textScroll && line < textScroll+9;
        const bool caret = view == View::Edit && i == cursor;
        const unsigned char c = i == text.length() ? ' ' : static_cast<unsigned char>(text[i]);
        if (visible && (caret || (c != '\n' && c != '\r'))) {
            const int x = 6+col*6, y = 36+(line-textScroll)*8;
            canvas.setTextColor(caret ? TFT_BLACK : TFT_WHITE, caret ? TFT_CYAN : TFT_BLACK);
            char glyph[2] = {static_cast<char>(c >= 32 && c <= 126 ? c : (c == '\t' || c == '\n' || c == '\r' ? ' ' : '?')), 0};
            canvas.drawString(glyph, x, y);
        }
        if (i == text.length()) break;
        TextCursor::advance(position, c, Columns);
    }
}
void moveCursor(int delta) {
    cursor = delta < 0 ? TextCursor::previous(text.c_str(),cursor) : TextCursor::next(text.c_str(),text.length(),cursor);
}
void edit(const InputEvent& e, char key) {
    if (e.ctrl && key == 's' && !e.repeat) {
        if (FileManager::saveText(selectedPath, text)) { modified = false; view = View::Text; }
        notice = FileManager::status(); return;
    }
    if (escape(e)) { if (modified) { discardHome = false; view = View::Discard; } else view = View::Text; return; }
    if (e.code == 0x50) { moveCursor(-1); return; }
    if (e.code == 0x4f) { moveCursor(1); return; }
    if (e.code == 0x52 || e.code == 0x51) {
        cursor = TextCursor::vertical(text.c_str(),text.length(),cursor,Columns,e.code == 0x52 ? -1 : 1);
        return;
    }
    if (e.key == '\b' && cursor) {
        const size_t end = cursor; moveCursor(-1); text.remove(cursor, end-cursor); modified = true;
    } else if (e.code == 0x4c && cursor < text.length()) {
        const size_t start = cursor; moveCursor(1); text.remove(start, cursor-start); cursor = start; modified = true;
    } else if (!e.ctrl && (!e.fn || e.key == '\n') && (e.key == '\n' || (e.key >= 32 && e.key <= 126))) {
        if (text.length() >= FileManager::MaxText) { notice = "Text limit: 4KB"; return; }
        const String edited = text.substring(0, cursor) + String(e.key) + text.substring(cursor);
        if (edited.length() != text.length()+1) { notice = "Not enough text memory"; return; }
        text = edited;
        ++cursor; modified = true;
    }
}
void browserRows() {
    const auto& entries = FileManager::entries();
    const size_t count = entries.size()+(FileManager::more()?1:0);
    if (!count) { Ui::line(55,"Empty folder",TFT_LIGHTGREY); Ui::line(75,"M: menu / create files"); return; }
    const size_t top = selected / Rows * Rows;
    for (size_t row=0; row<Rows && top+row<count; ++row) {
        const size_t index = top+row;
        const int y = 36+row*12;
        const uint16_t bg = selected == index ? TFT_BLUE : TFT_BLACK;
        Ui::canvas().fillRect(4,y-1,232,12,bg);
        Ui::canvas().setTextColor(TFT_WHITE,bg);
        char label[40];
        if (index == entries.size()) snprintf(label,sizeof(label),"[More files...]");
        else snprintf(label,sizeof(label),"%s %.30s",entries[index].directory?"[DIR]":"     ",entries[index].name.c_str());
        Ui::canvas().drawString(label,6,y);
    }
}
}
namespace SdStorage {
void begin() { active = true; wasAccessible = accessible(); notice = ""; reload(); }
void end() {
    active = false; FileManager::cancel(); MediaPlayer::stop(); VideoPlayer::stop();
    FileManager::releaseList(); text = ""; modified = false; view = View::Browser;
}
void update() {
    if (!active) return;
    const bool access = accessible();
    if (access != wasAccessible) {
        wasAccessible = access;
        if (access) { notice = "SD returned to device"; reload(true); }
        else { MediaPlayer::stop(); VideoPlayer::stop(); FileManager::cancel(); FileManager::releaseList(); }
    }
    if (view == View::Copy && !FileManager::busy()) { const String result = FileManager::status(); reload(); notice = result; }
}
bool onInput(const InputEvent& e) {
    if (e.type != InputType::Key) return false;
    const char key = static_cast<char>(tolower(static_cast<unsigned char>(e.key)));
    if (e.fn && key == 'q' && !e.repeat) {
        if (modified) { discardHome = true; view = View::Discard; return false; }
        return true;
    }
    if (!accessible()) return false;
    if (view == View::Edit) { edit(e,key); return false; }
    if (view == View::Discard) {
        if (!e.repeat && key == 'y') { modified = false; if (discardHome) return true; backToBrowser(); }
        else if (!e.repeat && (key == 'n' || escape(e))) view = View::Edit;
        return false;
    }
    if (view == View::Name) {
        if (escape(e) && !e.repeat) { view = View::Browser; return false; }
        if (e.key == '\b') { if (input.length()) input.remove(TextCursor::previous(input.c_str(),input.length())); }
        else if (e.key == '\n' && !e.repeat) {
            bool done = false;
            if (nameAction == NameAction::Folder) done = FileManager::mkdir(directory,input);
            if (nameAction == NameAction::Text) done = FileManager::createText(directory,input);
            if (nameAction == NameAction::Rename) done = FileManager::rename(selectedPath,input);
            const String result = FileManager::status();
            if (done) reload(true);
            notice = result;
        } else if (!e.ctrl && !e.fn && e.key >=32 && e.key <=126 && input.length() < FilePath::MaxName) input += e.key;
        return false;
    }
    if (view == View::Delete) {
        if (!e.repeat && key == 'y') { FileManager::remove(selectedPath); const String result = FileManager::status(); reload(); notice = result; }
        else if (!e.repeat && (key == 'n' || escape(e) || e.key == '\b')) view = View::Browser;
        return false;
    }
    if (view == View::Copy) { if (!e.repeat && (escape(e) || e.key == '\b')) FileManager::cancel(); return false; }
    if (view == View::Audio || view == View::Video) {
        if (!e.repeat && (escape(e) || e.key == '\b')) backToBrowser();
        else if (!e.repeat && (key == 'p' || e.key == ' ')) { if (view == View::Audio) MediaPlayer::toggle(); else VideoPlayer::toggle(); }
        else if (view == View::Audio && (e.key == '+' || e.key == '=')) MediaPlayer::setVolume(std::min(100,MediaPlayer::volume()+5));
        else if (view == View::Audio && e.key == '-') MediaPlayer::setVolume(std::max(0,MediaPlayer::volume()-5));
        return false;
    }
    if (view == View::Text) {
        if (!e.repeat && key == 'e') { view = View::Edit; cursor = 0; notice = ""; }
        else if (up(e,key) && textScroll) --textScroll;
        else if (down(e,key) && textScroll < visualLineAt(text.length())) ++textScroll;
        else if (!e.repeat && (escape(e) || e.key == '\b')) backToBrowser();
        return false;
    }
    if (view == View::Info) { if (!e.repeat && (escape(e) || e.key == '\b' || e.key == '\n')) view = View::Browser; return false; }
    if (view == View::Menu) {
        if (up(e,key)) menuItem = (menuItem+ActionCount-1)%ActionCount;
        else if (down(e,key)) menuItem = (menuItem+1)%ActionCount;
        else if (!e.repeat && e.key == '\n') { notice = ""; performAction(); }
        else if (!e.repeat && (escape(e) || e.key == '\b')) view = View::Browser;
        return false;
    }
    const size_t count = FileManager::entries().size()+(FileManager::more()?1:0);
    if (up(e,key) && count) selected = (selected+count-1)%count;
    else if (down(e,key) && count) selected = (selected+1)%count;
    else if (!e.repeat && e.key == '\n') openSelected();
    else if (!e.repeat && (key == 'm')) { menuItem = 0; notice = ""; view = View::Menu; }
    else if (!e.repeat && (e.key == '\b' || escape(e))) {
        if (directory == "/") return true;
        directory = FilePath::parent(directory.c_str()).c_str(); notice = ""; reload(true);
    } else if (!e.repeat && key == 'r') { notice = ""; reload(true); }
    else if (!e.repeat && key == 'v') paste();
    else if (!e.repeat && e.ctrl && (key == 'c' || key == 'x')) toClipboard(key == 'x');
    else if (!e.repeat && e.code == 0x4f) nextBatch();
    else if (!e.repeat && e.code == 0x50) previousBatch();
    return false;
}
void draw() {
    Ui::header("SD Storage");
    if (!accessible()) {
        Ui::line(40,Storage::available()?"SD shared with computer":"No SD card",TFT_YELLOW);
        Ui::line(62,Storage::available()?"Safely eject on PC first":"Insert FAT32 SD and reboot");
        Ui::footer("Fn+Q: home"); return;
    }
    char row[48];
    if (view == View::Browser) {
        const String breadcrumb = directory.length() > 38 ? "..."+directory.substring(directory.length()-35) : directory;
        Ui::line(22,breadcrumb.c_str(),TFT_CYAN); browserRows();
        snprintf(row,sizeof(row),"%u/%u%s  %s",static_cast<unsigned>(FileManager::entries().empty()?0:selected+1),static_cast<unsigned>(FileManager::entries().size()),FileManager::more()?"+":"",clipboard.isEmpty()?"":(cut?"[Cut]":"[Copy]"));
        Ui::line(111,notice.length()?notice.c_str():row,TFT_LIGHTGREY);
        Ui::footer("Enter:open M:menu BS:up R:refresh");
    } else if (view == View::Menu) {
        const size_t top = menuItem/Rows*Rows;
        for (size_t i=top; i<std::min(top+Rows,ActionCount); ++i) {
            const int y = 28+(i-top)*14;
            const uint16_t bg = i == menuItem ? TFT_BLUE : TFT_BLACK;
            Ui::canvas().fillRect(4,y-1,232,14,bg);
            Ui::canvas().setTextColor(TFT_WHITE,bg); Ui::canvas().drawString(actions[i],8,y);
        }
        Ui::line(112,notice.c_str(),TFT_YELLOW); Ui::footer("W/S:select Enter:apply BS:back");
    } else if (view == View::Name) {
        Ui::line(28,nameAction==NameAction::Folder?"New folder name:":nameAction==NameAction::Text?"New text file (e.g. notes.txt):":"Rename to:",TFT_CYAN);
        for (unsigned i=0;i<4;++i) Ui::line(48+i*12,input.substring(i*38,(i+1)*38).c_str());
        Ui::line(108,notice.c_str(),TFT_YELLOW); Ui::footer("Enter:save Esc:cancel BS:edit");
    } else if (view == View::Delete || view == View::Discard) {
        Ui::line(32,view==View::Delete?"Delete selected item?":"Discard unsaved text?",TFT_YELLOW);
        Ui::line(54,selectedName.substring(0,38).c_str());
        Ui::line(80,view==View::Delete?"Folders must be empty":"Ctrl+S in editor saves changes");
        Ui::footer("Y:confirm N:cancel");
    } else if (view == View::Info) {
        for (unsigned i=0;i<4;++i) Ui::line(28+i*12,selectedPath.substring(i*38,(i+1)*38).c_str());
        snprintf(row,sizeof(row),"%s  %lu bytes",selectedDirectory?"Folder":"File",static_cast<unsigned long>(selectedSize));
        Ui::line(84,row,TFT_CYAN); Ui::line(108,notice.c_str(),TFT_YELLOW); Ui::footer("Enter / BS: back");
    } else if (view == View::Text || view == View::Edit) {
        snprintf(row,sizeof(row),"%s%.32s",modified?"* ":"",selectedName.c_str()); Ui::line(22,row,TFT_CYAN);
        showText(); Ui::line(111,notice.c_str(),TFT_YELLOW);
        Ui::footer(view==View::Edit?"Ctrl+S:save Esc:back Arrows:cursor":"W/S:scroll E:edit BS:back");
    } else if (view == View::Audio) {
        Ui::line(30,selectedName.substring(0,38).c_str(),TFT_CYAN);
        Ui::line(52,MediaPlayer::status(),TFT_GREEN);
        const uint32_t seconds = MediaPlayer::elapsedSeconds();
        snprintf(row,sizeof(row),"%lu:%02lu  Vol:%u%%",static_cast<unsigned long>(seconds/60),static_cast<unsigned long>(seconds%60),MediaPlayer::volume()); Ui::line(75,row);
        snprintf(row,sizeof(row),"File read: %u%%",MediaPlayer::progress()); Ui::line(92,row);
        Ui::canvas().drawRect(8,108,224,5,TFT_DARKGREY); Ui::canvas().fillRect(9,109,222*MediaPlayer::progress()/100,3,TFT_CYAN);
        Ui::footer("P:pause/play +/-:volume BS:back");
    } else if (view == View::Video) {
        VideoPlayer::render();
        if (!VideoPlayer::isPlaying()) Ui::line(110,VideoPlayer::status(),TFT_CYAN);
        Ui::footer("P:pause/play BS:files Fn+Q:home");
    } else if (view == View::Copy) {
        Ui::line(36,"Copying file...",TFT_CYAN);
        snprintf(row,sizeof(row),"%u%%",FileManager::progress()); Ui::line(60,row);
        Ui::line(84,"Keep SD inserted until finished"); Ui::footer("BS / Esc: cancel copy");
    }
}
}
