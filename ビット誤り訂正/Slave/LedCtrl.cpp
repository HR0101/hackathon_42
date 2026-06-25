#include "LedCtrl.h"

void LedCtrl::begin() {
    pinMode(IR_LED_PIN, OUTPUT);
    digitalWrite(IR_LED_PIN, LOW);
}

void LedCtrl::flash(uint16_t durationMs) {
    digitalWrite(IR_LED_PIN, HIGH);
    _offAt = millis() + durationMs;
}

void LedCtrl::update() {
    if (_offAt && millis() >= _offAt) {
        digitalWrite(IR_LED_PIN, LOW);
        _offAt = 0;
    }
}
