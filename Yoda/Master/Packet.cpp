/**
 * @file    Packet.cpp
 * @brief   赤外線通信 パケット生成/解析クラス 実装
 *
 * @details build() でパケットを 24bit に組み立て、
 *          parse() でパケットを分解して XOR 整合性検査を行います。
 */

#include "Packet.h"

// ============================================================
//  Packet::build — 24 ビットパケット生成
// ============================================================
uint32_t Packet::build(uint8_t dest, uint8_t cmd, uint8_t data) {
    // ─── Step 1: 上位バイト (upperByte) の計算 ───────────────
    // 宛先 (下位 4bit) とコマンド (上位 4bit) を 1 バイトに合成する。
    // これはパケットの最下位バイト (bit[7:0]) と同じ値。
    const uint8_t upperByte = (dest & 0x0F)           // 宛先: bit[3:0]
                            | ((cmd  & 0x0F) << 4);   // CMD : bit[7:4]

    // ─── Step 2: 検査バイト (checkByte) の計算 ───────────────
    // upperByte XOR data で計算する 1 バイトの誤り検査値。
    // 受信側は同じ計算を行い、受信した検査バイトと比較する。
    const uint8_t checkByte = upperByte ^ data;

    // ─── Step 3: 24 ビットパケットの組み立て ──────────────────
    //  bit [ 3: 0] = 宛先 (dest)
    //  bit [ 7: 4] = コマンド (cmd)
    //  bit [15: 8] = データ (data)
    //  bit [23:16] = 検査バイト (checkByte)
    const uint32_t packet =
        ((uint32_t)(dest  & 0x0F) << IR_DEST_SHIFT)   |  // 宛先
        ((uint32_t)(cmd   & 0x0F) << IR_CMD_SHIFT)    |  // コマンド
        ((uint32_t)data           << IR_DATA_SHIFT)   |  // データ
        ((uint32_t)checkByte      << IR_CHECK_SHIFT);    // 検査バイト

    return packet;
}

// ============================================================
//  Packet::parse — 24 ビットパケット解析 & 整合性検査
// ============================================================
bool Packet::parse(uint32_t raw, uint8_t &dest, uint8_t &cmd, uint8_t &data) {
    // ─── Step 1: 各フィールドの抽出 ──────────────────────────
    // 24bit の raw データから各フィールドをマスク＆シフトで分離する。
    dest = static_cast<uint8_t>((raw >> IR_DEST_SHIFT)  & 0x0F);
    cmd  = static_cast<uint8_t>((raw >> IR_CMD_SHIFT)   & 0x0F);
    data = static_cast<uint8_t>((raw >> IR_DATA_SHIFT)  & 0xFF);
    const uint8_t checkReceived =
        static_cast<uint8_t>((raw >> IR_CHECK_SHIFT) & 0xFF);

    // ─── Step 2: 検査バイトの再計算 ──────────────────────────
    // 送信側と同じアルゴリズムで期待値を計算する。
    const uint8_t upperByte    = (dest & 0x0F) | ((cmd & 0x0F) << 4);
    const uint8_t checkExpected = upperByte ^ data;

    // ─── Step 3: 受信値と期待値の比較 ────────────────────────
    // 一致すれば通信エラーなしと判断して true を返す。
    // 不一致の場合はビット化けなどの通信エラーと判断して false を返す。
    return (checkReceived == checkExpected);
}
