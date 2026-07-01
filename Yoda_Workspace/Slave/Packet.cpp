/**
 * @file    Packet.cpp
 * @brief   赤外線通信 パケット生成/解析クラス 実装 (拡張ハミング(24,18) SEC-DED)
 *
 * @details build() で 18 情報ビットからハミングパリティを生成し 24bit に組み立て、
 *          parse() でシンドローム復号により 1bit 訂正 / 2bit 検出を行います。
 *
 * 【符号の構成】
 *   情報ビット i0..i17 = bit17:0 (DST/CMD/DATA/SEQ)
 *   各 i_k に 1〜31 のうち 2 の冪(1,2,4,8,16)を除く相異なる 5bit 列ベクトル c_k を割当。
 *     SEC パリティ s(5bit) = XOR( i_k=1 である c_k )                  → bit22:18
 *     全体パリティ p(1bit) = (18情報ビット ⊕ 5パリティビット)の偶数パリティ → bit23 (DED)
 *
 *   復号: シンドローム S = s_recv ⊕ s_calc, 全体パリティ照合 P を求める。
 *     S=0 かつ P 一致     → 誤りなし (OK)
 *     P 不一致 (奇数誤り) → 単一誤り。S が指す位置を反転して受理 (CORRECTED)
 *     S≠0 かつ P 一致     → 偶数 (2 重) 誤り。訂正せず廃棄 (UNCORRECTABLE)
 *   ※ 廃棄・欠落した分は Master 側の反復送信が救済する (ハミング＋反復 併用)。
 */

#include "Packet.h"

// ============================================================
//  ハミング符号 列ベクトルテーブル
//
//  情報ビット i0..i17 に割り当てる 5bit 列ベクトル c_k。
//  1〜31 のうち 2 の冪(1,2,4,8,16)を除く相異なる 18 個を採用する。
//  単一情報ビット誤り時、シンドローム S が一意に c_k と一致するため、
//  どのビットが化けたかを特定して訂正できる。
// ============================================================
static const uint8_t IR_COL_VEC[18] = {
    3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23
};

// ============================================================
//  bitParity — 立っているビット数の偶奇 (偶数パリティ) を返す
// ============================================================
static inline uint8_t bitParity(uint32_t v) {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return static_cast<uint8_t>(v & 1U);
}

// ============================================================
//  computeSec — SEC パリティ s(5bit) を計算する
//  s = XOR( 情報ビット i_k が 1 である列ベクトル c_k )
// ============================================================
static inline uint8_t computeSec(uint32_t info18) {
    uint8_t s = 0;
    for (uint8_t k = 0; k < 18; k++) {
        if ((info18 >> k) & 1U) {
            s ^= IR_COL_VEC[k];
        }
    }
    return s;  // 下位 5bit が有効
}

// ============================================================
//  Packet::build — 24 ビットパケット生成 (SEC-DED 符号化)
// ============================================================
uint32_t Packet::build(uint8_t dest, uint8_t cmd, uint8_t data, uint8_t seq) {
    // ─── Step 1: 18 情報ビットを組み立てる (bit17:0) ───────────
    //  宛先・コマンド・データ・連番を所定のビット位置に配置する。
    const uint32_t info =
          ((uint32_t)(dest & 0x0F)       << IR_DEST_SHIFT)   // 宛先 bit3:0
        | ((uint32_t)(cmd  & 0x0F)       << IR_CMD_SHIFT)    // CMD  bit7:4
        | ((uint32_t)data                << IR_DATA_SHIFT)   // DATA bit15:8
        | ((uint32_t)(seq  & IR_SEQ_MAX) << IR_SEQ_SHIFT);   // SEQ  bit17:16

    // ─── Step 2: SEC パリティ s(5bit) と全体パリティ p(1bit) ───
    const uint8_t s = computeSec(info);
    // 全体パリティ = 18 情報ビット ⊕ 5 パリティビット の偶数パリティ (DED 用)
    const uint8_t p = bitParity(info) ^ bitParity(s);

    // ─── Step 3: パリティ 6bit を bit23:18 に格納して完成 ──────
    const uint32_t packet =
          info
        | ((uint32_t)s        << IR_PARITY_SHIFT)         // SEC 5bit : bit22:18
        | ((uint32_t)(p & 1U) << (IR_PARITY_SHIFT + 5));  // 全体 1bit: bit23

    return packet;
}

