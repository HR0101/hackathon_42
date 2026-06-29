#pragma once
#include <Arduino.h>
#include "Config.h"
#include "Song.h"

/**
 * @file  Player.h
 * @brief 倍音合成 + ADSR による自己完結型シンセサイザ（輪唱演奏エンジン）
 *
 * Arduino 内だけで楽曲を生成・演奏します（PC や外部音源は不要）。
 *
 *  - 音色   : Song.h の倍音テーブルから 1 周期ぶんのウェーブテーブルを生成し、
 *             位相アキュムレータで任意の高さの音を発振する。
 *  - 包絡線 : ADSR を FspTimer のサンプリング割り込み内で 1 サンプルずつ計算。
 *  - 出力   : A0（12bit DAC）→ TA7368 アンプ → スピーカー。
 *  - 輪唱   : 入りタイミングは Master が管理する。スレーブは PLAY を受けた
 *             瞬間に旋律を先頭から開始し、SONG_LEN_BEATS ごとにループする。
 *             setVoice() では機体番号に応じた「楽器」のみを選ぶ。
 *             スレーブ同士のずれは Master の SYNC で sync() が位相補正する。
 *
 * 【呼び出し関係（Slave.ino）】
 *   begin()        setup() で 1 回。DAC とサンプリング割り込みを起動。
 *   setVoice(id)   setup() で 1 回。MY_SLAVE_ID から音色・輪唱位置を決定。
 *   play(bpm)      PLAY 受信時。演奏開始。
 *   setBpm(bpm)    BPM 受信時。テンポ変更（再生中も即時反映）。
 *   sync(beat)     SYNC 受信時。位相ドリフトを補正。
 *   stop()         STOP 受信時。消音。
 *   update()       loop() で毎回。拍に従って音符を発火する。
 */

/** サンプリング周波数 [Hz]（DAC 出力レート）*/
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
/** ウェーブテーブル長（1 周期のサンプル数）*/
constexpr uint16_t WT_SIZE = 256;

/** SYNC 補正：この値 [ms] を超えるズレのときだけ位相を補正する */
constexpr int32_t  SYNC_CORRECT_MS = 20;
/** 補正係数（0.5 = ハーフステップ補正でオーバーシュートを防ぐ）*/
constexpr float    SYNC_DAMP       = 0.5f;

class Player {
public:
    void begin();                 ///< DAC + サンプリング割り込み起動
    void setVoice(uint8_t slaveId); ///< 機体番号 → 楽器・輪唱オフセット選択
    void play(uint16_t bpm);      ///< 演奏開始
    void stop();                  ///< 停止・消音
    void setBpm(uint16_t bpm);    ///< テンポ変更
    void sync(uint8_t beatCount); ///< 位相補正
    void update();                ///< 拍スケジューラ（loop から毎回）
    bool isPlaying() const { return _playing; }

private:
    void  _setInstrument(uint8_t instId);  ///< ウェーブテーブル & ADSR を再構築
    void  _noteOn(float freq);             ///< 発音開始（ADSR Attack へ）
    void  _noteOff();                      ///< 発音終了（ADSR Release へ）
    float _elapsedBeats(uint32_t now) const; ///< play からの経過拍数

    uint16_t _bpm         = 120;
    float    _beatMs      = 500.0f;  ///< 1 拍の長さ [ms] = 60000 / bpm
    bool     _playing     = false;
    uint32_t _playStartMs = 0;       ///< play() を呼んだ時刻 [ms]
    uint32_t _playCalledUs = 0;      ///< play() を呼んだ時刻 [µs]（遅延計測用）
    bool     _firstNote   = true;    ///< 初回 noteOn 計測フラグ

    // ── SYNC 位相補正アンカー ──
    bool     _syncAnchored  = false; ///< 最初の SYNC でアンカーを確立済みか
    uint8_t  _anchorMaster  = 0;     ///< アンカー時の Master beatCount
    int32_t  _anchorSlave   = 0;     ///< アンカー時の slave 内部拍（整数）

    // ── SYNC 遅延統計 ──
    int32_t  _errSum     = 0;   ///< 誤差の合計 [ms]（正=進み, 負=遅れ）
    int32_t  _errAbsSum  = 0;   ///< |誤差| の合計 [ms]
    int32_t  _errMax     = 0;   ///< 最大誤差 [ms]
    int32_t  _errMin     = 0;   ///< 最小誤差 [ms]
    uint16_t _errCount   = 0;   ///< サンプル数
    uint8_t  _instId      = INST_PIANO;
    uint8_t  _role        = ROLE_MELODY; ///< 旋律(輪唱) or リズム(ドラム)
    const float* _pattern = nullptr; ///< リズム機が叩く拍の配列（旋律機は nullptr）
    uint8_t  _patternLen  = 0;       ///< _pattern の要素数

    // ── 拍スケジューラの状態 ──
    uint8_t  _eventIdx     = 0;      ///< 次に発火する音符の SONG[] インデックス
    float    _loopBase     = 0.0f;   ///< 現在ループの先頭拍（ループごとに +SONG_LEN_BEATS）
    float    _nextTrigBeat = 0.0f;   ///< 次の音符を鳴らす絶対拍
    bool     _noteSounding = false;  ///< 現在発音中か
    float    _noteOffBeat  = 0.0f;   ///< 現在の音符を消す絶対拍
    bool     _songFinished = false;  ///< 1周演奏済みフラグ
};
