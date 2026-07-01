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

    /** ノイズ混合比（0.0=トーンのみ / 1.0=ノイズのみ）。スネアは 0.7 等。*/
    volatile float noiseMix = 0.0f;
    /** ノイズ生成用の乱数状態（xorshift32）*/
    volatile uint32_t rng = 0x12345678UL;

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
    void audioISR(timer_callback_args_t*) {
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

        // ── 2. 音源を読み出す（ウェーブテーブルとノイズを noiseMix で混合）──
        uint16_t out;
        if (adsr != A_IDLE) {
            // トーン成分（倍音合成ウェーブテーブル）
            const uint16_t idx = (uint16_t)phase;
            const float tone = wt[idx];
            phase += phaseInc;
            if (phase >= (float)WT_SIZE) phase -= (float)WT_SIZE;

            // ノイズ成分（xorshift32 ホワイトノイズ）
            float sample;
            if (noiseMix > 0.0f) {
                uint32_t x = rng;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                rng = x;
                const float noise = ((float)(x & 0xFFFFUL) / 32768.0f) - 1.0f;  // -1〜+1
                sample = noiseMix * noise + (1.0f - noiseMix) * tone;
            } else {
                sample = tone;
            }

            float v = (sample * env + 1.0f) * 2047.5f;  // 0〜4095 に変換
            if (v < 0.0f)    v = 0.0f;
            if (v > 4095.0f) v = 4095.0f;
            out = (uint16_t)v;
        } else {
            out = DAC_MID;                            // 無音は中央値（ポップ音防止）
        }

        // ── 3. DAC 出力（A0 の analogWrite は内部的に R_DAC_Write 直結なので ISR 安全）──
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
        bool ok = audioTimer.begin(TIMER_MODE_PERIODIC, type, ch,
                                   (float)AUDIO_SAMPLE_RATE, 0.0f, audioISR, nullptr);
        audioTimer.setup_overflow_irq();
        audioTimer.open();
        audioTimer.start();
        Serial.print(F("[Player] timer type=")); Serial.print(type);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" begin=")); Serial.println(ok ? F("OK") : F("FAIL"));
        Serial.print(F("[Player] ready  pin=A0(DAC)  fs="));
        Serial.print(AUDIO_SAMPLE_RATE); Serial.println(F("Hz"));
    } else {
        Serial.println(F("[Player] ERROR: 空きタイマーなし"));
    }
}

// ============================================================
//  setVoice() ── 機体番号から役割・楽器・リズムパターンを選択
//               （輪唱の入りタイミングは Master 管理）
// ============================================================
void Player::setVoice(uint8_t slaveId) {
    const VoiceConfig& v = voiceForSlave(slaveId);
    _role       = v.role;
    _instId     = v.instrument;
    _pattern    = v.pattern;
    _patternLen = v.patternLen;
    _setInstrument(_instId);

    Serial.print(F("[Player] voice: 役割="));
    Serial.print(_role == ROLE_RHYTHM ? F("リズム") : F("旋律(輪唱)"));
    Serial.print(F("  楽器="));
    Serial.println(INSTRUMENTS[_instId].name);
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
    const Instrument& in = INSTRUMENTS[_instId];
    // 打楽器は固定周波数(baseFreq)、旋律楽器は音符の周波数で発音
    const float f = (in.baseFreq > 0.0f) ? in.baseFreq : freq;

    noInterrupts();
    noiseMix = in.noiseMix;
    phaseInc = f * (float)WT_SIZE / (float)AUDIO_SAMPLE_RATE;
    phase    = 0.0f;
    adsrCount = 0;
    adsr      = A_ATTACK;
    interrupts();

    // ── 初回発音の遅延を計測（PLAY受信 → 最初の音出しまでの時間）──
    if (_firstNote) {
        _firstNote = false;
        Serial.print(F("[TIMING] play→noteOn = "));
        Serial.print(micros() - _playCalledUs);
        Serial.println(F(" us"));
    }
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
    _playCalledUs = micros();
    _firstNote    = true;
    _syncAnchored = false;
    _errSum = _errAbsSum = _errMax = _errMin = 0;
    _errCount = 0;
    _eventIdx     = 0;
    _loopBase     = 0.0f;
    _noteSounding = false;
    _songFinished = false;
    // Master 指示で即開始（自己遅延なし）。先頭イベントの拍を起点にする。
    _nextTrigBeat = (_role == ROLE_RHYTHM && _pattern) ? _pattern[0]
                                                       : SONG[0].startBeat;

    Serial.print(F("[Player] play BPM=")); Serial.print(bpm);
    Serial.print(F("  楽器=")); Serial.println(INSTRUMENTS[_instId].name);

    if (ENABLE_TIMING_LOG) {
        // 計測用ログ: PLAY を実行した実時刻（輪唱の入りタイミング計測の起点）
        Serial.print(F("PLAY,")); Serial.print(_playStartMs);
        Serial.print(','); Serial.print(bpm);
        Serial.print(','); Serial.println(INSTRUMENTS[_instId].name);
    }
}

