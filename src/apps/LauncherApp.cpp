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
#include <algorithm>

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
            if(up(e,key)) selected_ = selected_ >= 2 ? selected_ - 2 : selected_ + 4;
            else if(down(e,key)) selected_ = selected_ < 4 ? selected_ + 2 : selected_ - 4;
            else if(left(e,key) && selected_ % 2) --selected_;
            else if(right(e,key) && !(selected_ % 2) && selected_ + 1 < 6) ++selected_;
            else if(e.key=='\n' && !e.repeat) { const uint8_t pages[] = {1,2,3,4,7,5}; enter(pages[selected_]); }
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
            if(key>='1' && key<='9' && key-'1'<IrRemote::buttonCount()) { irButton_=key-'1'; IrRemote::sendButton(irButton_); }
            if(key=='n') { IrRemote::nextProfile(); irButton_=255; }
            if(key=='b') { IrRemote::previousProfile(); irButton_=255; }
            if(key=='r') { IrRemote::loadProfile(); irButton_=255; }
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
    uint8_t screen_=0,selected_=0,setting_=0,irButton_=255;
    bool diagnostics_=false;
    bool confirmUsbStop_=false;
    static bool up(const InputEvent& e,char key) { return e.code==0x52 || key=='w' || key=='k'; }
    static bool down(const InputEvent& e,char key) { return e.code==0x51 || key=='s' || key=='j'; }
    static bool left(const InputEvent& e,char key) { return e.code==0x50 || key=='a'; }
    static bool right(const InputEvent& e,char key) { return e.code==0x4f || key=='d'; }
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
        Ui::header("Cardputer Hub");
        const char* names[]={"SSH Terminal","Keyboard","IR Remote","SD Storage","Recorder","Settings"};
        const Ui::Icon marks[]={Ui::Icon::Terminal,Ui::Icon::Keyboard,Ui::Icon::Infrared,
            Ui::Icon::Storage,Ui::Icon::Recorder,Ui::Icon::Settings};
        for(int i=0;i<6;++i) {
            const int x=5+(i%2)*117, y=24+(i/2)*32;
            const bool active=i==selected_;
            const uint16_t bg=active?Ui::selected():Ui::surface();
            const uint16_t fg=active?Ui::onSelected():Ui::text();
            Ui::card(x,y,113,28,active);
            Ui::icon(marks[i],x+6,y+6,fg);
            Ui::label(x+28,y+10,80,names[i],fg,bg);
        }
        Ui::footer("Arrows:move  Enter:open");
    }
    void drawSsh() {
        Ui::header("SSH Terminal",Ui::Icon::Terminal);
        if(SshService::connected()) {
            // PTY is exactly 38 columns: reserve 228 pixels, without ellipsis.
            for(uint8_t row=0;row<12;++row)
                Ui::label(6,22+row*8,228,SshService::terminalRow(row),Ui::text(),Ui::background());
            Ui::footer("Fn+Q:home"); return;
        }
        if(SshService::awaitingTrust()) {
            Ui::line(25,"Verify server fingerprint",Ui::warning());
            Ui::panel(5,40,230,39);
            char part[33];
            memcpy(part,SshService::fingerprint(),32); part[32]=0;
            Ui::label(10,47,220,part,Ui::text(),Ui::surface());
            memcpy(part,SshService::fingerprint()+32,32); part[32]=0;
            Ui::label(10,63,220,part,Ui::text(),Ui::surface());
            Ui::line(88,SshService::status(),Ui::muted());
            Ui::line(107,"Trust only a known server",Ui::warning());
            Ui::footer("T:trust & save  Fn+Q:cancel"); return;
        }
        if(SshService::enteringPassword() || SshService::editingSsh()) {
            const bool wifi=SshService::enteringPassword();
            Ui::header(wifi?"Wi-Fi password":"SSH Login",Ui::Icon::Terminal);
            Ui::line(26,wifi?SshService::passwordSsid():SshService::status(),Ui::muted());
            Ui::line(46,wifi?"Password":SshService::sshFieldName());
            Ui::card(6,60,228,30);
            Ui::canvas().drawRoundRect(6,60,228,30,3,Ui::accent());
            const char* value=wifi?SshService::passwordDisplay():SshService::sshEditDisplay();
            const size_t length=strlen(value);
            Ui::label(12,71,216,value+(length>36?length-36:0),Ui::text(),Ui::surface());
            Ui::line(102,SshService::status(),Ui::warning());
            Ui::footer(wifi?"Enter:join BS:edit Esc:cancel":"Enter:next BS:edit Esc:cancel"); return;
        }
        if(SshService::view()==1) {
            Ui::header("Wi-Fi scan");
            const uint8_t count=SshService::wifiCount(), current=SshService::wifiSelected();
            if(!count) {
                Ui::line(35,SshService::status(),Ui::warning());
                Ui::line(60,"C:scan again  H:saved");
            } else {
                constexpr uint8_t visible=5;
                const uint8_t start=count<=visible?0:std::min<int>(std::max<int>(0,current-2),count-visible);
                char summary[40]; snprintf(summary,sizeof(summary),"Networks  %u/%u",current+1,count);
                Ui::line(23,summary,Ui::muted());
                for(uint8_t row=0;row<visible && start+row<count;++row) {
                    const uint8_t i=start+row; const int y=36+row*16;
                    const bool active=i==current;
                    const uint16_t bg=active?Ui::selected():Ui::surface(), fg=active?Ui::onSelected():Ui::text();
                    Ui::card(5,y,227,14,active);
                    Ui::label(10,y+3,156,SshService::wifiName(i),fg,bg);
                    Ui::label(170,y+3,6,SshService::wifiSecured(i)?"*":"",fg,bg);
                    char signal[12]; snprintf(signal,sizeof(signal),"%ld",static_cast<long>(SshService::wifiRssi(i)));
                    Ui::label(184,y+3,42,signal,fg,bg);
                }
                Ui::scrollBar(236,36,78,start,visible,count);
            }
            Ui::footer("W/S:select Enter:join C:scan H:saved"); return;
        }
        if(SshService::view()==2) {
            Ui::header("Saved connections",Ui::Icon::Terminal);
            const uint8_t count=SshService::savedCount();
            if(!count) {
                Ui::line(34,SshService::status(),Ui::muted());
                Ui::line(58,"C:scan Wi-Fi  D:SD config");
            } else {
                for(uint8_t i=0;i<count && i<6;++i) {
                    const int y=24+i*15; const bool active=i==SshService::savedSelected();
                    const uint16_t bg=active?Ui::selected():Ui::surface(),fg=active?Ui::onSelected():Ui::text();
                    Ui::card(5,y,230,13,active);
                    Ui::label(10,y+3,91,SshService::savedSsid(i),fg,bg);
                    Ui::label(109,y+3,120,SshService::savedTarget(i),fg,bg);
                }
            }
            Ui::footer("W/S:select Enter:join C:scan"); return;
        }
        if(SshService::wifiReady()) {
            Ui::header("Wi-Fi connected");
            Ui::canvas().fillCircle(11,27,3,Ui::success());
            char summary[40];
            snprintf(summary,sizeof(summary),"CONNECTED    RSSI %ld",static_cast<long>(SshService::wifiSignal()));
            Ui::label(20,23,212,summary,Ui::text(),Ui::background());
            Ui::panel(5,36,230,60);
            const char* labels[]={"SSID","IP","GW","MASK","DNS"};
            const char* values[]={SshService::wifiSsid(),SshService::wifiIp(),SshService::wifiGateway(),
                SshService::wifiSubnet(),SshService::wifiDns()};
            for(int i=0;i<5;++i) {
                Ui::label(11,39+i*11,30,labels[i],Ui::muted(),Ui::surface());
                Ui::label(47,39+i*11,182,values[i],Ui::text(),Ui::surface());
            }
            Ui::line(98,SshService::status(),Ui::muted());
            Ui::card(6,108,228,12,true);
            Ui::label(78,110,150,"I: SSH login",Ui::onSelected(),Ui::selected());
            Ui::footer("C:scan H:saved E:password Fn+Q:home"); return;
        }
        Ui::line(26,SshService::status(),Ui::warning());
        Ui::card(6,45,228,22,true);
        Ui::label(14,52,212,"C: Scan Wi-Fi",Ui::onSelected(),Ui::selected());
        Ui::card(6,72,228,20);
        Ui::label(14,78,212,"H: Saved connections",Ui::text(),Ui::surface());
        Ui::line(104,"D:SD config  I:SSH login",Ui::muted());
        Ui::footer("Fn+Q:home");
    }
    void drawKeyboard() {
        Ui::header("Keyboard",Ui::Icon::Keyboard);
        if(diagnostics_) {
            Ui::line(26,BleKeyboardService::status(),Ui::info());
            Ui::line(46,BleKeyboardService::diagnostic());
            Ui::line(64,BleKeyboardService::counters());
            Ui::line(91,"Fn+R: clear all BLE pairings",Ui::warning());
            Ui::footer("Fn+D:back  Fn+Q:home");
        } else {
            Ui::line(23,"SEND TO",Ui::muted());
            Ui::card(6,35,228,22,true);
            Ui::label(13,42,214,KeyboardManager::modeName(),Ui::onSelected(),Ui::selected());
            Ui::card(6,62,228,38);
            Ui::canvas().fillCircle(13,72,2,UsbKeyboardService::connected()?Ui::success():Ui::muted());
            Ui::label(22,68,205,UsbKeyboardService::connected()?"USB: connected":"USB: not connected",Ui::text(),Ui::surface());
            Ui::canvas().fillCircle(13,89,2,KeyboardManager::bleConnected()?Ui::success():Ui::muted());
            Ui::label(22,85,205,BleKeyboardService::status(),Ui::text(),Ui::surface());
            Ui::line(108,"Pair: Cardputer Hub N2",Ui::muted());
            Ui::footer("Fn+M:mode Fn+D:info Fn+Q:home");
        }
    }
    void drawIr() {
        Ui::header("IR Remote",Ui::Icon::Infrared);
        char device[40];
        snprintf(device,sizeof(device),"%u/%u  %s",IrRemote::profileIndex()+1,IrRemote::profileCount(),IrRemote::profileName());
        Ui::line(23,device);
        char protocol[40];
        if(strcmp(IrRemote::protocolName(),"RAW")==0)
            snprintf(protocol,sizeof(protocol),"RAW  %ukHz",IrRemote::profileFrequency());
        else snprintf(protocol,sizeof(protocol),"%s  Addr:0x%X",IrRemote::protocolName(),IrRemote::profileAddress());
        Ui::line(35,protocol,Ui::muted());
        for(uint8_t i=0;i<IrRemote::buttonCount();++i) {
            const int x=5+(i%3)*78,y=48+(i/3)*19;
            const bool active=i==irButton_;
            Ui::card(x,y,74,16,active);
            char label[24]; snprintf(label,sizeof(label),"%u %s",i+1,IrRemote::buttonLabel(i));
            Ui::label(x+4,y+4,66,label,active?Ui::onSelected():Ui::text(),active?Ui::selected():Ui::surface());
        }
        Ui::line(109,IrRemote::status(),Ui::warning());
        Ui::footer("1-9:send B/N:device R:reload Fn+Q:home");
    }
    void drawSettings() {
        Ui::header("Settings",Ui::Icon::Settings);
        const char* labels[]={"Brightness","Volume","Video FPS","USB SD sharing"};
        char values[4][12];
        snprintf(values[0],sizeof(values[0]),"%u%%",AppSettings::brightness()*100/255);
        snprintf(values[1],sizeof(values[1]),"%u%%",AppSettings::volume());
        snprintf(values[2],sizeof(values[2]),"%u",AppSettings::videoFps());
        snprintf(values[3],sizeof(values[3]),"%s >",UsbStorageService::hostActive()?"ON":"OFF");
        for(int i=0;i<4;++i) {
            const int y=24+i*21; const bool active=i==setting_;
            const uint16_t bg=active?Ui::selected():Ui::surface(),fg=active?Ui::onSelected():Ui::text();
            Ui::card(5,y,230,18,active);
            Ui::label(11,y+5,110,labels[i],fg,bg);
            if(i==0) Ui::progress(126,y+7,58,AppSettings::brightness()*100/255,active?fg:Ui::accent());
            Ui::label(195,y+5,36,values[i],fg,bg);
        }
        char status[40]; snprintf(status,sizeof(status),"SD:%s  RAM:%uK",Storage::available()?"ready":"absent",ESP.getFreeHeap()/1024);
        Ui::line(110,status,Ui::muted());
        Ui::footer(setting_==3?"Enter:USB sharing  Fn+Q:home":"W/S:select +/-:change Fn+Q:home");
    }
    void drawUsbStorage() {
        Ui::header("USB SD sharing",Ui::Icon::Usb);
        if(confirmUsbStop_) {
            Ui::line(30,"Eject SD on computer first",Ui::warning());
            Ui::panel(6,47,228,44);
            Ui::label(13,55,214,"Y: Stop sharing",Ui::text(),Ui::surface());
            Ui::label(13,74,214,"N: Keep sharing",Ui::text(),Ui::surface());
            Ui::line(104,"Wait for PC writes to finish",Ui::muted());
            Ui::footer("Y:stop  N:cancel  Fn+Q:home"); return;
        }
        const bool sharing=UsbStorageService::hostActive();
        Ui::card(76,25,88,29,true);
        Ui::label(sharing?102:96,32,62,sharing?"ON":"OFF",Ui::onSelected(),Ui::selected(),2);
        Ui::line(61,UsbStorageService::status(),sharing?Ui::success():Ui::muted());
        Ui::line(75,sharing?"Computer owns SD":"Device owns SD");
        Ui::line(89,sharing?"Safely eject on PC to finish":"Connect USB to share files",Ui::muted());
        Ui::card(6,103,228,16,true);
        Ui::label(14,107,212,sharing?"Enter: stop sharing":"Enter: share with PC",Ui::onSelected(),Ui::selected());
        Ui::footer("Fn+Q:home (keeps sharing state)");
    }
};
App* createLauncherApp() { return new LauncherApp(); }
