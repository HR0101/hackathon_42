#pragma once
#include <Arduino.h>
#include "IrDef.h"

/**
 * @brief 可視光 LED の非ブロッキング点滅制御 (IR_LED_PIN = D4)
 *
 * flash() を呼ぶと LED を ON にし、update() を loop() 内で毎回呼ぶことで
 * 指定時間後に自動 OFF する。delay() を使わないためタイミングを乱さない。
 */
class LedCtrl {
public:
    void begin();
    void flash(uint16_t durationMs = 50);
    void update();

private:
    uint32_t _offAt = 0;  // LED を OFF にする millis() 値 (0 = 消灯済み)
};
