#include "Player.h"
#include "FspTimer.h"
#include <math.h>

// ============================================================
//  割り込み（サンプリング ISR）と共有する状態
//  ── FspTimer のコールバックは static 関数のため、ファイルスコープに置く ──
// ============================================================
namespace {

    FspTimer audioTimer;

    /** 1 周期ぶんの波形（-1.0〜+1.0 に正規化）。楽器ごとに作り直す。*/
    float wt[WT_SIZE];

    /** 位相アキュムレータ（0〜WT_SIZE）と 1 サンプルあたりの位相増分 */
    volatile float phase    = 0.0f;
    volatile float phaseInc = 0.0f;

    // ── ADSR 包絡線 ──
    enum AdsrState : uint8_t { A_IDLE, A_ATTACK, A_DECAY, A_SUSTAIN, A_RELEASE };
    volatile AdsrState adsr       = A_IDLE;
    volatile uint32_t  adsrCount  = 0;       ///< 現フェーズ内の経過サンプル数
    volatile uint32_t  atkSamples = 1;       ///< Attack のサンプル数
    volatile uint32_t  decSamples = 1;       ///< Decay のサンプル数
    volatile uint32_t  relSamples = 1;       ///< Release のサンプル数
    volatile float     susLevel   = 0.0f;    ///< Sustain レベル（0〜1）
    volatile float     maxAmp     = 0.5f;    ///< 最大振幅（DAC 飽和回避のため 0.5）
    volatile float     env        = 0.0f;    ///< 現在の包絡線振幅
    volatile float     relStart   = 0.0f;    ///< Release 開始時の振幅

    /** DAC 中央値（無音時の出力）= 12bit の中点 */
    constexpr uint16_t DAC_MID = 2048;

    // --------------------------------------------------------
    //  サンプリング割り込み：ADSR 計算 → ウェーブテーブル読み出し → DAC 出力
    //  AUDIO_SAMPLE_RATE [Hz] 周期で呼ばれる。
    // --------------------------------------------------------
    void audioISR(void*) {
        // ── 1. ADSR 包絡線を 1 サンプルぶん進める ──
        switch (adsr) {
        case A_ATTACK:
            env = maxAmp * ((float)adsrCount / (float)atkSamples);
            if (++adsrCount >= atkSamples) { adsrCount = 0; adsr = A_DECAY; }
            break;
        case A_DECAY: {
            const float p = (float)adsrCount / (float)decSamples;
            env = maxAmp - (maxAmp - maxAmp * susLevel) * p;
            if (++adsrCount >= decSamples) { adsrCount = 0; adsr = A_SUSTAIN; }
            break;
        }
        case A_SUSTAIN:
            env = maxAmp * susLevel;
            break;
        case A_RELEASE: {
            const float p = (float)adsrCount / (float)relSamples;
            env = relStart * (1.0f - p);
            if (++adsrCount >= relSamples || env <= 0.0f) { env = 0.0f; adsr = A_IDLE; }
            break;
        }
        case A_IDLE:
        default:
            env = 0.0f;
            break;
        }

        // ── 2. ウェーブテーブルを位相に従って読み出す ──
        uint16_t out;
        if (adsr != A_IDLE) {
            const uint16_t idx = (uint16_t)phase;
            const float s = wt[idx] * env;            // -0.5〜+0.5
            phase += phaseInc;
            if (phase >= (float)WT_SIZE) phase -= (float)WT_SIZE;
            float v = (s + 1.0f) * 2047.5f;           // 0〜4095 に変換
            if (v < 0.0f)    v = 0.0f;
            if (v > 4095.0f) v = 4095.0f;
            out = (uint16_t)v;
        } else {
            out = DAC_MID;                            // 無音は中央値（ポップ音防止）
        }

        // ── 3. DAC 出力 ──
        analogWrite(AUDIO_PIN, out);
    }

} // namespace

