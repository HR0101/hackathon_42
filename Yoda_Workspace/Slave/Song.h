/**
 * @file    Song.h
 * @brief   輪唱用 楽曲データ・楽器音色・機体別設定（このファイル1つに集約）
 *
 * =====================================================================
 *  このファイルだけを編集すれば、輪唱の曲・音色を変更できます。
 *
 *   1) SONG[]        … 演奏する旋律（音名・開始拍・音符長）
 *   2) INSTRUMENTS[] … 楽器ごとの倍音構成と ADSR（旋律楽器＋ドラム）
 *   3) VOICES[]      … 「何機目か」→ 役割（旋律/リズム）+ 楽器 の対応表
 *
 *  スレーブ機は Slave.ino の MY_SLAVE_ID（1〜5号機）に応じて
 *  VOICES[] から自分の「役割・楽器・リズムパターン」を自動で選びます。
 *  3 機が旋律（輪唱）、2 機がリズム（ドラム）という構成です。
 *
 *  ★ 輪唱の入りタイミング（遅れ拍数）は Master が管理します。
 *    変更は Master.ino の ROUND_START_BEAT[] を編集してください。
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
 * @param noiseMix     ノイズと倍音合成音の混合比（0.0=トーンのみ, 1.0=ノイズのみ）。
 *                     スネアは 0.7 程度でノイズ主体＋胴鳴りトーンを混ぜる。
 * @param baseFreq     >0 ならこの固定周波数で発音（キック/スネア胴など打楽器）。
 *                     0 なら音符の周波数で発音（旋律楽器）。
 */
struct Instrument {
    const char*  name;
    const float* harmonics;
    uint8_t      numHarmonics;
    float        attackSec;
    float        decaySec;
    float        sustainLevel;
    float        releaseSec;
    float        noiseMix;
    float        baseFreq;
};

// ── 倍音テーブル（055/・ISHIMARU/ の Processing 音源から移植）──
static constexpr float HARM_PIANO[]       = { 1.00f, 0.60f, 0.35f, 0.20f, 0.12f, 0.08f, 0.03f };
static constexpr float HARM_TRUMPET[]     = { 1.00f, 0.90f, 0.85f, 0.70f, 0.55f, 0.45f, 0.30f, 0.20f, 0.15f, 0.10f };
static constexpr float HARM_MOKKIN_MARI[] = { 1.00f, 0.05f, 0.40f, 0.05f, 0.15f };
static constexpr float HARM_MOKKIN_SIRO[] = { 1.00f, 0.00f, 0.70f, 0.00f, 0.50f, 0.00f, 0.30f, 0.00f, 0.15f };
// キック: 単一オシレータが 300Hz→40Hz へピッチスイープする構成（ISHIMARU/kiku_kansei2）。
//        本エンジンはピッチスイープ非対応のため、音の大半を占める低域の
//        整定周波数（≈45Hz）で単一トーンとして近似する。
static constexpr float HARM_KICK[]        = { 1.00f };
// スネア胴鳴り: 180Hz（基音）+ 900Hz（5倍音）の2層トーン（ISHIMARU/sunea_kansei2）。
//              180Hz : 900Hz = 0.50 : 0.20 の振幅比で合成。
static constexpr float HARM_SNARE_BODY[]  = { 0.50f, 0.00f, 0.00f, 0.00f, 0.20f };

/** 楽器インデックス */
enum InstrumentId : uint8_t {
    INST_PIANO = 0,    ///< ピアノ（旋律）
    INST_TRUMPET,      ///< トランペット（旋律）
    INST_MOKKIN_MARI,  ///< 木琴・まろやか（旋律）
    INST_MOKKIN_SIRO,  ///< 木琴・硬い（旋律）
    INST_KICK,         ///< キックドラム（リズム / 低音トーン）
    INST_SNARE,        ///< スネアドラム（リズム / ノイズ＋胴鳴り）
    INST_COUNT
};

// 打楽器の基本周波数（ISHIMARU kiku_kansei2 / sunea_kansei2 の値）
constexpr float KICK_FREQ        = 45.0f;   ///< キック整定周波数 [Hz]（ピッチスイープの低域を近似）
constexpr float SNARE_BODY_FREQ  = 180.0f;  ///< スネア胴鳴り基音 [Hz]（5倍音=900Hz）

