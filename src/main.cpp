#include <M5Cardputer.h>
#include "app/App.h"
#include "core/InputManager.h"
#include "core/Ui.h"
#include "core/Storage.h"
#include "media/MediaPlayer.h"
#include "media/VideoPlayer.h"
#include "core/IrRemote.h"
#include "core/KeyboardManager.h"
#include "core/SshService.h"
#include "core/BleKeyboardService.h"
#include "core/AppSettings.h"
#include "core/UsbStorageService.h"

App* createLauncherApp();
// LibSSH-ESP32's upstream client uses a 51200-byte task stack for crypto.
SET_LOOP_TASK_STACK_SIZE(51200);

InputManager input;
App* currentApp = nullptr;
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    Ui::begin();
    AppSettings::begin();
    input.begin();
    Storage::begin();
    UsbStorageService::begin();
    MediaPlayer::begin();
    VideoPlayer::begin();
    IrRemote::begin();
    KeyboardManager::begin();
    SshService::begin();
    currentApp = createLauncherApp();
    currentApp->begin();
    currentApp->draw();
    Serial.printf("[Hub] ready; free heap: %u\n", ESP.getFreeHeap());
}

void loop() {
    M5Cardputer.update();
    UsbStorageService::loop();
    SshService::loop();
    MediaPlayer::loop();
    VideoPlayer::loop();
    const InputEvent event = input.poll();
    currentApp->update();
    const bool sshDirty = SshService::dirty();
    const bool videoDirty = VideoPlayer::dirty();
    const bool bleDirty = BleKeyboardService::dirty();
    const bool mediaDirty = MediaPlayer::dirty();
    const bool storageDirty = UsbStorageService::dirty();
    if (event.type != InputType::None) {
        currentApp->onInput(event);
        currentApp->draw();
    } else if (sshDirty || videoDirty || bleDirty || mediaDirty || storageDirty) {
        currentApp->draw();
    }
    KeyboardManager::update();
    AppSettings::loop();
    delay(5);
}
