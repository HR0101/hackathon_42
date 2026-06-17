/**
 * @file    Tx.h
 * @brief   赤外線送信クラス (マスタ機専用)
 *
 * @details D9 ピン (RA4M1: P303 / GTIOC1B) から 38 kHz 搬送波を出力し、
 *          Pulse Distance 方式 (NECフォーマット準拠) で 24 ビットパケットを
 *          送信します。全ての遅延は delayMicroseconds() によるブロッキング処理です。
 *
 * 【38 kHz 搬送波の生成方法】
 *   RA4M1 の GPT タイマーを FspTimer クラス経由で設定し、
 *   76 kHz の周期割り込みで D9 (IR_TX_PIN) をトグルします。
 *   これにより実効 38 kHz・約 50% デューティの搬送波が得られます。
 *
 *   タイマーは begin() 呼び出し時に起動しますが、搬送波出力は
 *   sendMark() 内で有効化されるまで停止しています。
 *
 * 【使用例 (マスタ機 setup / loop)】
 * @code
 *   Tx tx;
 *   void setup() { tx.begin(); }
 *   void loop()  {
 *       uint32_t pkt = Packet::build(IR_DEST_ALL, (uint8_t)IrCmd::BPM, 120);
 *       tx.sendFrame(pkt);
 *       delay(500);
 *   }
 * @endcode
 *
 * @note   FspTimer の空きチャンネルを自動取得します。
 *         Servo ライブラリ等と併用する場合は競合に注意してください。
 */

#pragma once
#include <Arduino.h>
#include "IrDef.h"

// ============================================================
//  Tx クラス
// ============================================================
class Tx {
public:
    Tx()  = default;
    ~Tx() = default;

    /**
     * @brief 送信ピン (D9) と GPT タイマーを初期化する
     *
     * @details  1. IR_TX_PIN / IR_LED_PIN を出力に設定
     *           2. FspTimer で GPT を 76 kHz 周期割り込みモードで設定
     *           3. タイマーカウントを開始 (搬送波はまだ出力しない)
     *
     * @note setup() 内で一度だけ呼び出してください。
     */
    void begin();

    /**
     * @brief 38 kHz 搬送波を time [µs] 出力する (Mark 区間)
     *
     * @param time 搬送波を出力する時間 [マイクロ秒]
     *
     * @details  1. キャリアフラグを ON → タイマー ISR がトグルを開始
     *           2. delayMicroseconds(time) でブロック待機
     *           3. キャリアフラグを OFF → ピンを LOW に固定
     */
    void sendMark(uint16_t time);

    /**
     * @brief 搬送波を停止して time [µs] 待機する (Space 区間)
     *
     * @param time 搬送波を停止する時間 [マイクロ秒]
     *
     * @details ピンを LOW に固定し、delayMicroseconds(time) で待機します。
     */
    void sendSpace(uint16_t time);

    /**
     * @brief 24 ビットパケットを完全に NEC フォーマットで送信する
     *
     * @param packet Packet::build() で生成した 24 ビットパケット
     *
     * @details 送信シーケンス:
     *   1. Leader Mark  (9000 µs)   — リーダー搬送波
     *   2. Leader Space (4500 µs)   — リーダースペース
     *   3. Bit Mark + Bit Space × 24 回 (LSB ファースト)
     *      - bit=0: Mark 562µs + Space  562µs
     *      - bit=1: Mark 562µs + Space 1687µs
     *   4. Trail Mark   ( 562 µs)   — 終端パルス
     *
     * @note 合計送信時間 = 9000+4500+(562+562)*n0+(562+1687)*n1+562 [µs]
     *       最長 (全ビット=1): 約 67.9 ms
     */
    void sendFrame(uint32_t packet);

private:
    bool _initialized = false;  ///< begin() 呼び出し済みフラグ
};
