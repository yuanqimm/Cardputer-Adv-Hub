#include "core/SshService.h"
#include "core/TerminalBuffer.h"
#include "core/WifiPasswordInput.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include "libssh_esp32.h"
#include <libssh/libssh.h>

namespace {
enum class State { Idle, Scanning, Password, SshEdit, Wifi, WifiReady, Connect, Verify, Trust, Authenticate, Open, Pty, Shell, Ready, Error };
enum class Browser { None, Wifi, Saved };
constexpr uint8_t MaxWifi = 8;
constexpr uint8_t MaxSaved = 6;
struct WifiEntry { String ssid; int32_t rssi = 0; bool secured = false; uint8_t auth = WIFI_AUTH_OPEN; };
struct Profile {
    String ssid, wifiPassword, host, user, sshPassword, fingerprint;
    uint16_t port = 22;
};
State state = State::Idle;
Browser browser = Browser::None;
WifiEntry networks[MaxWifi];
uint8_t networkCount = 0, networkSelected = 0;
Profile saved[MaxSaved];
uint8_t savedCountValue = 0, savedSelectedIndex = 0;
Profile active;
String message = "C: scan Wi-Fi   H: saved";
String expected;
WifiPasswordInput wifiPasswordInput;
enum class SshField : uint8_t { Host, User, Password };
SshField sshField = SshField::Host;
String sshEditValue;
struct WifiMemory { String ssid, password; };
WifiMemory wifiMemory[MaxSaved];
uint8_t wifiMemoryCount = 0;
bool sshConfigured = false, activeSecured = false;
bool lastWifiConnected = false;
char fingerprintText[65] = {}, pinKey[15] = {};
uint32_t deadline = 0;
ssh_session session = nullptr;
ssh_channel channel = nullptr;
TerminalBuffer terminalBuffer;
bool changed = false;
uint8_t outgoing[256]; size_t outgoingSize = 0;

void mark(const char* text) { message = text; changed = true; }
void move(State next, const char* text) { state = next; message = text; deadline = millis() + 20000; changed = true; }
void release() {
    if (channel) { ssh_channel_free(channel); channel = nullptr; }
    if (session) { ssh_disconnect(session); ssh_free(session); session = nullptr; }
    outgoingSize = 0;
}
void fail(const char* text) { release(); move(State::Error, text); }
bool canStart() { return state == State::Idle || state == State::Error || state == State::WifiReady; }
void openSshEditor() {
    sshField = SshField::Host;
    sshEditValue = active.host;
    move(State::SshEdit, "Enter SSH host or IP");
}
void loadWifiMemory() {
    Preferences prefs;
    if (!prefs.begin("ssh-prof", true)) return;
    const String stored = prefs.getString("wifi", "");
    prefs.end();
    JsonDocument doc;
    if (!stored.length() || stored.length() > 4096 || deserializeJson(doc, stored)) return;
    for (JsonObject row : doc.as<JsonArray>()) {
        if (wifiMemoryCount >= MaxSaved) break;
        const String ssid = row["ssid"] | "";
        if (!ssid.length() || ssid.length() > 32) continue;
        wifiMemory[wifiMemoryCount].ssid = ssid;
        wifiMemory[wifiMemoryCount++].password = row["password"] | "";
    }
}
bool rememberWifi() {
    int match = -1;
    for (uint8_t i=0; i<wifiMemoryCount; ++i) if (wifiMemory[i].ssid == active.ssid) { match=i; break; }
    if (match >= 0 && wifiMemory[match].password == active.wifiPassword) return true;
    // Commit a complete record array atomically; failed connections never call this.
    JsonDocument doc;
    JsonArray rows = doc.to<JsonArray>();
    JsonObject first = rows.add<JsonObject>();
    first["ssid"] = active.ssid; first["password"] = active.wifiPassword;
    for (uint8_t i=0; i<wifiMemoryCount && rows.size()<MaxSaved; ++i) {
        if (i == match) continue;
        JsonObject row = rows.add<JsonObject>();
        row["ssid"] = wifiMemory[i].ssid; row["password"] = wifiMemory[i].password;
    }
    String stored; serializeJson(doc, stored);
    Preferences prefs;
    if (!prefs.begin("ssh-prof", false)) return false;
    const bool ok = prefs.putString("wifi", stored) == stored.length();
    prefs.end();
    if (ok) { wifiMemoryCount = 0; loadWifiMemory(); }
    return ok;
}
bool loadJson(const char* path, JsonDocument& doc) {
    File file = SD.open(path, FILE_READ);
    if (!file || file.size() > 4096) { if (file) file.close(); return false; }
    const bool ok = !deserializeJson(doc, file);
    file.close();
    return ok;
}
String keyFor(uint8_t index, const char* suffix) {
    char key[16]; snprintf(key, sizeof(key), "p%u_%s", index, suffix); return key;
}
void loadSaved() {
    savedCountValue = 0;
    Preferences prefs;
    if (!prefs.begin("ssh-prof", true)) return;
    const uint8_t count = min<uint8_t>(prefs.getUChar("count", 0), MaxSaved);
    for (uint8_t i = 0; i < count; ++i) {
        Profile p;
        p.ssid = prefs.getString(keyFor(i,"ssid").c_str(), "");
        p.wifiPassword = prefs.getString(keyFor(i,"wpass").c_str(), "");
        p.host = prefs.getString(keyFor(i,"host").c_str(), "");
        p.user = prefs.getString(keyFor(i,"user").c_str(), "");
        p.sshPassword = prefs.getString(keyFor(i,"spass").c_str(), "");
        p.fingerprint = prefs.getString(keyFor(i,"fp").c_str(), "");
        p.port = prefs.getUShort(keyFor(i,"port").c_str(), 22);
        if (p.ssid.length() && p.host.length() && p.user.length()) saved[savedCountValue++] = p;
    }
    prefs.end();
}
void saveProfile(const Profile& profile) {
    if (!profile.ssid.length() || !profile.host.length() || !profile.user.length()) return;
    int match = -1;
    for (uint8_t i = 0; i < savedCountValue; ++i) {
        if (saved[i].ssid == profile.ssid && saved[i].host == profile.host && saved[i].port == profile.port && saved[i].user == profile.user) { match = i; break; }
    }
    if (match < 0) {
        if (savedCountValue < MaxSaved) match = savedCountValue++;
        else { for (uint8_t i=1;i<MaxSaved;++i) saved[i-1]=saved[i]; match=MaxSaved-1; }
    }
    saved[match] = profile;
    Preferences prefs;
    if (!prefs.begin("ssh-prof", false)) return;
    prefs.putUChar("count", savedCountValue);
    for (uint8_t i = 0; i < savedCountValue; ++i) {
        prefs.putString(keyFor(i,"ssid").c_str(), saved[i].ssid);
        prefs.putString(keyFor(i,"wpass").c_str(), saved[i].wifiPassword);
        prefs.putString(keyFor(i,"host").c_str(), saved[i].host);
        prefs.putString(keyFor(i,"user").c_str(), saved[i].user);
        prefs.putString(keyFor(i,"spass").c_str(), saved[i].sshPassword);
        prefs.putString(keyFor(i,"fp").c_str(), saved[i].fingerprint);
        prefs.putUShort(keyFor(i,"port").c_str(), saved[i].port);
    }
    prefs.end();
}
bool loadSshConfig(Profile& profile) {
    JsonDocument config;
    if (!loadJson("/config/ssh.json", config)) return false;
    profile.host = config["host"] | "";
    profile.user = config["user"] | "";
    profile.sshPassword = config["password"] | "";
    const int requestedPort = config["port"] | 22;
    profile.fingerprint = config["host_key_sha256"] | "";
    profile.fingerprint.toLowerCase(); profile.fingerprint.replace(":", "");
    if (!profile.host.length() || !profile.user.length() || requestedPort < 1 || requestedPort > 65535 ||
        (profile.fingerprint.length() && profile.fingerprint.length() != 64)) return false;
    for (size_t i=0; i<profile.fingerprint.length(); ++i) if (!isxdigit(static_cast<unsigned char>(profile.fingerprint[i]))) return false;
    profile.port = requestedPort;
    return true;
}
bool makeProfileForNetwork(const String& ssid, bool secured, Profile& profile, bool& needsPassword) {
    needsPassword = secured;
    sshConfigured = loadSshConfig(profile);
    if (!sshConfigured) {
        profile = Profile();
        for (uint8_t i=0;i<savedCountValue;++i) {
            if (saved[i].ssid == ssid) { profile = saved[i]; sshConfigured = true; break; }
        }
    }
    profile.ssid = ssid;
    profile.wifiPassword = "";
    if (!secured) return true;
    for (uint8_t i=0;i<wifiMemoryCount;++i) {
        if (wifiMemory[i].ssid == ssid && wifiMemory[i].password.length()) {
            profile.wifiPassword = wifiMemory[i].password; needsPassword = false; return true;
        }
    }
    for (uint8_t i=0;i<savedCountValue;++i) {
        if (saved[i].ssid == ssid && saved[i].wifiPassword.length()) {
            profile.wifiPassword = saved[i].wifiPassword; needsPassword = false; return true;
        }
    }
    return true;
}
bool verifyKey() {
    ssh_key key = nullptr; unsigned char* hash = nullptr; size_t length = 0;
    if (ssh_get_server_publickey(session, &key) != SSH_OK) return false;
    const int rc = ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length);
    ssh_key_free(key);
    if (rc != SSH_OK || length != 32) { if (hash) ssh_clean_pubkey_hash(&hash); return false; }
    for (size_t i=0;i<32;++i) snprintf(fingerprintText+i*2,3,"%02x",hash[i]);
    ssh_clean_pubkey_hash(&hash);
    if (expected.length()) {
        if (expected != fingerprintText) { fail("Host key changed: check server"); return true; }
        active.fingerprint = fingerprintText;
        move(State::Authenticate,"Authenticating..."); return true;
    }
    expected = active.fingerprint;
    if (expected.length()) {
        if (expected != fingerprintText) { fail("Host key changed: check server"); return true; }
        move(State::Authenticate,"Authenticating..."); return true;
    }
    uint8_t targetHash[32]; const String target = active.host + ":" + String(active.port);
    mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(target.c_str()), target.length(), targetHash, 0);
    for(unsigned i=0;i<7;++i) snprintf(pinKey+i*2,3,"%02x",targetHash[i]);
    Preferences prefs; String savedFingerprint;
    if (prefs.begin("hub-ssh", true)) { savedFingerprint = prefs.getString(pinKey, ""); prefs.end(); }
    if (savedFingerprint.length()) {
        if (savedFingerprint != fingerprintText) { fail("Host key changed: check server"); return true; }
        active.fingerprint = fingerprintText; move(State::Authenticate,"Authenticating...");
    } else move(State::Trust,"New host: verify SHA256");
    return true;
}
void beginSsh() {
    const uint32_t freeHeap = ESP.getFreeHeap();
    // Wi-Fi keeps a sizeable receive buffer while LibSSH allocates its crypto state.
    // Leave 48 KiB for that state and report the actual value when the guard trips.
    if (freeHeap < 48000) {
        static char text[40];
        snprintf(text, sizeof(text), "SSH memory low: %uK", static_cast<unsigned>(freeHeap / 1024));
        fail(text); return;
    }
    session = ssh_new(); if (!session) { fail("Cannot allocate SSH session"); return; }
    const long timeout = 5;
    const int sshPort = active.port; // libssh reads an int, not a uint16_t.
    if (ssh_options_set(session, SSH_OPTIONS_HOST, active.host.c_str()) != SSH_OK ||
        ssh_options_set(session, SSH_OPTIONS_USER, active.user.c_str()) != SSH_OK ||
        ssh_options_set(session, SSH_OPTIONS_PORT, &sshPort) != SSH_OK ||
        ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout) != SSH_OK) { fail("Invalid SSH options"); return; }
    ssh_set_blocking(session, 0); move(State::Connect, "SSH handshake...");
}
void beginWifiConnection(const char* text) {
    release();
    terminalBuffer.reset(); fingerprintText[0] = 0; pinKey[0] = 0;
    wifiPasswordInput.clear();
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_STA); WiFi.begin(active.ssid.c_str(), active.wifiPassword.c_str());
    browser = Browser::None; move(State::Wifi, text);
}
}
namespace SshService {
void begin() { libssh_begin(); loadSaved(); loadWifiMemory(); WiFi.persistent(false); WiFi.setAutoReconnect(true); lastWifiConnected = WiFi.status() == WL_CONNECTED; }
void scanWifi() {
    if (!canStart()) return;
    release(); browser = Browser::Wifi; networkCount = networkSelected = 0; WiFi.mode(WIFI_STA); WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED) { state = State::Idle; mark("Wi-Fi scan failed; C retry"); return; }
    move(State::Scanning, "Scanning Wi-Fi...");
}
void showSaved() { if (!canStart()) return; loadSaved(); browser = Browser::Saved; savedSelectedIndex = 0; mark(savedCountValue ? "Select saved SSH profile" : "No saved profile"); }
void moveSelection(int delta) {
    const uint8_t count = browser == Browser::Wifi ? networkCount : browser == Browser::Saved ? savedCountValue : 0;
    if (!count) return;
    int value = browser == Browser::Wifi ? networkSelected : savedSelectedIndex;
    value = (value + delta + count) % count;
    if (browser == Browser::Wifi) networkSelected = value; else savedSelectedIndex = value;
    changed = true;
}
bool selectCurrent() {
    if (!canStart()) return false;
    if (browser == Browser::Wifi && networkCount) {
        const auto auth = networks[networkSelected].auth;
        if (auth != WIFI_AUTH_OPEN && auth != WIFI_AUTH_WPA_PSK && auth != WIFI_AUTH_WPA2_PSK &&
            auth != WIFI_AUTH_WPA_WPA2_PSK && auth != WIFI_AUTH_WPA3_PSK && auth != WIFI_AUTH_WPA2_WPA3_PSK) {
            mark("Unsupported Wi-Fi security"); return false;
        }
        Profile profile; bool needsPassword = false;
        if (!makeProfileForNetwork(networks[networkSelected].ssid, networks[networkSelected].secured, profile, needsPassword)) {
            mark("Need /config/ssh.json"); return false;
        }
        active = profile; expected = active.fingerprint;
        activeSecured = networks[networkSelected].secured;
        if (needsPassword) { wifiPasswordInput.clear(); browser = Browser::None; move(State::Password, "Enter Wi-Fi password"); return true; }
        beginWifiConnection("Connecting Wi-Fi..."); return true;
    }
    if (browser == Browser::Saved && savedCountValue) {
        active = saved[savedSelectedIndex]; expected = active.fingerprint;
        sshConfigured = true; activeSecured = active.wifiPassword.length() > 0;
        beginWifiConnection("Connecting saved Wi-Fi..."); return true;
    }
    return false;
}
void connect() {
    if (!canStart()) return;
    browser = Browser::None;
    Profile profile;
    JsonDocument wifi;
    if (!loadSshConfig(profile) || !loadJson("/config/wifi.json", wifi)) { mark("Need /config/wifi.json + ssh.json"); return; }
    profile.ssid = wifi["ssid"] | ""; profile.wifiPassword = wifi["password"] | "";
    if (!profile.ssid.length()) { mark("Missing Wi-Fi SSID"); return; }
    active = profile; expected = active.fingerprint; sshConfigured = true; activeSecured = active.wifiPassword.length() > 0;
    beginWifiConnection("Connecting SD Wi-Fi...");
}
void disconnect() {
    if (state == State::Scanning) esp_wifi_scan_stop();
    WiFi.scanDelete(); release(); wifiPasswordInput.clear(); browser = Browser::None;
    if (WiFi.status() == WL_CONNECTED) { state = State::WifiReady; mark("Wi-Fi connected; D SSH config"); }
    else { state = State::Idle; mark("Wi-Fi disconnected; C scan"); }
}
void cancel() { disconnect(); }
bool awaitingTrust() { return state == State::Trust; }
const char* fingerprint() { return fingerprintText; }
void trustServer() {
    if (!awaitingTrust()) return;
    active.fingerprint = fingerprintText;
    Preferences prefs;
    if (!prefs.begin("hub-ssh", false)) { fail("Cannot save host fingerprint"); return; }
    const bool ok = prefs.putString(pinKey, fingerprintText) == 64; prefs.end();
    if (!ok) { fail("Cannot save host fingerprint"); return; }
    move(State::Authenticate,"Authenticating...");
}
bool enteringPassword() { return state == State::Password; }
const char* passwordSsid() { return active.ssid.c_str(); }
const char* passwordDisplay() {
    static char masked[34];
    const size_t length = min<size_t>(wifiPasswordInput.size(), sizeof(masked) - 1);
    for (size_t i = 0; i < length; ++i) masked[i] = '*';
    masked[length] = 0; return masked;
}
void changeWifiPassword() {
    if (!canStart()) return;
    if (browser == Browser::Wifi || browser == Browser::Saved) {
        // Selection may start connecting with stored credentials. Stop that attempt
        // before entering the editor; the next loop has not run yet.
        if (!selectCurrent()) return;
        WiFi.disconnect(false, false);
    }
    if (!active.ssid.length() || !activeSecured) return;
    release(); browser = Browser::None; wifiPasswordInput.clear();
    move(State::Password, "Enter new Wi-Fi password");
}
void editPassword(const InputEvent& event) {
    if (!enteringPassword() || event.type != InputType::Key) return;
    if (event.repeat) return;
    if (event.key == 27) { wifiPasswordInput.clear(); browser = Browser::Wifi; state = State::Idle; mark("Select Wi-Fi and press Enter"); return; }
    if (event.fn || event.ctrl || event.alt) return;
    if (event.key == '\n') {
        if (event.repeat) return;
        if (!wifiPasswordInput.valid()) { mark("Use 8-63 chars or 64 hex digits"); return; }
        active.wifiPassword = wifiPasswordInput.text(); beginWifiConnection("Connecting Wi-Fi..."); return;
    }
    if (event.key == '\b') { wifiPasswordInput.backspace(); changed = true; return; }
    wifiPasswordInput.append(event.key); changed = true;
}
bool editingSsh() { return state == State::SshEdit; }
void beginSshSetup() {
    if (!canStart()) return;
    if (WiFi.status() != WL_CONNECTED) { mark("Connect Wi-Fi first"); return; }
    if (!active.host.length()) {
        Profile configured;
        if (loadSshConfig(configured)) {
            configured.ssid = active.ssid;
            configured.wifiPassword = active.wifiPassword;
            active = configured;
        }
    }
    openSshEditor();
}
const char* sshFieldName() {
    switch (sshField) {
    case SshField::Host: return "SSH host or IP";
    case SshField::User: return "SSH username";
    default: return "SSH password";
    }
}
const char* sshEditDisplay() {
    static char masked[66];
    if (sshField != SshField::Password) return sshEditValue.c_str();
    const size_t length = min<size_t>(sshEditValue.length(), sizeof(masked) - 1);
    for (size_t i=0; i<length; ++i) masked[i] = '*';
    masked[length] = 0;
    return masked;
}
void editSsh(const InputEvent& event) {
    if (!editingSsh() || event.type != InputType::Key || event.repeat) return;
    if (event.key == 27) {
        state = WiFi.status() == WL_CONNECTED ? State::WifiReady : State::Idle;
        mark(state == State::WifiReady ? "Wi-Fi connected; I SSH login" : "C: scan Wi-Fi   H: saved");
        return;
    }
    if (event.fn || event.ctrl || event.alt) return;
    if (event.key == '\b') { if (sshEditValue.length()) sshEditValue.remove(sshEditValue.length() - 1); changed = true; return; }
    if (event.key != '\n') {
        const size_t limit = sshField == SshField::Host ? 63 : sshField == SshField::User ? 31 : 63;
        if (event.key >= 32 && event.key <= 126 && sshEditValue.length() < limit) { sshEditValue += event.key; changed = true; }
        return;
    }
    if (!sshEditValue.length()) { mark("This field cannot be empty"); return; }
    if (sshField == SshField::Host) {
        active.host = sshEditValue; sshField = SshField::User; sshEditValue = active.user; mark("Enter SSH username"); return;
    }
    if (sshField == SshField::User) {
        active.user = sshEditValue; sshField = SshField::Password; sshEditValue = active.sshPassword; mark("Enter SSH password"); return;
    }
    active.sshPassword = sshEditValue; active.port = 22; active.fingerprint = ""; expected = ""; sshConfigured = true;
    beginSsh();
}
void loop() {
    const bool wifiNow = WiFi.status() == WL_CONNECTED;
    if (wifiNow != lastWifiConnected) { lastWifiConnected = wifiNow; changed = true; }
    if (state == State::Scanning) {
        const int result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) {
            if (static_cast<int32_t>(millis()-deadline)>=0) { esp_wifi_scan_stop(); WiFi.scanDelete(); state=State::Idle; mark("Wi-Fi scan timed out; C retry"); }
            return;
        }
        if (result < 0) { WiFi.scanDelete(); state=State::Idle; mark("Wi-Fi scan failed; C retry"); return; }
        networkCount = 0;
        for (int i=0; i<result && networkCount<MaxWifi; ++i) {
            const String ssid = WiFi.SSID(i); if (!ssid.length()) continue;
            bool duplicate=false; for(uint8_t j=0;j<networkCount;++j) if(networks[j].ssid==ssid) duplicate=true;
            if (duplicate) continue;
            networks[networkCount].ssid = ssid;
            networks[networkCount].rssi = WiFi.RSSI(i);
            networks[networkCount].secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            networks[networkCount].auth = WiFi.encryptionType(i);
            ++networkCount;
        }
        WiFi.scanDelete(); state=State::Idle; networkSelected=0; mark(networkCount ? "Select Wi-Fi and press Enter" : "No Wi-Fi found; C retry"); return;
    }
    if (state == State::Idle || state == State::Error || state == State::Password || state == State::SshEdit || state == State::Trust) return;
    if (state == State::WifiReady) { if (WiFi.status()!=WL_CONNECTED) fail("Wi-Fi disconnected; C scan"); return; }
    if (state != State::Ready && static_cast<int32_t>(millis()-deadline)>=0) { fail("Connection stage timed out"); return; }
    if (state == State::Wifi) {
        if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) { fail("Wi-Fi failed; E edit password"); return; }
        if (WiFi.status()!=WL_CONNECTED) return;
        const bool remembered = rememberWifi();
        if (!sshConfigured) { openSshEditor(); return; }
        beginSsh(); return;
    }
    if (WiFi.status()!=WL_CONNECTED) { fail("Wi-Fi disconnected"); return; }
    int rc=SSH_OK;
    switch(state) {
    case State::Connect: rc=ssh_connect(session); if(rc==SSH_OK) move(State::Verify,"Verifying server key..."); else if(rc!=SSH_AGAIN) fail("SSH handshake failed"); break;
    case State::Verify: if(!verifyKey()) fail("Cannot read server key"); break;
    case State::Authenticate:
        rc=ssh_userauth_password(session,nullptr,active.sshPassword.c_str());
        if(rc==SSH_AUTH_SUCCESS) { saveProfile(active); active.sshPassword=""; channel=ssh_channel_new(session); if(channel) move(State::Open,"Opening shell..."); else fail("Cannot allocate channel"); }
        else if(rc!=SSH_AUTH_AGAIN) fail("SSH password auth failed"); break;
    case State::Open: rc=ssh_channel_open_session(channel); if(rc==SSH_OK) move(State::Pty,"Starting terminal..."); else if(rc!=SSH_AGAIN) fail("SSH channel failed"); break;
    case State::Pty: rc=ssh_channel_request_pty_size(channel,"vt100",TerminalBuffer::Columns,TerminalBuffer::Rows); if(rc==SSH_OK) move(State::Shell,"Starting shell..."); else if(rc!=SSH_AGAIN) fail("SSH PTY failed"); break;
    case State::Shell: rc=ssh_channel_request_shell(channel); if(rc==SSH_OK) move(State::Ready,"Connected"); else if(rc!=SSH_AGAIN) fail("SSH shell failed"); break;
    case State::Ready: {
        if(outgoingSize) { rc=ssh_channel_write(channel,outgoing,outgoingSize); if(rc>0) { memmove(outgoing,outgoing+rc,outgoingSize-rc); outgoingSize-=rc; } else if(rc!=SSH_AGAIN && rc<0) { fail("SSH write failed"); break; } }
        char data[256]; for(int stderrStream=0;stderrStream<2;++stderrStream) { rc=ssh_channel_read_nonblocking(channel,data,sizeof(data),stderrStream); if(rc>0) { terminalBuffer.write(data,rc); changed=true; } else if(rc<0 && rc!=SSH_AGAIN) { fail("SSH read failed"); break; } }
        if(channel && (ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel))) disconnect(); break;
    }
    default: break;
    }
}
void sendInput(const InputEvent& event) {
    if(!connected() || event.type!=InputType::Key) return;
    char data[8]; size_t size=0; if(event.alt) data[size++]=27; const char* special=nullptr;
    switch(event.code) { case 0x52:special="\033[A";break; case 0x51:special="\033[B";break; case 0x50:special="\033[D";break; case 0x4f:special="\033[C";break; case 0x4c:special="\033[3~";break; default:break; }
    if(special) { const size_t n=strlen(special); memcpy(data+size,special,n); size+=n; } else if(event.key) { char ch=event.key=='\n'?'\r':event.key=='\b'?127:event.key; if(event.ctrl) { if(ch>='a'&&ch<='z')ch-=32; if(ch>='@'&&ch<='_')ch&=31; else if(ch==' ')ch=0; } data[size++]=ch; }
    if(size && outgoingSize+size<=sizeof(outgoing)) { memcpy(outgoing+outgoingSize,data,size); outgoingSize+=size; } else if(size) mark("Input queue full");
}
bool dirty() { const bool result=changed; changed=false; return result; }
bool connected() { return state==State::Ready; }
const char* status() { return message.c_str(); }
const char* terminalRow(uint8_t row) { return terminalBuffer.row(row); }
uint8_t view() { return static_cast<uint8_t>(browser); }
uint8_t wifiCount() { return networkCount; }
uint8_t wifiSelected() { return networkSelected; }
const char* wifiName(uint8_t index) { return index<networkCount ? networks[index].ssid.c_str() : ""; }
int32_t wifiRssi(uint8_t index) { return index<networkCount ? networks[index].rssi : 0; }
bool wifiSecured(uint8_t index) { return index<networkCount && networks[index].secured; }
uint8_t savedCount() { return savedCountValue; }
uint8_t savedSelected() { return savedSelectedIndex; }
const char* savedSsid(uint8_t index) { return index<savedCountValue ? saved[index].ssid.c_str() : ""; }
const char* savedTarget(uint8_t index) { static char text[40]; if(index>=savedCountValue)return ""; snprintf(text,sizeof(text),"%s:%u",saved[index].host.c_str(),saved[index].port); return text; }
}
