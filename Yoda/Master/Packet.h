/**
 * @file    Packet.h
 * @brief   赤外線通信 パケット生成/解析クラス
 *
 * @details 24 ビットパケットの生成・分解・XOR 整合性検査を担当します。
 *          マスタ機・スレーブ機の両スケッチから共通でインクルードしてください。
 *
 * 【パケット構造 (24bit、LSB ファースト送信)】
 *
 *   bit 23        16  15         8  7    4  3    0
 *   ┌─────────────┬─────────────┬────────┬────────┐
 *   │  CHECK (8b) │  DATA  (8b) │CMD (4b)│DST (4b)│
 *   └─────────────┴─────────────┴────────┴────────┘
 *
 *   upperByte = (dest & 0x0F) | ((cmd & 0x0F) << 4)
 *   CHECK     = upperByte XOR DATA
 *
 * @note    静的メソッドのみで構成されるため、インスタンス生成は不要です。
 */

#pragma once
#include <Arduino.h>
#include "IrDef.h"

// ============================================================
//  Packet クラス
// ============================================================
class Packet {
public:
    // ---------------------------------------------------------
    //  コンストラクタ/デストラクタ禁止 (ユーティリティクラス)
    // ---------------------------------------------------------
    Packet()  = delete;
    ~Packet() = delete;

    /**
     * @brief  24 ビットパケットを生成する
     *
     * @param  dest  宛先アドレス (4bit: 0x0 = ブロードキャスト, 0x1〜0x5 = 各スレーブ)
     * @param  cmd   コマンド     (4bit: IrCmd 参照)
     * @param  data  データ       (8bit: BPM 値など)
     * @return 生成された 24 ビットパケット (上位 8bit は常に 0)
     *
     * @details
     *   1. upperByte = (dest & 0x0F) | ((cmd & 0x0F) << 4)
     *   2. checkByte = upperByte XOR data
     *   3. packet    = dest[3:0] | cmd[7:4] | data[15:8] | check[23:16]
     *
     * @example
     *   uint32_t pkt = Packet::build(IR_DEST_ALL, (uint8_t)IrCmd::BPM, 120);
     */
    static uint32_t build(uint8_t dest, uint8_t cmd, uint8_t data);

    /**
     * @brief  24 ビットパケットを解析し、XOR 整合性を検査する
     *
     * @param  raw   受信・デコードされた 24 ビット生データ
     * @param  dest  [出力] 宛先アドレス (4bit)
     * @param  cmd   [出力] コマンド     (4bit)
     * @param  data  [出力] データ       (8bit)
     * @return 検査バイトが一致すれば true、不一致（通信エラー）なら false
     *
     * @note  戻り値が false の場合、dest/cmd/data の内容は不定です。
     *
     * @example
     *   uint8_t dest, cmd, data;
     *   if (Packet::parse(rawPacket, dest, cmd, data)) {
     *       // 正常受信: 各フィールドを使用
     *   }
     */
    static bool parse(uint32_t raw, uint8_t &dest, uint8_t &cmd, uint8_t &data);
};
