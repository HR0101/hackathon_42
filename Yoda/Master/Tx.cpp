/**
 * @file    Tx.cpp
 * @brief   赤外線送信クラス 実装 (マスタ機専用)
 *
 * 【38 kHz 搬送波の生成メカニズム】
 *
 *   RA4M1 の GPT タイマーを FspTimer 経由で「76 kHz 周期割り込み」に設定し、
 *   コールバック (carrierISR) 内で D9 (IR_TX_PIN) を 1 ビットトグルします。
 *
 *     タイマー周波数:  76,000 Hz  (= 38 kHz × 2)
 *     トグル後の実効:  38,000 Hz  / 約 50% デューティ
 *
 *   48 MHz PCLKD でのカウンタ設定:
 *     48,000,000 / 76,000 ≈ 631.6 カウント/周期
 *     → 実効 38,019 Hz (誤差 +0.05 %) — NEC 規格 ±20% 内
 *
 *   タイマーは begin() で起動しますが、搬送波を出力するか否かは
 *   volatile フラグ s_carrierActive で制御します。
 *   フラグ OFF 時は ISR 内で何もしない (ピンは LOW のまま)。
 *
 * 【ピン → GPT チャンネル の対応 (参考)】
 *   D9 = P303 → GTIOC1B (GPT1 チャンネル B)
 *   ただし、本実装は GPT 出力機能を直接使わず「ソフトウェアトグル」で
 *   搬送波を生成するため、チャンネル番号は任意の空きを使用します。
 *
 * 【高速化が必要な場合 (ポートレジスタ直接アクセス)】
 *   ISR 内の digitalWrite() を以下に差し替えることで ISR 負荷を削減できます:
 *
 *     // D9 = P303: Port3, Pin3
 *     // HIGH: PCNTR3 の PSET ビット (bit[19:16] 相当) に 1 を書く
 *     R_PORT3->PCNTR3 = (uint32_t)(1U << (3U + 16U));  // SET  P303
 *     // LOW:  PCNTR3 の PRST ビット (bit[3:0] 相当) に 1 を書く
 *     R_PORT3->PCNTR3 = (uint32_t)(1U << 3U);          // RESET P303
 */

#include "Tx.h"
#include <FspTimer.h>

// ============================================================
//  モジュール内部 (ファイルスコープ) 変数
// ============================================================

/** 38 kHz 搬送波生成に使用する GPT タイマーインスタンス */
static FspTimer s_carrierTimer;

/**
 * @brief 搬送波出力中フラグ
 *
 * @details - true  : ISR が IR_TX_PIN をトグル → 38 kHz 搬送波出力 (Mark)
 *           - false : ISR は何もしない   → ピンは LOW のまま   (Space)
 *
 * @note  volatile: ISR と通常コード間で共有するため最適化を禁止。
 */
static volatile bool s_carrierActive = false;

/**
 * @brief トグル用ピンレベル保持変数 (ISR 内で使用)
 *
 * @details `LOW`/`HIGH` の切り替えに使用。
 *          HIGH (1) ↔ LOW (0) を交互に出力することで搬送波を生成する。
 */
static volatile uint8_t s_pinLevel = LOW;

// ============================================================
//  タイマー割り込みコールバック (ISR コンテキスト)
// ============================================================
/**
 * @brief GPT タイマーオーバーフロー時に呼ばれるコールバック
 *
 * @details 76 kHz で呼ばれ、s_carrierActive が true の間だけ
 *          IR_TX_PIN をトグルして 38 kHz 搬送波を生成します。
 *
 * @note  ISR コンテキストのためブロッキング処理・malloc 等は厳禁です。
 * @note  timer_callback_args_t は RA4M1 FSP で定義された構造体です。
 */
static void carrierISR(timer_callback_args_t *args) {
    (void)args;  // 未使用引数の警告を抑制

    if (!s_carrierActive) {
        // Space 区間: 何もしない (ピンは sendSpace() が LOW に設定済み)
        return;
    }

    // Mark 区間: H→L→H→… とトグルして 38 kHz 搬送波を生成
    s_pinLevel ^= 1u;
    digitalWrite(IR_TX_PIN, s_pinLevel);

    // ─────────────────────────────────────────────────────────
    // [最適化メモ] 高速化が必要な場合は上記 digitalWrite() を
    // RA4M1 ポートレジスタ直接書き込みに差し替えてください。
    //
    //   if (s_pinLevel) {
    //       R_PORT3->PCNTR3 = (uint32_t)(1U << (3U + 16U)); // SET  P303=HIGH
    //   } else {
    //       R_PORT3->PCNTR3 = (uint32_t)(1U << 3U);         // RESET P303=LOW
    //   }
    //
    // PCNTR3 は RA4M1 の Port n Control Register 3:
    //   bit[31:16] PSET: 対応ビットに 1 を書くと PODR のそのビットが HIGH
    //   bit[15: 0] PRST: 対応ビットに 1 を書くと PODR のそのビットが LOW
    // ─────────────────────────────────────────────────────────
}

