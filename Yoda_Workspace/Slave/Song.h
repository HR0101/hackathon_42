/**
 * @file    Song.h
 * @brief   輪唱用 楽曲データ・楽器音色・機体別設定（このファイル1つに集約）
 *
 * =====================================================================
 *  このファイルだけを編集すれば、輪唱の曲・音色・タイミングを変更できます。
 *
 *   1) SONG[]        … 演奏する旋律（音名・開始拍・音符長）
 *   2) INSTRUMENTS[] … 楽器ごとの倍音構成と ADSR（音色）
 *   3) VOICES[]      … 「何機目か」→ 輪唱の遅れ拍数 と 楽器 の対応表
 *
 *  スレーブ機は Slave.ino の MY_SLAVE_ID（1〜5号機）に応じて
 *  VOICES[] から自分の「輪唱タイミング」と「楽器」を自動で選びます。
 * =====================================================================
 */
#pragma once
#include <stdint.h>

// ============================================================
//  1) 音名 → 周波数 [Hz]（平均律）
// ============================================================
constexpr float NOTE_REST = 0.0f;     ///< 休符（発音しない）
constexpr float NOTE_C4 = 261.63f;
constexpr float NOTE_D4 = 293.66f;
constexpr float NOTE_E4 = 329.63f;
constexpr float NOTE_F4 = 349.23f;
constexpr float NOTE_G4 = 392.00f;
constexpr float NOTE_A4 = 440.00f;
constexpr float NOTE_B4 = 493.88f;
constexpr float NOTE_C5 = 523.25f;

// ============================================================
//  楽曲イベント定義
// ============================================================
/**
 * @brief 1 つの音符
 * @param startBeat 曲先頭からの開始位置 [拍]
 * @param durBeat   音符の長さ [拍]
 * @param freq      周波数 [Hz]（NOTE_REST=0 は休符）
 */
struct Note {
    float startBeat;
    float durBeat;
    float freq;
};

// ============================================================
//  楽曲データ ── かえるのうた（輪唱曲）
//  ※ 4/4 拍子・全 8 小節 = 32 拍で 1 ループ（輪唱として綺麗に巡回する）
// ============================================================
static constexpr Note SONG[] = {
    // ── 1〜2小節：かえるの うたが きこえて くるよ ──
    { 0.0f, 0.5f, NOTE_C4 }, { 1.0f, 0.5f, NOTE_D4 }, { 2.0f, 0.5f, NOTE_E4 }, { 3.0f, 0.5f, NOTE_F4 },
    { 4.0f, 0.5f, NOTE_E4 }, { 5.0f, 0.5f, NOTE_D4 }, { 6.0f, 0.5f, NOTE_C4 },
    // ── 3〜4小節 ──
    { 8.0f, 0.5f, NOTE_E4 }, { 9.0f, 0.5f, NOTE_F4 }, { 10.0f, 0.5f, NOTE_G4 }, { 11.0f, 0.5f, NOTE_A4 },
    { 12.0f, 0.5f, NOTE_G4 }, { 13.0f, 0.5f, NOTE_F4 }, { 14.0f, 0.5f, NOTE_E4 },
    // ── 5〜6小節：ぐわっ ぐわっ ぐわっ ぐわっ（2分音符）──
    { 16.0f, 0.5f, NOTE_C4 }, { 18.0f, 0.5f, NOTE_C4 }, { 20.0f, 0.5f, NOTE_C4 }, { 22.0f, 0.5f, NOTE_C4 },
    // ── 7小節：ぐわぐわぐわぐわ（8分音符の連打）──
    { 24.0f, 0.25f, NOTE_C4 }, { 24.5f, 0.25f, NOTE_C4 }, { 25.0f, 0.25f, NOTE_D4 }, { 25.5f, 0.25f, NOTE_D4 },
    { 26.0f, 0.25f, NOTE_E4 }, { 26.5f, 0.25f, NOTE_E4 }, { 27.0f, 0.25f, NOTE_F4 }, { 27.5f, 0.25f, NOTE_F4 },
    // ── 8小節 ──
    { 28.0f, 0.5f, NOTE_E4 }, { 29.0f, 0.5f, NOTE_D4 }, { 30.0f, 0.5f, NOTE_C4 },
};
/** 楽曲の音符数 */
constexpr uint8_t SONG_LEN = sizeof(SONG) / sizeof(SONG[0]);
/** 1 ループの長さ [拍]（この拍数ごとに先頭へ戻る。輪唱の周期）*/
constexpr float SONG_LEN_BEATS = 32.0f;