// ============================================================
//  Packet::parse — 24 ビットパケット復号 (シンドローム訂正/検出)
// ============================================================
ParseResult Packet::parse(uint32_t raw, uint8_t &dest, uint8_t &cmd,
                          uint8_t &data, uint8_t &seq, int8_t* correctedBit) {
    // ─── Step 1: 受信した情報ビットとパリティを分離 ────────────
    const uint32_t info  = raw & IR_INFO_MASK;                    // bit17:0
    const uint8_t  sRecv = (raw >> IR_PARITY_SHIFT) & 0x1F;       // SEC 5bit : bit22:18
    const uint8_t  pRecv = (raw >> (IR_PARITY_SHIFT + 5)) & 1U;   // 全体 1bit: bit23

    // ─── Step 2: シンドローム S と 全体パリティ照合 P を計算 ────
    const uint8_t sCalc = computeSec(info);
    const uint8_t S = sRecv ^ sCalc;  // 5bit シンドローム (化けた列ベクトルの XOR)
    // 受信 24bit 全体の偶数パリティ。誤りなしなら 0、奇数誤りなら 1。
    const uint8_t P = bitParity(info) ^ bitParity(sRecv) ^ pRecv;

    // ─── Step 3: 判定と訂正 ────────────────────────────────────
    uint32_t corrected = raw;
    ParseResult result;
    // 計測用: 反転した物理ビット位置 (bit0〜23)。-1=誤りなし, -2=訂正不能(多重誤り)。
    int8_t bitPos = -1;

    if (S == 0 && P == 0) {
        // 誤りなし
        result = ParseResult::OK;

    } else if (P == 1) {
        // 奇数誤り → 単一誤りとして訂正を試みる
        if (S == 0) {
            // 全体パリティビット (bit23) 自身の化け。情報ビットは無傷。
            result = ParseResult::CORRECTED;
            bitPos = IR_PARITY_SHIFT + 5;  // bit23
        } else {
            // S が情報ビットの列ベクトル c_k と一致するか探索する
            int8_t idx = -1;
            for (uint8_t k = 0; k < 18; k++) {
                if (IR_COL_VEC[k] == S) { idx = static_cast<int8_t>(k); break; }
            }
            if (idx >= 0) {
                // 情報ビット i_idx の化け → 反転して訂正
                corrected ^= (1UL << idx);
                result = ParseResult::CORRECTED;
                bitPos = idx;  // 情報ビットの列番号 = 物理ビット位置 (bit0〜17)
            } else if (S == 1 || S == 2 || S == 4 || S == 8 || S == 16) {
                // SEC パリティビット自身の化け。情報ビットは無傷。
                result = ParseResult::CORRECTED;
                // S = 2^n → SEC パリティの n ビット目 → 物理ビット位置 = 18+n
                uint8_t n = 0;
                for (uint8_t sh = S; sh > 1; sh >>= 1) n++;
                bitPos = IR_PARITY_SHIFT + n;
            } else {
                // 単一誤りなら必ず上記いずれかに該当する。該当なし = 多重誤り。
                // 誤った符号語で動作させないため安全側で廃棄する。
                result = ParseResult::UNCORRECTABLE;
                bitPos = -2;
            }
        }

    } else {
        // S != 0 かつ P == 0 → 偶数 (2 重) 誤り → 訂正せず廃棄 (安全側)
        result = ParseResult::UNCORRECTABLE;
        bitPos = -2;
    }

    if (correctedBit) *correctedBit = bitPos;

    // ─── Step 4: (訂正後の) 各フィールドを抽出する ─────────────
    //  UNCORRECTABLE のときは値が信頼できないため抽出しない。
    if (result != ParseResult::UNCORRECTABLE) {
        dest = static_cast<uint8_t>((corrected >> IR_DEST_SHIFT) & 0x0F);
        cmd  = static_cast<uint8_t>((corrected >> IR_CMD_SHIFT)  & 0x0F);
        data = static_cast<uint8_t>((corrected >> IR_DATA_SHIFT) & 0xFF);
        seq  = static_cast<uint8_t>((corrected >> IR_SEQ_SHIFT)  & IR_SEQ_MAX);
    }

    return result;
}
