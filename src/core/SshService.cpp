#include "core/SshService.h"
#include "core/TerminalBuffer.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include "libssh_esp32.h"
#include <libssh/libssh.h>

namespace {
enum class State { Idle, Scanning, Wifi, Connect, Verify, Trust, Authenticate, Open, Pty, Shell, Ready, Error };
enum class Browser { None, Wifi, Saved };
constexpr uint8_t MaxWifi = 8;
constexpr uint8_t MaxSaved = 6;
struct WifiEntry { String ssid; int32_t rssi = 0; bool secured = false; };
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
    expected = config["host_key_sha256"] | "";
    expected.toLowerCase(); expected.replace(":", "");
    if (!profile.host.length() || !profile.user.length() || requestedPort < 1 || requestedPort > 65535 ||
        (expected.length() && expected.length() != 64)) return false;
    profile.port = requestedPort;
    return true;
}
bool makeProfileForNetwork(const String& ssid, bool secured, Profile& profile) {
    for (uint8_t i=0;i<savedCountValue;++i) if (saved[i].ssid == ssid) { profile = saved[i]; expected = profile.fingerprint; return true; }
    JsonDocument wifi;
    if (!loadSshConfig(profile)) return false;
    if (!loadJson("/config/wifi.json", wifi)) {
        if (secured) return false;
        profile.ssid = ssid; profile.wifiPassword = ""; return true;
    }
    const String configured = wifi["ssid"] | "";
    const String pass = wifi["password"] | "";
    if (configured != ssid && secured) return false;
    profile.ssid = ssid;
    profile.wifiPassword = configured == ssid ? pass : "";
    return !secured || profile.wifiPassword.length() > 0;
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
    if (ESP.getFreeHeap() < 70000) { fail("Not enough free SSH memory"); return; }
    session = ssh_new(); if (!session) { fail("Cannot allocate SSH session"); return; }
    const long timeout = 5;
    if (ssh_options_set(session, SSH_OPTIONS_HOST, active.host.c_str()) != SSH_OK ||
        ssh_options_set(session, SSH_OPTIONS_USER, active.user.c_str()) != SSH_OK ||
        ssh_options_set(session, SSH_OPTIONS_PORT, &active.port) != SSH_OK ||
        ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout) != SSH_OK) { fail("Invalid SSH options"); return; }
    ssh_set_blocking(session, 0); move(State::Connect, "SSH handshake...");
}
}
namespace SshService {
void begin() { libssh_begin(); loadSaved(); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); }
void scanWifi() {
    if (state == State::Ready || state == State::Scanning || state == State::Wifi || state == State::Connect || state == State::Verify || state == State::Authenticate || state == State::Open || state == State::Pty || state == State::Shell) return;
    release(); browser = Browser::Wifi; networkCount = networkSelected = 0; WiFi.mode(WIFI_STA); WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED) { state = State::Idle; mark("Wi-Fi scan failed; C retry"); return; }
    state = State::Scanning; mark("Scanning Wi-Fi...");
}
void showSaved() { loadSaved(); browser = Browser::Saved; savedSelectedIndex = 0; mark(savedCountValue ? "Select saved SSH profile" : "No saved profile"); }
void moveSelection(int delta) {
    const uint8_t count = browser == Browser::Wifi ? networkCount : browser == Browser::Saved ? savedCountValue : 0;
    if (!count) return;
    int value = browser == Browser::Wifi ? networkSelected : savedSelectedIndex;
    value = (value + delta + count) % count;
    if (browser == Browser::Wifi) networkSelected = value; else savedSelectedIndex = value;
    changed = true;
}
bool selectCurrent() {
    if (browser == Browser::Wifi && networkCount) {
        Profile profile;
        if (!makeProfileForNetwork(networks[networkSelected].ssid, networks[networkSelected].secured, profile)) {
            mark(networks[networkSelected].secured ? "Need password in /config/wifi.json" : "Need /config/ssh.json"); return false;
        }
        active = profile; expected = active.fingerprint; terminalBuffer.reset(); fingerprintText[0] = 0; pinKey[0] = 0;
        WiFi.mode(WIFI_STA); WiFi.begin(active.ssid.c_str(), active.wifiPassword.c_str());
        browser = Browser::None; move(State::Wifi, "Connecting Wi-Fi..."); return true;
    }
    if (browser == Browser::Saved && savedCountValue) {
        active = saved[savedSelectedIndex]; expected = active.fingerprint; terminalBuffer.reset(); fingerprintText[0] = 0; pinKey[0] = 0; browser = Browser::None;
        WiFi.mode(WIFI_STA); WiFi.begin(active.ssid.c_str(), active.wifiPassword.c_str()); move(State::Wifi, "Connecting saved Wi-Fi..."); return true;
    }
    return false;
}
void connect() {
    if (browser != Browser::None && selectCurrent()) return;
    Profile profile;
    JsonDocument wifi;
    if (!loadSshConfig(profile) || !loadJson("/config/wifi.json", wifi)) { mark("Need /config/wifi.json + ssh.json"); return; }
    profile.ssid = wifi["ssid"] | ""; profile.wifiPassword = wifi["password"] | "";
    if (!profile.ssid.length()) { mark("Missing Wi-Fi SSID"); return; }
    active = profile; expected = active.fingerprint; terminalBuffer.reset(); fingerprintText[0] = 0; pinKey[0] = 0;
    WiFi.mode(WIFI_STA); WiFi.begin(active.ssid.c_str(), active.wifiPassword.c_str()); move(State::Wifi, "Connecting SD Wi-Fi...");
}
void disconnect() { release(); WiFi.disconnect(false, false); browser = Browser::None; move(State::Idle, "Disconnected; C scan Wi-Fi"); }
void cancel() { release(); WiFi.disconnect(false, false); browser = Browser::None; state = State::Idle; mark("C: scan Wi-Fi   H: saved"); }
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
void loop() {
    if (state == State::Scanning) {
        const int result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) return;
        if (result < 0) { WiFi.scanDelete(); state=State::Idle; mark("Wi-Fi scan failed; C retry"); return; }
        networkCount = 0;
        for (int i=0; i<result && networkCount<MaxWifi; ++i) {
            const String ssid = WiFi.SSID(i); if (!ssid.length()) continue;
            bool duplicate=false; for(uint8_t j=0;j<networkCount;++j) if(networks[j].ssid==ssid) duplicate=true;
            if (duplicate) continue;
            networks[networkCount].ssid = ssid;
            networks[networkCount].rssi = WiFi.RSSI(i);
            networks[networkCount].secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            ++networkCount;
        }
        WiFi.scanDelete(); state=State::Idle; networkSelected=0; mark(networkCount ? "Select Wi-Fi and press Enter" : "No Wi-Fi found; C retry"); return;
    }
    if (state == State::Idle || state == State::Error || state == State::Trust) return;
    if (state != State::Ready && static_cast<int32_t>(millis()-deadline)>=0) { fail("Connection stage timed out"); return; }
    if (state == State::Wifi) {
        if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) { fail("Wi-Fi connection failed"); return; }
        if (WiFi.status()!=WL_CONNECTED) return;
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
        else if(rc!=SSH_AGAIN) fail("SSH password auth failed"); break;
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
