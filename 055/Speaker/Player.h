#ifndef PLAYER_H
#define PLAYER_H

#include <Arduino.h>

class Player {
public:
    Player();
    void begin();
    
    // ★ 楽譜自動走査のupdate()は不要になりますが、一応残しておきます
    void update() {} 

    // ★ 外部（赤外線コマンド受信部）から直接この2つを呼び出す形に変えます
    void noteOn(float freq);
    void noteOff();

private:
    static void callback_audio_out(void* args);
};

#endif // PLAYER_H