void Player::stop() {
    _playing      = false;
    _noteSounding = false;
    _noteOff();

    Serial.print(F("STOP,")); Serial.println(millis());
    if (_errCount == 0) return;

    const int32_t avg    = _errSum    / (int32_t)_errCount;
    const int32_t avgAbs = _errAbsSum / (int32_t)_errCount;
    Serial.println(F("===== SYNC 遅延計測結果 ====="));
    Serial.print(F("  サンプル数 : ")); Serial.println(_errCount);
    Serial.print(F("  平均誤差   : ")); Serial.print(avg);    Serial.println(F(" ms  (正=進み, 負=遅れ)"));
    Serial.print(F("  平均|誤差| : ")); Serial.print(avgAbs); Serial.println(F(" ms"));
    Serial.print(F("  最大       : ")); Serial.print(_errMax); Serial.println(F(" ms"));
    Serial.print(F("  最小       : ")); Serial.print(_errMin); Serial.println(F(" ms"));
    Serial.println(F("============================="));
    // 計測用ログ: 統計サマリを CSV でも出力（スクリプトでの集計用）
    Serial.print(F("STOP_STATS,")); Serial.print(_errCount);
    Serial.print(','); Serial.print(avg);
    Serial.print(','); Serial.print(avgAbs);
    Serial.print(','); Serial.print(_errMax);
    Serial.print(','); Serial.println(_errMin);
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
//  sync() ── SYNC 受信時の位相補正
//
//  【方式】絶対拍アライメント（beatCount 基準）
//
//  初回 SYNC でアンカーを確立する:
//    _anchorMaster = beatCount   (Master の絶対拍カウンタ)
//    _anchorSlave  = round(beat) (その時点の Slave 内部拍)
//
//  以降は Master の経過拍を uint8_t 演算でラップ込みに計算し、
//  Slave が本来いるべき拍を求めて誤差を算出する。
//
//  誤差が 1/2 拍超 → パケットロスによる位相ズレ → ハード補正
//  誤差が SYNC_CORRECT_MS 超 → 通常ドリフト → ダンプ補正
//  それ以下 → 許容範囲内、補正なし
// ============================================================
void Player::sync(uint8_t beatCount) {
    if (!_playing) return;

    const uint32_t now  = millis();
    const float    beat = _elapsedBeats(now);

    // ── アンカー確立（PLAY 後の最初の SYNC のみ）──
    if (!_syncAnchored) {
        _anchorMaster = beatCount;
        _anchorSlave  = (int32_t)roundf(beat);
        _syncAnchored = true;
        if (ENABLE_TIMING_LOG) {
            Serial.print(F("SYNC_ANCHOR,")); Serial.print(now);
            Serial.print(','); Serial.print(beatCount);
            Serial.print(','); Serial.println(_anchorSlave);
        }
        return;
    }

    // ── 期待拍を計算（uint8_t ラップを正しく処理）──
    // masterElapsed: 0〜255 で正しく動く（255拍 ≈ 2分 @ 120BPM）
    const uint8_t  masterElapsed = (uint8_t)(beatCount - _anchorMaster);
    const float    expectedBeat  = (float)_anchorSlave + (float)masterElapsed;
    const float    errBeat       = beat - expectedBeat;       // 正=進み過ぎ
    const int32_t  errMs         = (int32_t)(errBeat * _beatMs);

    // ── 統計蓄積 ──
    const int32_t absErr = (errMs >= 0) ? errMs : -errMs;
    _errSum    += errMs;
    _errAbsSum += absErr;
    if (_errCount == 0 || errMs > _errMax) _errMax = errMs;
    if (_errCount == 0 || errMs < _errMin) _errMin = errMs;
    _errCount++;

    // ── 位相補正 ──
    const int32_t halfBeatMs = (int32_t)(_beatMs * 0.5f);
    const char* status;
    bool isCorrection;
    if (absErr > halfBeatMs) {
        // 1/2 拍超 = パケットロスによる多拍ズレ → 一気に正しい位置へ
        _playStartMs = (uint32_t)((int32_t)_playStartMs + errMs);
        status = "hard";
        isCorrection = true;
    } else if (absErr > SYNC_CORRECT_MS) {
        // 通常ドリフト → ダンプ補正（発散防止）
        const int32_t correction = (int32_t)(errMs * SYNC_DAMP);
        _playStartMs = (uint32_t)((int32_t)_playStartMs + correction);
        status = "damp";
        isCorrection = true;
    } else {
        status = "ok";
        isCorrection = false;
    }

    // ── 計測用ログ（CSV, 1行/拍） ──
    // hard/damp（補正あり）は毎回出力する。ok は ENABLE_TIMING_LOG のときのみ
    // 出力する（毎拍ブロッキングして loop() の応答が遅れ、高 BPM で音符の
    // 発音を取りこぼす一因になるため、既定では計測時だけ有効にする）。
    if (isCorrection || ENABLE_TIMING_LOG) {
        Serial.print(F("SYNC,")); Serial.print(now);
        Serial.print(','); Serial.print(status);
        Serial.print(','); Serial.print(beatCount);
        Serial.print(','); Serial.print(expectedBeat, 2);
        Serial.print(','); Serial.print(beat, 2);
        Serial.print(','); Serial.println(errMs);
    }
}

// ============================================================
//  update() ── 拍に従って音符を発火する（loop から毎回）
// ============================================================
void Player::update() {
    if (!_playing) return;

    const uint32_t now  = millis();
    const float    beat = _elapsedBeats(now);

    // ── リズム機（ドラム）: pattern[] の拍ごとにワンショット発音 ──
    if (_role == ROLE_RHYTHM) {
        if (!_pattern || _patternLen == 0) return;
        while (beat >= _nextTrigBeat) {
            _noteOn(0.0f);  // 周波数は楽器固定（キック=低音 / スネア=ノイズ）
            if (ENABLE_TIMING_LOG) {
                // 計測用ログ: 発音時刻の目標拍からのズレ [ms]
                const float actualMs   = (float)(now - _playStartMs);
                const float expectedMs = _nextTrigBeat * _beatMs;
                Serial.print(F("HIT,")); Serial.print(now);
                Serial.print(','); Serial.print(_eventIdx);
                Serial.print(','); Serial.print(_nextTrigBeat, 2);
                Serial.print(','); Serial.println(actualMs - expectedMs, 1);
            }
            // ドラムは sustain=0 で自然減衰するため noteOff 不要（ワンショット）
            if (++_eventIdx >= _patternLen) {
                // 1周完了 — 停止
                stop();
                return;
            }
            _nextTrigBeat = _loopBase + _pattern[_eventIdx];
        }
        return;
    }

    // ── 旋律機（輪唱）: SONG[] を順に演奏 ──
    // 現在の音符を音符長ぶん鳴らしたら消す（スタッカート/余韻へ）
    if (_noteSounding && beat >= _noteOffBeat) {
        _noteOff();
        _noteSounding = false;
    }

    // 1周演奏済みかつ最後の音が鳴り終わったら停止
    if (_songFinished && !_noteSounding) {
        stop();
        return;
    }

    // 発火タイミングに達した音符を順に鳴らす（通常 1 回 / 拍未満）
    while (beat >= _nextTrigBeat) {
        const Note& n = SONG[_eventIdx];
        if (n.freq > 0.0f) {                  // 休符でなければ発音
            _noteOn(n.freq);
            _noteSounding = true;
            _noteOffBeat  = _nextTrigBeat + n.durBeat;

            if (ENABLE_TIMING_LOG) {
                // 計測用ログ: 発音時刻の目標拍からのズレ [ms]（輪唱の入りタイミング計測用）
                const float actualMs   = (float)(now - _playStartMs);
                const float expectedMs = _nextTrigBeat * _beatMs;
                Serial.print(F("NOTE,")); Serial.print(now);
                Serial.print(','); Serial.print(_eventIdx);
                Serial.print(','); Serial.print(_nextTrigBeat, 2);
                Serial.print(','); Serial.print(n.freq, 1);
                Serial.print(','); Serial.println(actualMs - expectedMs, 1);
            }
        }

        // 次の音符へ。末尾まで来たら1周完了フラグを立てて終了
        if (++_eventIdx >= SONG_LEN) {
            _songFinished = true;
            _nextTrigBeat = 1.0e9f;  // 以降の発音をブロック
            break;
        }
        _nextTrigBeat = _loopBase + SONG[_eventIdx].startBeat;
    }
}

// ============================================================
//  内部ヘルパ
// ============================================================
float Player::_elapsedBeats(uint32_t now) const {
    return (float)(now - _playStartMs) / _beatMs;
}