static constexpr Instrument INSTRUMENTS[INST_COUNT] = {
    //  name        harmonics          n   attack  decay  sustain release  noiseMix baseFreq
    { "Piano",    HARM_PIANO,        7, 0.001f, 0.450f, 0.001f, 0.35f, 0.0f, 0.0f },
    { "Trumpet",  HARM_TRUMPET,     10, 0.050f, 0.050f, 0.95f, 0.50f, 0.0f, 0.0f },
    { "MokkinM",  HARM_MOKKIN_MARI,  5, 0.001f, 0.050f, 0.10f, 0.20f, 0.0f, 0.0f },
    { "MokkinS",  HARM_MOKKIN_SIRO,  9, 0.001f, 0.080f, 0.00f, 0.06f, 0.0f, 0.0f },
    // キック: ワンショット（sustain=0）。kiku_kansei2 の attack≈3ms/decay(exp(-12t))を踏まえ、
    //         わずかなノイズ(noiseMix)でアタック時のクリック感を近似する。
    { "Kick",     HARM_KICK,         1, 0.003f, 0.220f, 0.00f, 0.05f, 0.12f, KICK_FREQ },
    // スネア: sunea_kansei2 の noise*0.35 : body(0.5+0.2) 比から noiseMix≈0.33 に近似。
    { "Snare",    HARM_SNARE_BODY,   5, 0.002f, 0.250f, 0.00f, 0.05f, 0.33f, SNARE_BODY_FREQ },
};

// ============================================================
//  リズムパターン（リズム機が SONG の代わりに叩く拍）
//  ── 輪唱 3 回目（3号機）が終わる拍と同時に鳴り止むよう、
//     Master の ROUND_START_BEAT[2]=16 拍 + SONG[] 最終音符の終端 30.5 拍
//     = 絶対拍 46.5 まで、通常パターン（偶数=キック/奇数=スネア）を延長する。
// ============================================================
/** キック: 各小節の 1・3 拍目（偶数拍）= ドン・ドン。46.5 拍（輪唱3回目終了）まで */
static constexpr float KICK_PATTERN[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
    32, 34, 36, 38, 40, 42, 44, 46
};
/** スネア: 各小節の 2・4 拍目（奇数拍）= バックビート タン・タン。46.5 拍まで */
static constexpr float SNARE_PATTERN[] = {
    1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31,
    33, 35, 37, 39, 41, 43, 45
};
constexpr uint8_t KICK_PATTERN_LEN  = sizeof(KICK_PATTERN)  / sizeof(KICK_PATTERN[0]);
constexpr uint8_t SNARE_PATTERN_LEN = sizeof(SNARE_PATTERN) / sizeof(SNARE_PATTERN[0]);

// ============================================================
//  3) 機体別設定 ── 「何機目か」→ 役割（旋律/リズム）+ 楽器
// ============================================================
/** 機体の役割 */
enum Role : uint8_t {
    ROLE_MELODY = 0,  ///< 旋律（輪唱）。SONG[] を演奏する。Master が入りをずらす。
    ROLE_RHYTHM,      ///< リズム（ドラム）。pattern[] を叩く。輪唱初回から演奏。
};

/**
 * @brief 1 機分の設定
 * @param role       ROLE_MELODY（旋律/輪唱）または ROLE_RHYTHM（リズム）
 * @param instrument 楽器（INSTRUMENTS[] のインデックス）
 * @param pattern    ROLE_RHYTHM のとき叩く拍の配列。ROLE_MELODY は nullptr。
 * @param patternLen pattern の要素数。
 */
struct VoiceConfig {
    uint8_t      role;
    uint8_t      instrument;
    const float* pattern;
    uint8_t      patternLen;
};

/**
 * VOICES[0] = 1号機, [1] = 2号機, …
 *
 *   旋律(輪唱)機 … 3 機。Master が ROUND_START_BEAT[] で入りをずらす。
 *   リズム機     … 2 機。輪唱初回（拍0）から演奏を開始する。
 *
 * ★ 輪唱の入りタイミングは Master(Master.ino の ROUND_START_BEAT[]) が管理。
 *   ここでは役割・楽器・リズムパターンだけを決める。
 */
static constexpr VoiceConfig VOICES[] = {
    /* 1号機 */ { ROLE_MELODY, INST_PIANO,       nullptr,       0 },
    /* 2号機 */ { ROLE_MELODY, INST_TRUMPET,     nullptr,       0 },
    /* 3号機 */ { ROLE_MELODY, INST_MOKKIN_MARI, nullptr,       0 },
    /* 4号機 */ { ROLE_RHYTHM, INST_KICK,        KICK_PATTERN,  KICK_PATTERN_LEN  },
    /* 5号機 */ { ROLE_RHYTHM, INST_SNARE,       SNARE_PATTERN, SNARE_PATTERN_LEN },
};
constexpr uint8_t VOICE_COUNT = sizeof(VOICES) / sizeof(VOICES[0]);

/**
 * @brief スレーブ ID（IR_DEST_SLAVE1〜5 = 1〜5）から設定を取得する
 * @param slaveId 機体番号（1〜5）。範囲外なら 1号機の設定を返す。
 */
inline const VoiceConfig& voiceForSlave(uint8_t slaveId) {
    const uint8_t idx = (slaveId >= 1 && slaveId <= VOICE_COUNT) ? (slaveId - 1) : 0;
    return VOICES[idx];
}
