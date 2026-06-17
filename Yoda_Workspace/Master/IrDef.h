/**
 * @file    IrDef.h
 * @brief   赤外線通信システム 共通定数・型定義
 *
 * @details NECフォーマット準拠 Pulse Distance 方式の赤外線通信に使用する
 *          タイミング定数・ピン定義・パケット構造・コマンドenumを定義します。
 *          マスタ機・スレーブ機の両スケッチから共通でインクルードしてください。
 *
 * 【パケット構造 (24bit)】
 *  ┌─────────┬──────────┬────────────┬──────────────────────┐
 *  │  bit 3:0 │  bit 7:4 │  bit 15:8  │      bit 23:16       │
 *  │ 宛先(4b) │ CMD (4b) │ DATA (8b)  │ CHECK = upper⊕data   │
 *  └─────────┴──────────┴────────────┴──────────────────────┘
 *  upperByte = (dest & 0x0F) | ((cmd & 0x0F) << 4)
 *  checkByte = upperByte XOR data
 *
 * 【対象ハードウェア】
 *  Arduino UNO R4 WiFi (Renesas RA4M1 / R7FA4M1AB3CFP)
 */

#pragma once
#include <stdint.h>

// ============================================================
//  ピン定義 (Arduino UNO R4 WiFi 実装)
// ============================================================
/** 送信ピン: D9  (RA4M1: P303 / GTIOC1B) */
constexpr uint8_t IR_TX_PIN  = 9;
/** 演出用 LED ピン: D4 */
constexpr uint8_t IR_LED_PIN = 4;
/** 受信ピン: D2  (外部割り込み INT0 / IRQ0) */
constexpr uint8_t IR_RX_PIN  = 2;

// ============================================================
//  搬送波周波数
// ============================================================
/** 赤外線搬送波周波数 [Hz] — 38 kHz */
constexpr uint32_t IR_CARRIER_HZ = 38000UL;

// ============================================================
//  Pulse Distance 方式 送信タイミング定数 [µs]
//  (NECフォーマット準拠)
// ============================================================
/** リーダー マーク長 (搬送波 ON) */
constexpr uint16_t IR_LEADER_MARK  = 9000;
/** リーダー スペース長 (搬送波 OFF) */
constexpr uint16_t IR_LEADER_SPACE = 4500;
/** ビット共通 マーク長 */
constexpr uint16_t IR_BIT_MARK     = 562;
/** ビット '0' スペース長 (短い) */
constexpr uint16_t IR_BIT0_SPACE   = 562;
/** ビット '1' スペース長 (長い) */
constexpr uint16_t IR_BIT1_SPACE   = 1687;
/** トレーリング マーク長 (最終ビット後の終端パルス) */
constexpr uint16_t IR_TRAIL_MARK   = 562;

// ============================================================
//  パケット フィールド定義 (24bit)
// ============================================================
/** パケット総ビット数 */
constexpr uint8_t  IR_PACKET_BITS  = 24;

// ── シフト量 (パケット内での各フィールドの開始ビット位置) ──
constexpr uint8_t  IR_DEST_SHIFT   = 0;   ///< 宛先フィールド開始ビット
constexpr uint8_t  IR_CMD_SHIFT    = 4;   ///< コマンドフィールド開始ビット
constexpr uint8_t  IR_DATA_SHIFT   = 8;   ///< データフィールド開始ビット
constexpr uint8_t  IR_CHECK_SHIFT  = 16;  ///< 検査フィールド開始ビット

// ── 抽出マスク ──
constexpr uint32_t IR_DEST_MASK    = 0x0000000FUL;  ///< 宛先マスク  (4bit)
constexpr uint32_t IR_CMD_MASK     = 0x000000F0UL;  ///< コマンドマスク (4bit)
constexpr uint32_t IR_DATA_MASK    = 0x0000FF00UL;  ///< データマスク  (8bit)
constexpr uint32_t IR_CHECK_MASK   = 0x00FF0000UL;  ///< 検査マスク   (8bit)

// ============================================================
//  受信 タイミング判定しきい値 [µs]
//
//  FALLING エッジ間隔 = 直前マーク長 + 直前スペース長
//    リーダー間隔 : 9000 + 4500 = 13500 µs
//    ビット '0'  : 562  +  562  =  1124 µs
//    ビット '1'  : 562  + 1687  =  2249 µs
// ============================================================
/** リーダー間隔 判定下限 [µs] */
constexpr uint16_t IR_LEADER_INTV_MIN = 11000;
/** リーダー間隔 判定上限 [µs] */
constexpr uint16_t IR_LEADER_INTV_MAX = 16000;

/** ビット間隔 有効下限 [µs] (ノイズ除去) */
constexpr uint16_t IR_BIT_INTV_MIN   =  700;
/** ビット '0'/'1' 判定しきい値 [µs]
 *  1124µs (bit0) と 2249µs (bit1) の中間付近 */
constexpr uint16_t IR_BIT_THRESHOLD  = 1600;
/** ビット間隔 有効上限 [µs]
 *  理論値 bit'1' = 2249µs。OSRB38C9AAの AGC による延伸を考慮して 3500µs まで許容。 */
constexpr uint16_t IR_BIT_INTV_MAX   = 3500;

// ============================================================
//  宛先アドレス定義
// ============================================================
constexpr uint8_t IR_DEST_ALL    = 0x0;  ///< ブロードキャスト (全スレーブ同時)
constexpr uint8_t IR_DEST_SLAVE1 = 0x1;  ///< スレーブ 1 号機
constexpr uint8_t IR_DEST_SLAVE2 = 0x2;  ///< スレーブ 2 号機
constexpr uint8_t IR_DEST_SLAVE3 = 0x3;  ///< スレーブ 3 号機
constexpr uint8_t IR_DEST_SLAVE4 = 0x4;  ///< スレーブ 4 号機
constexpr uint8_t IR_DEST_SLAVE5 = 0x5;  ///< スレーブ 5 号機

// ============================================================
//  コマンド定義
// ============================================================
/**
 * @enum  IrCmd
 * @brief 赤外線通信コマンド一覧 (4bit: 0x1〜0xF を使用)
 */
enum class IrCmd : uint8_t {
    PLAY = 0x1,  ///< 再生開始 (data: 無効)
    STOP = 0x2,  ///< 再生停止 (data: 無効)
    BPM  = 0x3,  ///< BPM 設定 (data: BPM値 40〜240)
    SYNC = 0x4,  ///< タイミング同期 (data: 小節位相など)
};