// ============================================================
//  begin() ── DAC とサンプリング割り込みを起動
// ============================================================
void Player::begin() {
    analogWriteResolution(12);
    pinMode(AUDIO_PIN, OUTPUT);
    analogWrite(AUDIO_PIN, DAC_MID);

    _setInstrument(_instId);  // 既定音色のウェーブテーブルを生成

    // 空き GPT タイマーを 1 つ確保し、AUDIO_SAMPLE_RATE で割り込みを起動
    uint8_t type;
    const int8_t ch = FspTimer::get_available_timer(type);
    if (ch >= 0) {
        audioTimer.begin(TIMER_MODE_PERIODIC, type, ch,
                         (float)AUDIO_SAMPLE_RATE, 50.0f, audioISR, nullptr);
        audioTimer.setup_overflow_irq();
        audioTimer.open();
        audioTimer.start();
        Serial.print(F("[Player] ready  pin=A0(DAC)  fs="));
        Serial.print(AUDIO_SAMPLE_RATE); Serial.println(F("Hz  amp=TA7368"));
    } else {
        Serial.println(F("[Player] ERROR: 空きタイマーがありません"));
    }
}

// ============================================================
//  setVoice() ── 機体番号から楽器と輪唱オフセットを選択
// ============================================================
void Player::setVoice(uint8_t slaveId) {
    const VoiceConfig& v = voiceForSlave(slaveId);
    _offsetBeats = v.roundOffsetBeats;
    _instId      = v.instrument;
    _setInstrument(_instId);

    Serial.print(F("[Player] voice: 楽器="));
    Serial.print(INSTRUMENTS[_instId].name);
    Serial.print(F("  輪唱オフセット="));
    Serial.print(_offsetBeats);
    Serial.println(F("拍"));
}

// ============================================================
//  _setInstrument() ── 倍音合成でウェーブテーブルを作り直す + ADSR 設定
// ============================================================
void Player::_setInstrument(uint8_t instId) {
    if (instId >= INST_COUNT) instId = INST_PIANO;
    const Instrument& in = INSTRUMENTS[instId];

    // 倍音振幅の総和（正規化用）
    float sum = 0.0f;
    for (uint8_t k = 0; k < in.numHarmonics; k++) sum += in.harmonics[k];
    if (sum <= 0.0f) sum = 1.0f;

    // 1 周期ぶんの波形を倍音合成（基音 + 整数倍音）
    for (uint16_t i = 0; i < WT_SIZE; i++) {
        const float ph = (float)i / (float)WT_SIZE;   // 0〜1（1 周期）
        float a = 0.0f;
        for (uint8_t k = 1; k <= in.numHarmonics; k++) {
            a += in.harmonics[k - 1] * sinf(2.0f * (float)M_PI * (float)k * ph);
        }
        wt[i] = a / sum;   // -1.0〜+1.0
    }

    // ADSR をサンプル数へ換算（ISR から使う値を更新）
    noInterrupts();
    atkSamples = (uint32_t)(in.attackSec  * (float)AUDIO_SAMPLE_RATE); if (!atkSamples) atkSamples = 1;
    decSamples = (uint32_t)(in.decaySec   * (float)AUDIO_SAMPLE_RATE); if (!decSamples) decSamples = 1;
    relSamples = (uint32_t)(in.releaseSec * (float)AUDIO_SAMPLE_RATE); if (!relSamples) relSamples = 1;
    susLevel   = in.sustainLevel;
    interrupts();
}

// ============================================================
//  発音制御
// ============================================================
void Player::_noteOn(float freq) {
    noInterrupts();
    phaseInc  = freq * (float)WT_SIZE / (float)AUDIO_SAMPLE_RATE;
    phase     = 0.0f;
    adsrCount = 0;
    adsr      = A_ATTACK;
    interrupts();
}

