#include "app/App.h"
#include "core/Ui.h"
#include "core/Storage.h"
#include "core/AppSettings.h"
#include "core/KeyboardManager.h"
#include "core/UsbKeyboardService.h"
#include "core/BleKeyboardService.h"
#include "core/IrRemote.h"
#include "core/SshService.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include <cstring>
#include <cctype>

class LauncherApp final : public App {
public:
    const char* title() const override { return "Cardputer Adv Hub"; }
    void update() override {}
    void onInput(const InputEvent& e) override {
        if(e.type!=InputType::Key) return;
        const char key=static_cast<char>(tolower(static_cast<unsigned char>(e.key)));
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
        if(screen_==0) {
            if(up(e,key)) selected_=(selected_+4)%5;
            else if(down(e,key)) selected_=(selected_+1)%5;
            else if(e.key=='\n' && !e.repeat) enter(selected_+1);
            return;
        }
        if((key=='q' || e.key=='\b') && !e.repeat) { home(); return; }
        if(screen_==1) { if(key=='c' && !e.repeat) SshService::connect(); return; }
        if(screen_==3 && !e.repeat) {
            if(key>='1' && key<='9') IrRemote::sendButton(key-'1');
            if(key=='n') IrRemote::nextProfile();
            if(key=='r') IrRemote::loadProfile();
        } else if(screen_==4) {
            if(key=='v' && !e.repeat) {
                MediaPlayer::stop(); VideoPlayer::stop(); videoMode_=!videoMode_;
            } else if(key=='r' && !e.repeat) {
                MediaPlayer::stop(); VideoPlayer::stop(); Storage::refresh();
            } else if(key=='p' && !e.repeat) {
                if(videoMode_) VideoPlayer::toggle(); else MediaPlayer::toggle();
            } else if((key=='n' || e.code==0x4f) && !e.repeat) {
                if(videoMode_) VideoPlayer::next(); else MediaPlayer::next();
            } else if((key=='b' || e.code==0x50) && !e.repeat) {
                if(videoMode_) VideoPlayer::previous(); else MediaPlayer::previous();
            } else if(!videoMode_ && (e.key=='+' || e.key=='=')) MediaPlayer::setVolume(std::min(100,MediaPlayer::volume()+5));
            else if(!videoMode_ && e.key=='-') MediaPlayer::setVolume(std::max(0,MediaPlayer::volume()-5));
        } else if(screen_==5) {
            if(up(e,key)) setting_=(setting_+2)%3;
            else if(down(e,key)) setting_=(setting_+1)%3;
            else if(e.key=='+' || e.key=='=' || e.code==0x4f) adjust(1);
            else if(e.key=='-' || e.code==0x50) adjust(-1);
            else if(key=='r' && !e.repeat) Storage::refresh();
        }
    }
    void draw() override {
        Ui::clear();
        switch(screen_) {
        case 0: drawHome(); break;
        case 1: drawSsh(); break;
        case 2: drawKeyboard(); break;
        case 3: drawIr(); break;
        case 4: drawMedia(); break;
        case 5: drawSettings(); break;
        }
        Ui::present();
    }
private:
    uint8_t screen_=0,selected_=0,setting_=0;
    bool videoMode_=false,diagnostics_=false;
    static bool up(const InputEvent& e,char key) { return e.code==0x52 || key=='w' || key=='k'; }
    static bool down(const InputEvent& e,char key) { return e.code==0x51 || key=='s' || key=='j'; }
    void home() {
        if(screen_==1) SshService::disconnect();
        if(screen_==4) { MediaPlayer::stop(); VideoPlayer::stop(); }
        KeyboardManager::setActive(false); screen_=0;
    }
    void enter(uint8_t page) {
        screen_=page;
        KeyboardManager::setActive(page==2);
        if(page==4) Storage::refresh();
    }
    void adjust(int direction) {
        if(setting_==0) AppSettings::setBrightness(AppSettings::brightness()+direction*16);
        if(setting_==1) MediaPlayer::setVolume(constrain(AppSettings::volume()+direction*5,0,100));
        if(setting_==2) AppSettings::setVideoFps(AppSettings::videoFps()+direction);
    }
    void drawHome() {
        Ui::header("Cardputer Adv Hub");
        const char* names[]={"SSH Terminal","Keyboard","IR Remote","Media Player","Settings"};
        for(int i=0;i<5;++i) Ui::item(i,names[i],i==selected_);
        Ui::footer("W/S: select   Enter: open");
    }
    void drawSsh() {
        Ui::header("SSH Terminal");
        if(SshService::connected()) {
            for(uint8_t row=0;row<12;++row) Ui::line(22+row*8,SshService::terminalRow(row));
        } else {
            Ui::line(30,SshService::status(),TFT_CYAN);
            if(SshService::awaitingTrust()) {
                char part[33];
                memcpy(part,SshService::fingerprint(),32); part[32]=0; Ui::line(52,part);
                memcpy(part,SshService::fingerprint()+32,32); part[32]=0; Ui::line(64,part);
                Ui::line(88,"T: trust this host and save",TFT_YELLOW);
            } else {
                Ui::line(58,"C: connect   Fn+Q: cancel");
                Ui::line(82,"SD: /config/wifi.json"); Ui::line(96,"    /config/ssh.json");
            }
        }
        Ui::footer("Fn+Q: disconnect / home");
    }
    void drawKeyboard() {
        Ui::header("Keyboard");
        if(diagnostics_) {
            Ui::line(30,BleKeyboardService::status(),TFT_CYAN);
            Ui::line(52,BleKeyboardService::diagnostic());
            Ui::line(72,BleKeyboardService::counters());
            Ui::line(96,"Fn+R: clear all BLE pairings");
        } else {
            char text[40]; snprintf(text,sizeof(text),"Send to: %s",KeyboardManager::modeName()); Ui::line(30,text,TFT_CYAN);
            Ui::line(48,UsbKeyboardService::connected()?"USB: connected":"USB: not connected");
            Ui::line(66,BleKeyboardService::status(),KeyboardManager::bleConnected()?TFT_GREEN:TFT_YELLOW);
            Ui::line(86,"Pair: Cardputer Hub N2");
            Ui::line(104,"Fn+M: mode   Fn+D: details");
        }
        Ui::footer("Fn+Q: home   Opt: Win/Cmd");
    }
    void drawIr() {
        Ui::header("IR Remote"); Ui::line(24,IrRemote::profileName(),TFT_CYAN);
        for(uint8_t i=0;i<IrRemote::buttonCount();++i) {
            char text[24]; snprintf(text,sizeof(text),"%u %.10s",i+1,IrRemote::buttonLabel(i));
            Ui::canvas().setTextColor(TFT_WHITE,TFT_BLACK);
            Ui::canvas().drawString(text,(i%3)*80+5,44+(i/3)*18);
        }
        Ui::line(104,IrRemote::status(),TFT_YELLOW);
        Ui::footer("N: device  R: reload  Fn+Q:home");
    }
    void drawMedia() {
        Ui::header(videoMode_?"Video Player":"Music Player");
        if(!Storage::available()) {
            Ui::line(38,"Insert FAT32 SD and reboot",TFT_YELLOW);
            Ui::line(64,"/music: MP3 WAV"); Ui::line(84,"/video: JPEG MJPEG");
        } else if(videoMode_) {
            VideoPlayer::render();
            if(!VideoPlayer::isPlaying()) Ui::line(104,VideoPlayer::status(),TFT_CYAN);
        } else {
            char text[48];
            snprintf(text,sizeof(text),"Track %u / %u",Storage::mediaCount("/music")?MediaPlayer::index()+1:0,Storage::mediaCount("/music"));
            Ui::line(28,text,TFT_CYAN); Ui::line(48,MediaPlayer::currentName());
            Ui::line(66,MediaPlayer::status(),TFT_GREEN);
            const uint32_t sec=MediaPlayer::elapsedSeconds();
            snprintf(text,sizeof(text),"%lu:%02lu  Vol:%u%%  File:%u%%",static_cast<unsigned long>(sec/60),static_cast<unsigned long>(sec%60),MediaPlayer::volume(),MediaPlayer::progress());
            Ui::line(84,text);
            Ui::canvas().drawRect(8,104,224,5,TFT_DARKGREY);
            Ui::canvas().fillRect(9,105,222*MediaPlayer::progress()/100,3,TFT_CYAN);
        }
        Ui::footer("P:play N/B:skip V:mode Fn+Q:home");
    }
    void drawSettings() {
        Ui::header("Settings"); char text[48];
        snprintf(text,sizeof(text),"Brightness   %u / 255",AppSettings::brightness()); Ui::item(0,text,setting_==0);
        snprintf(text,sizeof(text),"Volume       %u %%",AppSettings::volume()); Ui::item(1,text,setting_==1);
        snprintf(text,sizeof(text),"MJPEG speed  %u fps",AppSettings::videoFps()); Ui::item(2,text,setting_==2);
        snprintf(text,sizeof(text),"SD: %s   Free RAM: %uK",Storage::available()?"ready":"absent",ESP.getFreeHeap()/1024); Ui::line(86,text,TFT_CYAN);
        Ui::line(104,"Changes saved automatically");
        Ui::footer("W/S:select +/-:change Fn+Q:home");
    }
};
App* createLauncherApp() { return new LauncherApp(); }