// ============================================================
//  Tx::begin — 初期化
// ============================================================
void Tx::begin() {
    // 二重初期化を防止
    if (_initialized) return;

    // ── ピン初期化 ─────────────────────────────────────────
    // 送信ピン D9 を出力、初期値 LOW (搬送波なし)
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, LOW);

    // 演出用 LED (D4) を出力、初期値 OFF
    pinMode(IR_LED_PIN, OUTPUT);
    digitalWrite(IR_LED_PIN, LOW);

    // ── GPT タイマー設定 (FspTimer) ────────────────────────
    //
    // FspTimer::get_available_timer() で未使用の GPT チャンネルを
    // 自動取得する。-1 が返った場合は空きなし (Servo 等と競合している可能性)。
    //
    // force_use_of_pwm_reserved_timer(true) を事前に呼ぶと、
    // PWM 予約済みのタイマーも解放して取得できる場合がある。
    //
    uint8_t timerType = GPT_TIMER;
    int8_t  timerCh   = FspTimer::get_available_timer(timerType);

    if (timerCh < 0) {
        // PWM 予約済みタイマーを強制解放して再試行
        FspTimer::force_use_of_pwm_reserved_timer();
        timerCh = FspTimer::get_available_timer(timerType);
    }

    if (timerCh < 0) {
        // それでも取得できない場合はエラーを出力して終了
        Serial.println(F("[Tx] ERROR: GPT タイマーを確保できませんでした。"
                         "Servo ライブラリ等との競合を確認してください。"));
        return;
    }

    // タイマーを「周期割り込みモード / 76 kHz / コールバック付き」で初期化
    //   周波数 = IR_CARRIER_HZ * 2 = 38000 * 2 = 76000 Hz
    //   76 kHz でトグルすると実効周波数 = 38 kHz の搬送波になる
    const bool ok = s_carrierTimer.begin(
        TIMER_MODE_PERIODIC,                         // 周期割り込みモード
        timerType,                                   // GPT_TIMER
        timerCh,                                     // 自動取得したチャンネル
        static_cast<float>(IR_CARRIER_HZ) * 2.0f,   // 76,000.0 Hz
        0.0f,                                        // デューティ (未使用)
        carrierISR                                   // オーバーフロー ISR
    );

    if (!ok) {
        Serial.println(F("[Tx] ERROR: FspTimer::begin() が失敗しました。"));
        return;
    }

    // オーバーフロー割り込みの有効化と優先度設定
    // 優先度 8 (0=最高, 15=最低)。delayMicroseconds() より低くする必要あり。
    s_carrierTimer.setup_overflow_irq(8 /* priority */);

    // タイマーを開始 (カウンタ動作スタート、ISR 呼び出し開始)
    // この時点では s_carrierActive = false のため搬送波は出力されない
    s_carrierTimer.open();
    s_carrierTimer.start();

    _initialized = true;

    Serial.print(F("[Tx] 初期化完了: GPT ch="));
    Serial.print(timerCh);
    Serial.println(F(" / 76kHz タイマー起動 (38kHz キャリア待機中)"));
}

// ============================================================
//  Tx::sendMark — 搬送波出力 (Mark 区間)
// ============================================================
void Tx::sendMark(uint16_t time) {
    // キャリアフラグを ON にする前にピンレベルをリセットして
    // LOW から確実なトグルサイクルを始める
    s_pinLevel     = LOW;
    s_carrierActive = true;   // ISR が次の割り込みからトグルを開始

    // 指定時間だけ搬送波を出力しながらブロッキング待機
    delayMicroseconds(time);

    // キャリアフラグを OFF にしてトグルを停止
    s_carrierActive = false;

    // ピンを確実に LOW にして Mark 区間を終了する
    // (ISR が HIGH のタイミングで止まった場合のケアも兼ねる)
    s_pinLevel = LOW;
    digitalWrite(IR_TX_PIN, LOW);
}

// ============================================================
//  Tx::sendSpace — 搬送波停止待機 (Space 区間)
// ============================================================
void Tx::sendSpace(uint16_t time) {
    // 搬送波を確実に停止 (sendMark() の末尾で既に停止済みだが二重確認)
    s_carrierActive = false;
    digitalWrite(IR_TX_PIN, LOW);

    // 指定時間だけ無信号でブロッキング待機
    delayMicroseconds(time);
}

// ============================================================
//  Tx::sendFrame — 24 ビットパケット完全送信
// ============================================================
void Tx::sendFrame(uint32_t packet) {
    // ── 1. リーダー ─────────────────────────────────────────
    // NECフォーマット: 9000 µs の搬送波 + 4500 µs の無信号
    sendMark(IR_LEADER_MARK);
    sendSpace(IR_LEADER_SPACE);

    // ── 2. データビット (LSB ファースト、24 ビット) ─────────
    // LSB ファースト = bit0 (宛先 bit0) から bit23 (検査 bit7) の順に送信
    //
    //  ビット '0': IR_BIT_MARK (562 µs) + IR_BIT0_SPACE ( 562 µs)
    //  ビット '1': IR_BIT_MARK (562 µs) + IR_BIT1_SPACE (1687 µs)
    //
    for (uint8_t i = 0u; i < IR_PACKET_BITS; i++) {
        sendMark(IR_BIT_MARK);

        if (packet & (1UL << i)) {
            // ビット '1': 長いスペース (1687 µs)
            sendSpace(IR_BIT1_SPACE);
        } else {
            // ビット '0': 短いスペース ( 562 µs)
            sendSpace(IR_BIT0_SPACE);
        }
    }

    // ── 3. トレーリングマーク ────────────────────────────────
    // 最後のスペースの後に 562 µs の終端パルスを送信して
    // 受信側に最終ビットの終わりを伝える
    sendMark(IR_TRAIL_MARK);
}