void Player::_noteOff() {
    noInterrupts();
    if (adsr != A_IDLE && adsr != A_RELEASE) {
        relStart  = env;
        adsrCount = 0;
        adsr      = A_RELEASE;
    }
    interrupts();
}

// ============================================================
//  再生制御
// ============================================================
void Player::play(uint16_t bpm) {
    setBpm(bpm);
    _playing      = true;
    _playStartMs  = millis();
    _eventIdx     = 0;
    _loopBase     = 0.0f;
    _noteSounding = false;
    _nextTrigBeat = _offsetBeats + SONG[0].startBeat;

    Serial.print(F("[Player] play BPM=")); Serial.print(bpm);
    Serial.print(F("  楽器=")); Serial.print(INSTRUMENTS[_instId].name);
    Serial.print(F("  遅れ=")); Serial.print(_offsetBeats); Serial.println(F("拍"));
}

void Player::stop() {
    _playing      = false;
    _noteSounding = false;
    _noteOff();
    Serial.println(F("[Player] stop"));
}

void Player::setBpm(uint16_t bpm) {
    if (bpm == 0) return;
    const float newBeatMs = 60000.0f / (float)bpm;

    // 再生中にテンポを変えても演奏位置が飛ばないよう時刻基準を取り直す
    if (_playing) {
        const uint32_t now  = millis();
        const float    beat = _elapsedBeats(now);
        _playStartMs = now - (uint32_t)(beat * newBeatMs);
    }
    _beatMs = newBeatMs;
    _bpm    = bpm;
}

// ============================================================
//  sync() ── SYNC 受信時の位相ドリフト補正
// ============================================================
void Player::sync(uint8_t beatCount) {
    (void)beatCount;
    if (!_playing) return;

    const uint32_t now  = millis();
    const float    beat = _elapsedBeats(now);

    // 最寄りの整数拍からのズレ（-0.5〜+0.5 拍）
    const float frac = beat - roundf(beat);
    // ズレを ms に直し、SYNC 固有の赤外線伝送遅延ぶんを差し引く
    int32_t errMs = (int32_t)(frac * _beatMs) - IR_LATENCY_MS;

    if (errMs > SYNC_CORRECT_MS || errMs < -SYNC_CORRECT_MS) {
        // 進み過ぎ/遅れ過ぎを打ち消す向きに時刻基準をずらす
        _playStartMs = (uint32_t)((int32_t)_playStartMs + errMs);
        Serial.print(F("[SYNC] 補正 ")); Serial.print(errMs); Serial.println(F("ms"));
    }
}

// ============================================================
//  update() ── 拍に従って音符を発火する（loop から毎回）
// ============================================================
void Player::update() {
    if (!_playing) return;

    const uint32_t now  = millis();
    const float    beat = _elapsedBeats(now);

    // 現在の音符を音符長ぶん鳴らしたら消す（スタッカート/余韻へ）
    if (_noteSounding && beat >= _noteOffBeat) {
        _noteOff();
        _noteSounding = false;
    }

    // 発火タイミングに達した音符を順に鳴らす（通常 1 回 / 拍未満）
    while (beat >= _nextTrigBeat) {
        const Note& n = SONG[_eventIdx];
        if (n.freq > 0.0f) {                  // 休符でなければ発音
            _noteOn(n.freq);
            _noteSounding = true;
            _noteOffBeat  = _nextTrigBeat + n.durBeat;
        }

        // 次の音符へ。末尾まで来たらループ先頭へ戻る（輪唱の周回）
        if (++_eventIdx >= SONG_LEN) {
            _eventIdx  = 0;
            _loopBase += SONG_LEN_BEATS;
        }
        _nextTrigBeat = _offsetBeats + _loopBase + SONG[_eventIdx].startBeat;
    }
}

// ============================================================
//  内部ヘルパ
// ============================================================
float Player::_elapsedBeats(uint32_t now) const {
    return (float)(now - _playStartMs) / _beatMs;
}
