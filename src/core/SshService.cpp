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
enum class State { Idle, Wifi, Connect, Verify, Trust, Authenticate, Open, Pty, Shell, Ready, Error };
State state = State::Idle;
String message = "C: connect using SD config", host, user, password, expected;
char fingerprintText[65] = {}, pinKey[15] = {};
unsigned int port = 22;
uint32_t deadline = 0;
ssh_session session = nullptr;
ssh_channel channel = nullptr;
TerminalBuffer terminalBuffer;
bool changed = false;
uint8_t outgoing[256]; size_t outgoingSize = 0;
void move(State next, const char* text) { state = next; message = text; deadline = millis() + 20000; changed = true; }
void release() {
    if (channel) { ssh_channel_free(channel); channel = nullptr; }
    if (session) { ssh_disconnect(session); ssh_free(session); session = nullptr; }
    outgoingSize = 0; password = "";
}
void fail(const char* text) { release(); move(State::Error, text); }
bool load(const char* path, JsonDocument& doc) {
    File file = SD.open(path, FILE_READ);
    return file && file.size() <= 4096 && !deserializeJson(doc, file);
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
        move(State::Authenticate,"Authenticating..."); return true;
    }
    Preferences prefs;
    String saved;
    if (prefs.begin("hub-ssh",true)) { saved = prefs.getString(pinKey,""); prefs.end(); }
    if (saved.length()) {
        if (saved != fingerprintText) { fail("Host key changed: check server"); return true; }
        move(State::Authenticate,"Authenticating...");
    } else move(State::Trust,"New host: verify SHA256");
    return true;
}
}
namespace SshService {
void begin() { libssh_begin(); }
void connect() {
    if (state != State::Idle && state != State::Error) return;
    JsonDocument wifi, config;
    if (!load("/config/wifi.json",wifi) || !load("/config/ssh.json",config)) { fail("Missing/invalid SD SSH config"); return; }
    host = config["host"] | ""; user = config["user"] | ""; password = config["password"] | "";
    const int requestedPort = config["port"] | 22;
    expected = config["host_key_sha256"] | ""; expected.toLowerCase(); expected.replace(":","");
    const String ssid = wifi["ssid"] | "";
    if (!host.length() || !user.length() || !ssid.length() || requestedPort<1 || requestedPort>65535 ||
        (expected.length() && expected.length()!=64)) { fail("Invalid host/user/port/key"); return; }
    port = requestedPort;
    uint8_t targetHash[32]; const String target = host + ":" + String(port);
    mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(target.c_str()),target.length(),targetHash,0);
    for(unsigned i=0;i<7;++i) snprintf(pinKey+i*2,3,"%02x",targetHash[i]);
    terminalBuffer.reset(); fingerprintText[0]=0;
    WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(), wifi["password"] | "");
    move(State::Wifi,"Connecting Wi-Fi...");
}
void disconnect() { release(); move(State::Idle,"Disconnected; C reconnect"); }
bool awaitingTrust() { return state == State::Trust; }
const char* fingerprint() { return fingerprintText; }
void trustServer() {
    if (!awaitingTrust()) return;
    Preferences prefs;
    if (!prefs.begin("hub-ssh",false)) { fail("Cannot save host fingerprint"); return; }
    const bool ok = prefs.putString(pinKey,fingerprintText) == 64; prefs.end();
    if (!ok) { fail("Cannot save host fingerprint"); return; }
    move(State::Authenticate,"Authenticating...");
}
void loop() {
    if (state == State::Idle || state == State::Error || state == State::Trust) return;
    if (state != State::Ready && static_cast<int32_t>(millis()-deadline)>=0) { fail("Connection stage timed out"); return; }
    if (state == State::Wifi) {
        if (WiFi.status()!=WL_CONNECTED) return;
        if (ESP.getFreeHeap()<70000) { fail("Not enough free SSH memory"); return; }
        session=ssh_new(); if(!session) { fail("Cannot allocate SSH session"); return; }
        const long timeout=5;
        if (ssh_options_set(session,SSH_OPTIONS_HOST,host.c_str())!=SSH_OK ||
            ssh_options_set(session,SSH_OPTIONS_USER,user.c_str())!=SSH_OK ||
            ssh_options_set(session,SSH_OPTIONS_PORT,&port)!=SSH_OK ||
            ssh_options_set(session,SSH_OPTIONS_TIMEOUT,&timeout)!=SSH_OK) { fail("Invalid SSH options"); return; }
        ssh_set_blocking(session,0); move(State::Connect,"SSH handshake..."); return;
    }
    if (WiFi.status()!=WL_CONNECTED) { fail("Wi-Fi disconnected"); return; }
    int rc=SSH_OK;
    switch(state) {
    case State::Connect:
        rc=ssh_connect(session);
        if(rc==SSH_OK) move(State::Verify,"Verifying server key...");
        else if(rc!=SSH_AGAIN) fail("SSH handshake failed");
        break;
    case State::Verify:
        if(!verifyKey()) fail("Cannot read server key");
        break;
    case State::Authenticate:
        rc=ssh_userauth_password(session,nullptr,password.c_str());
        if(rc==SSH_AUTH_SUCCESS) { password=""; channel=ssh_channel_new(session); if(channel) move(State::Open,"Opening shell..."); else fail("Cannot allocate channel"); }
        else if(rc!=SSH_AUTH_AGAIN) fail("SSH password auth failed");
        break;
    case State::Open:
        rc=ssh_channel_open_session(channel);
        if(rc==SSH_OK) move(State::Pty,"Starting terminal..."); else if(rc!=SSH_AGAIN) fail("SSH channel failed");
        break;
    case State::Pty:
        rc=ssh_channel_request_pty_size(channel,"vt100",TerminalBuffer::Columns,TerminalBuffer::Rows);
        if(rc==SSH_OK) move(State::Shell,"Starting shell..."); else if(rc!=SSH_AGAIN) fail("SSH PTY failed");
        break;
    case State::Shell:
        rc=ssh_channel_request_shell(channel);
        if(rc==SSH_OK) move(State::Ready,"Connected"); else if(rc!=SSH_AGAIN) fail("SSH shell failed");
        break;
    case State::Ready: {
        if(outgoingSize) {
            rc=ssh_channel_write(channel,outgoing,outgoingSize);
            if(rc>0) { memmove(outgoing,outgoing+rc,outgoingSize-rc); outgoingSize-=rc; }
            else if(rc!=SSH_AGAIN && rc<0) { fail("SSH write failed"); break; }
        }
        char data[256];
        for(int stderrStream=0;stderrStream<2;++stderrStream) {
            rc=ssh_channel_read_nonblocking(channel,data,sizeof(data),stderrStream);
            if(rc>0) { terminalBuffer.write(data,rc); changed=true; }
            else if(rc<0 && rc!=SSH_AGAIN) { fail("SSH read failed"); break; }
        }
        if(channel && (ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel))) disconnect();
        break;
    }
    default: break;
    }
}
void sendInput(const InputEvent& event) {
    if(!connected() || event.type!=InputType::Key) return;
    char data[8]; size_t size=0;
    if(event.alt) data[size++]=27;
    const char* special=nullptr;
    switch(event.code) {
    case 0x52: special="\033[A"; break; case 0x51: special="\033[B"; break;
    case 0x50: special="\033[D"; break; case 0x4f: special="\033[C"; break;
    case 0x4c: special="\033[3~"; break;
    default: break;
    }
    if(special) { const size_t n=strlen(special); memcpy(data+size,special,n); size+=n; }
    else if(event.key) {
        char ch=event.key=='\n' ? '\r' : event.key=='\b' ? 127 : event.key;
        if(event.ctrl) {
            if(ch>='a' && ch<='z') ch-=32;
            if(ch>='@' && ch<='_') ch&=31; else if(ch==' ') ch=0;
        }
        data[size++]=ch;
    }
    if(size && outgoingSize+size<=sizeof(outgoing)) { memcpy(outgoing+outgoingSize,data,size); outgoingSize+=size; }
    else if(size) { message="Input queue full"; changed=true; }
}
bool dirty() { const bool result=changed; changed=false; return result; }
bool connected() { return state==State::Ready; }
const char* status() { return message.c_str(); }
const char* terminalRow(uint8_t row) { return terminalBuffer.row(row); }
}
