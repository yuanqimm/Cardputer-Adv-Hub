#include "app/App.h"
#include "app/SdStorage.h"
#include "app/Recorder.h"
#include "core/Ui.h"
#include "core/Storage.h"
#include "core/AppSettings.h"
#include "core/KeyboardManager.h"
#include "core/UsbKeyboardService.h"
#include "core/BleKeyboardService.h"
#include "core/IrRemote.h"
#include "core/SshService.h"
#include "core/UsbStorageService.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include <cstring>
#include <cctype>

class LauncherApp final : public App {
public:
    const char* title() const override { return "Cardputer Adv Hub"; }
    void update() override {
        if (!UsbStorageService::hostActive()) confirmUsbStop_=false;
        if (screen_==4) SdStorage::update();
        if (screen_==7) Recorder::update();
    }
    void onInput(const InputEvent& e) override {
        if(e.type!=InputType::Key) return;
        // The file editor owns printable keys and confirms unsaved text on exit.
        if(screen_==4) { if(SdStorage::onInput(e)) home(); return; }
        const char key=static_cast<char>(tolower(static_cast<unsigned char>(e.key)));
        if(screen_==7) {
            const bool leave = (e.fn && key=='q') || key=='\b' || key==27;
            Recorder::onInput(e);
            if (leave) home();
            return;
        }
        if(e.fn && key=='q') { if(!e.repeat) home(); return; }
        if(screen_==2) {
            if(e.fn && !e.repeat) {
                if(key=='m') KeyboardManager::cycleMode();
                if(key=='d') diagnostics_=!diagnostics_;
                if(key=='r') BleKeyboardService::resetPairings();
            }
            return;
        }
        if(screen_==1 && SshService::connected()) { if(!e.fn || e.code) SshService::sendInput(e); return; }
        if(screen_==1 && SshService::awaitingTrust()) { if(key=='t' && !e.repeat) SshService::trustServer(); return; }
        if(screen_==1 && SshService::enteringPassword()) { SshService::editPassword(e); return; }
        if(screen_==1 && SshService::editingSsh()) { SshService::editSsh(e); return; }
        if(screen_==0) {
            if(up(e,key)) selected_=(selected_+5)%6;
            else if(down(e,key)) selected_=(selected_+1)%6;
            else if(e.key=='\n' && !e.repeat) { const uint8_t pages[] = {1,2,3,4,5,7}; enter(pages[selected_]); }
            return;
        }
        if((key=='q' || e.key=='\b') && !e.repeat) { home(); return; }
        if(screen_==1) {
            if(!e.repeat && key=='c') { SshService::scanWifi(); return; }
            if(!e.repeat && key=='h') { SshService::showSaved(); return; }
            if(!e.repeat && key=='d') { SshService::connect(); return; }
            if(!e.repeat && key=='e' && SshService::view()==0) { SshService::changeWifiPassword(); return; }
            if(!e.repeat && key=='i' && SshService::view()==0) { SshService::beginSshSetup(); return; }
            if(SshService::view()!=0) {
                if(up(e,key)) SshService::moveSelection(-1);
                else if(down(e,key)) SshService::moveSelection(1);
                else if(e.key=='\n' && !e.repeat) SshService::selectCurrent();
                return;
            }
            return;
        }
        if(screen_==3 && !e.repeat) {
            if(key>='1' && key<='9') IrRemote::sendButton(key-'1');
            if(key=='n') IrRemote::nextProfile();
            if(key=='r') IrRemote::loadProfile();
        } else if(screen_==5) {
            if(up(e,key)) setting_=(setting_+3)%4;
            else if(down(e,key)) setting_=(setting_+1)%4;
            else if(e.key=='\n' && !e.repeat && setting_==3) { confirmUsbStop_=false; enter(6); }
            else if(e.key=='+' || e.key=='=' || e.code==0x4f) adjust(1);
            else if(e.key=='-' || e.code==0x50) adjust(-1);
        } else if(screen_==6 && !e.repeat) {
            if (confirmUsbStop_) {
                if (key=='y') { UsbStorageService::setEnabled(false); confirmUsbStop_=false; }
                else if (key=='n') confirmUsbStop_=false;
            } else if(e.key=='\n') {
                if (UsbStorageService::hostActive()) confirmUsbStop_=true;
                else UsbStorageService::setEnabled(true);
            }
        }
    }
    void draw() override {
        Ui::clear();
        switch(screen_) {
        case 0: drawHome(); break;
        case 1: drawSsh(); break;
        case 2: drawKeyboard(); break;
        case 3: drawIr(); break;
        case 4: SdStorage::draw(); break;
        case 5: drawSettings(); break;
        case 6: drawUsbStorage(); break;
        case 7: Recorder::draw(); break;
        }
        Ui::present();
    }
private:
    uint8_t screen_=0,selected_=0,setting_=0;
    bool diagnostics_=false;
    bool confirmUsbStop_=false;
    static bool up(const InputEvent& e,char key) { return e.code==0x52 || key=='w' || key=='k'; }
    static bool down(const InputEvent& e,char key) { return e.code==0x51 || key=='s' || key=='j'; }
    void home() {
        if(screen_==1) SshService::disconnect();
        if(screen_==4) SdStorage::end();
        if(screen_==7) Recorder::end();
        KeyboardManager::setActive(false); screen_=0;
    }
    void enter(uint8_t page) {
        screen_=page;
        KeyboardManager::setActive(page==2);
        if(page==4) SdStorage::begin();
        if(page==7) Recorder::begin();
    }
    void adjust(int direction) {
        if(setting_==0) AppSettings::setBrightness(AppSettings::brightness()+direction*16);
        if(setting_==1) MediaPlayer::setVolume(constrain(AppSettings::volume()+direction*5,0,100));
        if(setting_==2) AppSettings::setVideoFps(AppSettings::videoFps()+direction);
    }
    void drawHome() {
        Ui::header("Cardputer Adv Hub");
        Ui::line(21, "QUICK ACCESS", Ui::muted());
        const char* names[]={"SSH Terminal","Keyboard","IR Remote","SD Storage","Settings","Recorder"};
        const char* marks[]={">_", "KB", "IR", "SD", "CFG", "REC"};
        for(int i=0;i<6;++i) {
            const int column = i % 2;
            const int row = i / 2;
            const int x = column == 0 ? 5 : 122;
            const int y = 34 + row * 28;
            const bool active = i == selected_;
            const uint16_t bg = active ? Ui::selected() : Ui::surface();
            Ui::canvas().fillRoundRect(x, y, 113, 23, 4, bg);
            Ui::canvas().drawRoundRect(x, y, 113, 23, 4, active ? Ui::accent() : Ui::surfaceAlt());
            Ui::canvas().setTextColor(active ? Ui::accent() : Ui::info(), bg);
            Ui::canvas().drawString(marks[i], x + 6, y + 7);
            Ui::canvas().setTextColor(Ui::text(), bg);
            Ui::canvas().drawString(names[i], x + 31, y + 7);
        }
        Ui::footer("W/S: select   Enter: open");
    }
    void drawSsh() {
        Ui::header("SSH Terminal");
        if(SshService::connected()) {
            for(uint8_t row=0;row<12;++row) Ui::line(22+row*8,SshService::terminalRow(row));
        } else if(SshService::wifiReady()) {
            char row[44];
            Ui::line(28,"Wi-Fi connected",Ui::success());
            snprintf(row,sizeof(row),"SSID: %.25s",SshService::wifiSsid()); Ui::line(42,row);
            snprintf(row,sizeof(row),"IP: %.15s",SshService::wifiIp()); Ui::line(56,row);
            snprintf(row,sizeof(row),"GW: %.15s",SshService::wifiGateway()); Ui::line(70,row);
            snprintf(row,sizeof(row),"Mask: %.15s",SshService::wifiSubnet()); Ui::line(84,row);
            snprintf(row,sizeof(row),"DNS: %.15s  RSSI:%ld",SshService::wifiDns(),static_cast<long>(SshService::wifiSignal())); Ui::line(98,row);
            Ui::line(112,"I: SSH login",Ui::warning());
        } else {
            if(SshService::awaitingTrust()) {
                Ui::line(30,SshService::status(),Ui::info());
                char part[33];
                memcpy(part,SshService::fingerprint(),32); part[32]=0; Ui::line(52,part);
                memcpy(part,SshService::fingerprint()+32,32); part[32]=0; Ui::line(64,part);
                Ui::line(88,"T: trust this host and save",Ui::warning());
            } else if(SshService::view()==1) {
                const uint8_t count=SshService::wifiCount();
                if(!count) {
                    Ui::line(32,SshService::status(),Ui::info());
                    Ui::line(58,"C: scan again   H: saved");
                } else {
                    for(uint8_t i=0;i<count && i<8;++i) {
                        const int y=22+i*12;
                        const bool selected=i==SshService::wifiSelected();
                        const uint16_t bg=selected?Ui::selected():Ui::surface();
                        Ui::canvas().fillRoundRect(3,y-1,234,11,2,bg);
                        if (selected) Ui::canvas().drawRoundRect(3,y-1,234,11,2,Ui::accent());
                        char row[40];
                        snprintf(row,sizeof(row),"%c %-21.21s %4ld%c",selected?'>':' ',SshService::wifiName(i),static_cast<long>(SshService::wifiRssi(i)),SshService::wifiSecured(i)?'*':' ');
                        Ui::canvas().setTextColor(Ui::text(),bg); Ui::canvas().drawString(row,6,y);
                    }
                }
            } else if(SshService::view()==2) {
                const uint8_t count=SshService::savedCount();
                if(!count) {
                    Ui::line(32,SshService::status(),Ui::info());
                    Ui::line(58,"C: scan Wi-Fi   D: SD config");
                } else {
                    for(uint8_t i=0;i<count && i<6;++i) {
                        const int y=22+i*12;
                        const bool selected=i==SshService::savedSelected();
                        const uint16_t bg=selected?Ui::selected():Ui::surface();
                        Ui::canvas().fillRoundRect(3,y-1,234,11,2,bg);
                        if (selected) Ui::canvas().drawRoundRect(3,y-1,234,11,2,Ui::accent());
                        char row[48];
                        snprintf(row,sizeof(row),"%c %-15.15s %.19s",selected?'>':' ',SshService::savedSsid(i),SshService::savedTarget(i));
                        Ui::canvas().setTextColor(Ui::text(),bg); Ui::canvas().drawString(row,6,y);
                    }
                }
            } else if(SshService::enteringPassword()) {
                Ui::line(30,SshService::status(),Ui::info());
                char ssid[32]; snprintf(ssid,sizeof(ssid),"Wi-Fi: %.25s",SshService::passwordSsid());
                Ui::line(50,ssid);
                Ui::line(70,"Password:"); Ui::line(88,SshService::passwordDisplay(),Ui::warning());
                Ui::line(104,"Enter: connect  Backspace: edit");
            } else if(SshService::editingSsh()) {
                Ui::line(30,SshService::status(),Ui::info());
                Ui::line(50,SshService::sshFieldName());
                Ui::line(72,SshService::sshEditDisplay(),Ui::warning());
                Ui::line(104,"Enter: next/connect  Backspace: edit");
            } else {
                Ui::line(30,SshService::status(),Ui::info());
                Ui::line(58,"C: scan Wi-Fi   H: saved");
                Ui::line(76,"D: connect SD config");
                Ui::line(94,"I: SSH login   E: edit Wi-Fi");
            }
        }
        Ui::footer("C scan H history Fn+Q home");
    }
    void drawKeyboard() {
        Ui::header("Keyboard");
        if(diagnostics_) {
            Ui::line(30,BleKeyboardService::status(),Ui::info());
            Ui::line(52,BleKeyboardService::diagnostic());
            Ui::line(72,BleKeyboardService::counters());
            Ui::line(96,"Fn+R: clear all BLE pairings");
        } else {
            char text[40]; snprintf(text,sizeof(text),"Send to: %s",KeyboardManager::modeName()); Ui::line(30,text,Ui::info());
            Ui::line(48,UsbKeyboardService::connected()?"USB: connected":"USB: not connected");
            Ui::line(66,BleKeyboardService::status(),KeyboardManager::bleConnected()?Ui::success():Ui::warning());
            Ui::line(86,"Pair: Cardputer Hub N2");
            Ui::line(104,"Fn+M: mode   Fn+D: details");
        }
        Ui::footer("Fn+Q: home   Opt: Win/Cmd");
    }
    void drawIr() {
        Ui::header("IR Remote"); Ui::line(24,IrRemote::profileName(),Ui::info());
        for(uint8_t i=0;i<IrRemote::buttonCount();++i) {
            char text[24]; snprintf(text,sizeof(text),"%u %.10s",i+1,IrRemote::buttonLabel(i));
            Ui::canvas().setTextColor(Ui::text(),Ui::background());
            Ui::canvas().drawString(text,(i%3)*80+5,44+(i/3)*18);
        }
        Ui::line(104,IrRemote::status(),Ui::warning());
        Ui::footer("N: device  R: reload  Fn+Q:home");
    }
    void drawSettings() {
        Ui::header("Settings"); char text[48];
        snprintf(text,sizeof(text),"Brightness   %u / 255",AppSettings::brightness()); Ui::item(0,text,setting_==0);
        snprintf(text,sizeof(text),"Volume       %u %%",AppSettings::volume()); Ui::item(1,text,setting_==1);
        snprintf(text,sizeof(text),"MJPEG speed  %u fps",AppSettings::videoFps()); Ui::item(2,text,setting_==2);
        snprintf(text,sizeof(text),"USB SD sharing  %s >",UsbStorageService::hostActive()?"ON":"OFF"); Ui::item(3,text,setting_==3);
        snprintf(text,sizeof(text),"SD: %s   Free RAM: %uK",Storage::available()?"ready":"absent",ESP.getFreeHeap()/1024); Ui::line(104,text,Ui::info());
        Ui::footer(setting_==3?"Enter:open USB SD  Fn+Q:home":"W/S:select +/-:change Fn+Q:home");
    }
    void drawUsbStorage() {
        Ui::header("USB SD sharing");
        Ui::line(28,UsbStorageService::status(),UsbStorageService::hostActive()?Ui::success():Ui::info());
        if(confirmUsbStop_) {
            Ui::line(50,"Eject SD on computer first",Ui::warning());
            Ui::line(70,"Then Y: stop sharing");
            Ui::line(90,"N: keep sharing");
        } else if(UsbStorageService::hostActive()) {
            Ui::line(50,"Computer can read/write SD");
            Ui::line(70,"SD apps paused while sharing");
            Ui::line(90,"Safely eject on PC to finish");
            Ui::line(108,"Enter: stop sharing");
        } else {
            Ui::line(50,"SD belongs to this device");
            Ui::line(70,"Connect USB, then Enter");
            Ui::line(90,"Enter: enable PC read/write");
            Ui::line(108,"Sharing is OFF after reboot");
        }
        Ui::footer("Fn+Q:home (keeps sharing state)");
    }
};
App* createLauncherApp() { return new LauncherApp(); }
