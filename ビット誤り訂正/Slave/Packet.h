/**
 * @file    Packet.h
 * @brief   赤外線通信 パケット生成/解析クラス (拡張ハミング(24,18) SEC-DED)
 *
 * @details 24 ビットパケットの生成・分解・前方誤り訂正 (FEC) を担当します。
 *          旧 XOR 検査に代えて拡張ハミング SEC-DED 符号を用い、
 *          単一ビット誤りの訂正 (SEC) と 2 ビット誤りの検出 (DED) を行います。
 *          マスタ機・スレーブ機の両スケッチから共通でインクルードしてください。
 *
 * 【パケット構造 (24bit、LSB ファースト送信)】
 *
 *   bit 23        18  17 16  15        8  7    4  3    0
 *   ┌──────────────┬──────┬───────────┬────────┬────────┐
 *   │  PARITY (6b) │SEQ(2)│  DATA (8b)│CMD (4b)│DST (4b)│
 *   └──────────────┴──────┴───────────┴────────┴────────┘
 *
 *   情報ビット i0..i17 = [DST0..3, CMD0..3, DATA0..7, SEQ0..1] (= bit17:0)
 *   PARITY    = SEC 5bit (bit22:18) + 全体パリティ 1bit (bit23)
 *
 * @note    静的メソッドのみで構成されるため、インスタンス生成は不要です。
 */

#pragma once
#include <Arduino.h>
#include "IrDef.h"

// ============================================================
//  パケット解析結果 (parse() の戻り値)
// ============================================================
/**
 * @enum  ParseResult
 * @brief パケット復号の判定結果 (拡張ハミング SEC-DED)
 */
enum class ParseResult : uint8_t {
    OK            = 0,  ///< 誤りなし。そのまま受理。
    CORRECTED     = 1,  ///< 単一ビット誤りを訂正して受理。
    UNCORRECTABLE = 2,  ///< 2bit 誤り等で訂正不能。廃棄 (安全側)。
};

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
     * @brief  24 ビットパケットを生成する (拡張ハミング SEC-DED 符号化)
     *
     * @param  dest  宛先アドレス (4bit: 0x0 = ブロードキャスト, 0x1〜0x5 = 各スレーブ)
     * @param  cmd   コマンド     (4bit: IrCmd 参照)
     * @param  data  データ       (8bit: BPM 値など)
     * @param  seq   論理コマンド連番 (2bit: 反復送信の重複除去に使用)
     * @return 生成された 24 ビットパケット (上位 8bit は 0)
     *
     * @details
     *   1. 情報 18bit = DST[3:0] | CMD[7:4] | DATA[15:8] | SEQ[17:16]
     *   2. SEC パリティ s(5bit) = XOR( i_k=1 である列ベクトル c_k )
     *   3. 全体パリティ p(1bit) = (18情報ビット ⊕ 5パリティビット) の偶数パリティ
     *   4. packet = info | s[22:18] | p[23]
     *
     * @example
     *   uint32_t pkt = Packet::build(IR_DEST_ALL, (uint8_t)IrCmd::BPM, 120, seq);
     */
    static uint32_t build(uint8_t dest, uint8_t cmd, uint8_t data, uint8_t seq);

    /**
     * @brief  24 ビットパケットを復号し、誤り訂正/検出を行う
     *
     * @param  raw   受信・デコードされた 24 ビット生データ
     * @param  dest  [出力] 宛先アドレス (4bit)
     * @param  cmd   [出力] コマンド     (4bit)
     * @param  data  [出力] データ       (8bit)
     * @param  seq   [出力] 論理コマンド連番 (2bit)
     * @return ParseResult (OK / CORRECTED / UNCORRECTABLE)
     *
     * @note  UNCORRECTABLE のとき dest/cmd/data/seq の内容は不定です。
     *        OK / CORRECTED のときは (必要なら訂正済みの) 正しい値が入ります。
     *
     * @example
     *   uint8_t dest, cmd, data, seq;
     *   ParseResult r = Packet::parse(rawPacket, dest, cmd, data, seq);
     *   if (r != ParseResult::UNCORRECTABLE) {
     *       // 正常受信 (OK もしくは訂正済み): 各フィールドを使用
     *   }
     */
    static ParseResult parse(uint32_t raw, uint8_t &dest, uint8_t &cmd,
                             uint8_t &data, uint8_t &seq);
};
