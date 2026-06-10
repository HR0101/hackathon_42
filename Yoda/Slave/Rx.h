/**
 * @file    Rx.h
 * @brief   赤外線受信クラス (スレーブ機専用)
 *
 * @details D2 ピン (INT0) の FALLING エッジ外部割り込みで
 *          OSRB38C9AA (38 kHz 対応赤外線受信モジュール) の出力を受け取ります。
 *          エッジ間隔の計測と状態機械によって 24 ビットパケットをデコードします。
 *
 * 【OSRB38C9AA 動作特性】
 *   - 38 kHz 搬送波を検出すると出力が LOW になる (アクティブロー)
 *   - 搬送波なし (スペース区間) は出力が HIGH
 *   - FALLING エッジ = マーク区間の開始
 *
 * 【FALLING エッジ間隔デコード方式】
 *   連続する2つの FALLING エッジの間隔 = 直前マーク長 + 直前スペース長
 *
 *   間隔 ≒ 13500 µs → リーダー検出 (9000 + 4500 µs)
 *   間隔 ≒  1124 µs → ビット '0'    ( 562 +  562 µs)
 *   間隔 ≒  2249 µs → ビット '1'    ( 562 + 1687 µs)
 *
 *   ビット '0'/'1' の判定しきい値: 1600 µs (IR_BIT_THRESHOLD)
 *
 * 【受信状態機械】
 *   IDLE ─[リーダー間隔検出]→ RECEIVING ─[24bit 揃う]→ IDLE
 *                                        ─[異常間隔]──→ IDLE (リセット)
 *
 * 【シングルトンパターンの理由】
 *   attachInterrupt() に渡すコールバックは静的関数または
 *   グローバルアクセス可能な関数である必要があります。
 *   getInstance() でシングルトンインスタンスを取得し、
 *   ラムダ経由で onEdge() を呼び出します。
 *
 * 【使用例 (スレーブ機 setup / loop)】
 * @code
 *   void setup() { Rx::getInstance().begin(); }
 *   void loop() {
 *       uint32_t raw;
 *       if (Rx::getInstance().decode(raw)) {
 *           uint8_t dest, cmd, data;
 *           if (Packet::parse(raw, dest, cmd, data)) {
 *               // 宛先・コマンドに応じた処理
 *           }
 *       }
 *   }
 * @endcode
 */

#pragma once
#include <Arduino.h>
#include "IrDef.h"

// ============================================================
//  Rx クラス (シングルトン)
// ============================================================
class Rx {
public:
    /**
     * @brief シングルトンインスタンスを返す
     * @return Rx の唯一のインスタンスへの参照
     */
    static Rx &getInstance() {
        static Rx instance;
        return instance;
    }

    /**
     * @brief D2 ピンを入力に設定し、FALLING エッジ割り込みを登録する
     *
     * @details 1. IR_RX_PIN を INPUT (プルアップなし) に設定
     *          2. digitalPinToInterrupt() で割り込み番号を取得
     *          3. attachInterrupt() でコールバックを FALLING に登録
     *
     * @note setup() 内で一度だけ呼び出してください。
     */
    void begin();

    /**
     * @brief FALLING エッジ割り込みハンドラ
     *
     * @details attachInterrupt() から自動的に呼び出されます。
     *          micros() で前回エッジからの経過時間 (interval) を計測し、
     *          状態機械でリーダー検出・ビット値判定・パケット組み立てを行います。
     *
     * @warning ISR コンテキストで実行されます。
     *          ブロッキング処理・Serial.print() 等は使用禁止です。
     */
    void onEdge();

    /**
     * @brief 受信パケットのデコード結果を取得する (loop() 内で繰り返し呼ぶ)
     *
     * @param outPacket [出力] デコード済みの 24 ビット生パケット
     * @return 新しいパケットが揃っていれば true、なければ false
     *
     * @note  true を返した後は Packet::parse(outPacket, ...) で
     *        宛先・コマンド・データに分解し、XOR 検査を行ってください。
     *
     * @note  noInterrupts() / interrupts() でアトミックにデータを取り出します。
     */
    bool decode(uint32_t &outPacket);

private:
    // シングルトンのため コンストラクタ/コピー/ムーブ を非公開に制限
    Rx()                         = default;
    Rx(const Rx &)               = delete;
    Rx &operator=(const Rx &)    = delete;

    // ──────────────────────────────────────────────────
    //  受信状態機械の内部状態
    // ──────────────────────────────────────────────────

    /** 受信状態 */
    enum class State : uint8_t {
        IDLE,       ///< アイドル: リーダー間隔を待機中
        RECEIVING,  ///< データ受信中: ビット間隔を収集中
    };

    /** 現在の受信状態 */
    volatile State    _state    = State::IDLE;

    /** 受信済みビット数 (0〜23) */
    volatile uint8_t  _bitCount = 0;

    /** 組み立て中の 24 ビットパケット (LSB から順に格納) */
    volatile uint32_t _packet   = 0;

    /** パケット受信完了フラグ (decode() が true を返したらクリア) */
    volatile bool     _ready    = false;

    /** 前回 FALLING エッジの micros() 値 (間隔計算用) */
    volatile uint32_t _prevTime = 0;
};