// ============================================================
//  2) 楽器音色定義（倍音合成 + ADSR）
// ============================================================
/**
 * @brief 楽器 1 種類の音色パラメータ
 *
 * @param harmonics    倍音の振幅配列（harmonics[0]=基音, [1]=2倍音, …）
 * @param numHarmonics 倍音の数
 * @param attackSec    アタック時間 [秒]（立ち上がり）
 * @param decaySec     ディケイ時間 [秒]（サスティンまでの減衰）
 * @param sustainLevel サスティンレベル（0.0〜1.0）
 * @param releaseSec   リリース時間 [秒]（音を止めた後の余韻）
 */
struct Instrument {
    const char* name;
    const float* harmonics;
    uint8_t      numHarmonics;
    float        attackSec;
    float        decaySec;
    float        sustainLevel;
    float        releaseSec;
};

// ── 倍音テーブル（055/ の Processing 音源から移植）──
static constexpr float HARM_PIANO[]       = { 1.00f, 0.60f, 0.35f, 0.20f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f };
static constexpr float HARM_TRUMPET[]     = { 1.00f, 1.00f, 0.85f, 0.85f, 0.75f, 0.75f, 0.65f, 0.55f, 0.45f, 0.30f };
static constexpr float HARM_MOKKIN_MARI[] = { 1.00f, 0.00f, 0.50f };
static constexpr float HARM_MOKKIN_SIRO[] = { 1.00f, 0.00f, 0.70f, 0.00f, 0.50f, 0.00f, 0.30f, 0.00f, 0.15f };

/** 楽器インデックス */
enum InstrumentId : uint8_t {
    INST_PIANO = 0,    ///< ピアノ
    INST_TRUMPET,      ///< トランペット
    INST_MOKKIN_MARI,  ///< 木琴（まろやか）
    INST_MOKKIN_SIRO,  ///< 木琴（硬い・減衰速い）
    INST_COUNT
};

static constexpr Instrument INSTRUMENTS[INST_COUNT] = {
    //  name        harmonics          n   attack  decay  sustain release
    { "Piano",    HARM_PIANO,       10, 0.005f, 1.090f, 0.04f, 0.32f },
    { "Trumpet",  HARM_TRUMPET,     10, 0.100f, 0.100f, 0.95f, 0.30f },
    { "MokkinM",  HARM_MOKKIN_MARI,  3, 0.001f, 0.150f, 0.10f, 0.20f },
    { "MokkinS",  HARM_MOKKIN_SIRO,  9, 0.001f, 0.080f, 0.00f, 0.06f },
};

// ============================================================
//  3) 機体別設定 ── 「何機目か」→ 輪唱タイミング + 楽器
// ============================================================
/**
 * @brief 1 機分の輪唱設定
 * @param roundOffsetBeats 演奏開始を何拍遅らせるか（輪唱の入りタイミング）
 * @param instrument       使用する楽器（INSTRUMENTS[] のインデックス）
 */
struct VoiceConfig {
    float   roundOffsetBeats;
    uint8_t instrument;
};

/**
 * VOICES[0] = 1号機, VOICES[1] = 2号機, …
 *
 * roundOffsetBeats を 8 拍（2小節）ずつずらすと、かえるのうたが
 * 正統的な 4 部輪唱になります。楽器は機体ごとに変えています。
 * ★ ここを書き換えれば輪唱の入り順・音色を自由に変更できます。
 */
static constexpr VoiceConfig VOICES[] = {
    /* 1号機 */ {  0.0f, INST_PIANO       },
    /* 2号機 */ {  8.0f, INST_TRUMPET     },
    /* 3号機 */ { 16.0f, INST_MOKKIN_MARI },
    /* 4号機 */ { 24.0f, INST_MOKKIN_SIRO },
    /* 5号機 */ {  4.0f, INST_PIANO       },
};
constexpr uint8_t VOICE_COUNT = sizeof(VOICES) / sizeof(VOICES[0]);

/**
 * @brief スレーブ ID（IR_DEST_SLAVE1〜5 = 1〜5）から輪唱設定を取得する
 * @param slaveId 機体番号（1〜5）。範囲外なら 1号機の設定を返す。
 */
inline const VoiceConfig& voiceForSlave(uint8_t slaveId) {
    const uint8_t idx = (slaveId >= 1 && slaveId <= VOICE_COUNT) ? (slaveId - 1) : 0;
    return VOICES[idx];
}
