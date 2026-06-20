#include "Player.h"
#include "FspTimer.h"
#include "constants.h"
#include <math.h>

namespace {
    FspTimer audioTimer;
    uint16_t waveBuf[NUM_SAMPLE];
    volatile uint32_t currentSampleIndex = 0;

    enum AdsrState { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    volatile AdsrState adsrState = IDLE;

    volatile uint32_t adsrSampleCount = 0;
    volatile uint32_t attackSamples = 0;
    volatile uint32_t decaySamples = 0;
    volatile uint32_t releaseSamples = 0;

    volatile float currentEnvelopeAmp = 0.0f;
    volatile float releaseStartAmp = 0.0f;

    // .pde から完全移植したADSRパラメータ
    const float ATTACK_TIME_SEC  = 0.005f; 
    const float DECAY_TIME_SEC   = 0.50f;  
    const float SUSTAIN_LEVEL    = 0.08f;  
    const float RELEASE_TIME_SEC = 0.15f;  
    const float MAX_AMP          = 0.5f;   

    // .pde から完全移植した10倍音構造
    const float HARM_WEIGHTS[] = { 1.00f, 0.60f, 0.35f, 0.20f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f };
    const float WEIGHTS_SUM = 2.46f; 

    const uint8_t AUDIO_OUT_PIN = A0;

    void generatePianoWaveform(float freq) {
        for (int i = 0; i < NUM_SAMPLE; i++) {
            float t = (float)i / (float)SAMPLE_RATE;
            float sum = 0.0f;
            for (int k = 1; k <= 10; k++) {
                sum += HARM_WEIGHTS[k-1] * sinf(2.0f * M_PI * (freq * (float)k) * t);
            }
            float normalized = sum / WEIGHTS_SUM; 
            waveBuf[i] = (uint16_t)((normalized + 1.0f) * 2047.5f); 
        }
    }
}

Player::Player() {}

void Player::begin() {
    analogWriteResolution(12);
    pinMode(AUDIO_OUT_PIN, OUTPUT);
    
    attackSamples  = (uint32_t)(ATTACK_TIME_SEC * (float)SAMPLE_RATE);
    decaySamples   = (uint32_t)(DECAY_TIME_SEC * (float)SAMPLE_RATE);
    releaseSamples = (uint32_t)(RELEASE_TIME_SEC * (float)SAMPLE_RATE);

    uint8_t type;
    int8_t ch = FspTimer::get_available_timer(type);
    if(ch >= 0) {
        audioTimer.begin(TIMER_MODE_PERIODIC, type, ch, SAMPLE_RATE, 50.0f, callback_audio_out, nullptr);
        audioTimer.setup_overflow_irq();
        audioTimer.open();
        audioTimer.start();
    }
}

// ★ PCからNoteON命令が届いたら呼び出される関数
void Player::noteOn(float freq) {
    noInterrupts();
    generatePianoWaveform(freq); 
    currentSampleIndex = 0;
    adsrSampleCount = 0;
    adsrState = ATTACK;          
    interrupts();
}

// ★ PCからNoteOFF命令が届いたら呼び出される関数
void Player::noteOff() {
    noInterrupts();
    if (adsrState != IDLE && adsrState != RELEASE) {
        releaseStartAmp = currentEnvelopeAmp;
        adsrSampleCount = 0;
        adsrState = RELEASE;     
    }
    interrupts();
}

// 高精度なADSR計算とオーディオ出力（割り込み）
void Player::callback_audio_out(void*) {
    float outputSampleValue = 0.0f;

    switch (adsrState) {
        case ATTACK:
            currentEnvelopeAmp = MAX_AMP * ((float)adsrSampleCount / (float)attackSamples);
            adsrSampleCount++;
            if (adsrSampleCount >= attackSamples) { adsrSampleCount = 0; adsrState = DECAY; }
            break;
        case DECAY: {
            float progress = (float)adsrSampleCount / (float)decaySamples;
            float sustainAmp = MAX_AMP * SUSTAIN_LEVEL;
            currentEnvelopeAmp = MAX_AMP - (MAX_AMP - sustainAmp) * progress;
            adsrSampleCount++;
            if (adsrSampleCount >= decaySamples) { adsrSampleCount = 0; adsrState = SUSTAIN; }
            break;
        }
        case SUSTAIN:
            currentEnvelopeAmp = MAX_AMP * SUSTAIN_LEVEL;
            break;
        case RELEASE: {
            float progress = (float)adsrSampleCount / (float)releaseSamples;
            currentEnvelopeAmp = releaseStartAmp * (1.0f - progress);
            adsrSampleCount++;
            if (adsrSampleCount >= releaseSamples || currentEnvelopeAmp <= 0.0f) {
                currentEnvelopeAmp = 0.0f;
                adsrState = IDLE;
            }
            break;
        }
        case IDLE:
        default:
            currentEnvelopeAmp = 0.0f;
            break;
    }

    if (adsrState != IDLE) {
        float normalizedSample = ((float)waveBuf[currentSampleIndex] / 2047.5f) - 1.0f;
        outputSampleValue = (normalizedSample * currentEnvelopeAmp + 1.0f) * 2047.5f;
        currentSampleIndex = (currentSampleIndex + 1) % NUM_SAMPLE;
    } else {
        outputSampleValue = 2047.5f; 
    }

    analogWrite(AUDIO_OUT_PIN, (uint16_t)constrain(outputSampleValue, 0.0f, 4095.0f));
}