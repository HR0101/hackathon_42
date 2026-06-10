/**
 * @file    Rx.cpp
 * @brief   赤外線受信クラス 実装 (スレーブ機専用)
 *
 * 【受信アルゴリズムの詳細】
 *
 *  OSRB38C9AA の出力信号 (アクティブロー):
 *
 *    HIGH ─────┐     ┌──────┐  ┌──────┐  ┌──
 *    LOW        └─────┘      └──┘      └──┘
 *               ↑FALL        ↑FALL     ↑FALL
 *               | ←──────── T1 ──────→ |
 *               | ←─ T0 → |
 *
 *  FALLING エッジ間隔 T = Mark 長 + Space 長:
 *    T0 = 1 つ前の FALLING から現在の FALLING まで
 *
 *  リーダー検出:
 *    T ≒ 9000 + 4500 = 13500 µs  (判定範囲: 11000〜16000 µs)
 *    → State::IDLE から State::RECEIVING へ遷移
 *
 *  ビット判定 (リーダー検出後の各 T):
 *    T ≒  562 +  562 =  1124 µs → bit '0' (判定: T < 1600 µs)
 *    T ≒  562 + 1687 =  2249 µs → bit '1' (判定: T >= 1600 µs)
 *    T が 700〜3000 µs の範囲外  → 受信エラー → IDLE へリセット
 *
 *  24 ビット揃ったら _ready フラグを立てて IDLE に戻る。
 *  decode() が _ready を確認し、アトミックにパケットを取り出す。
 *
 * 【LSB ファースト】
 *  送信側 (Tx::sendFrame) は bit0 (宛先 LSB) から bit23 (検査 MSB) の順に送信。
 *  受信側も同じ順序でビットを _packet の bit0〜bit23 に格納するため、
 *  パケット値は送信側と完全に一致する。
 */

#include "Rx.h"

// ============================================================
//  Rx::begin — 受信初期化
// ============================================================
void Rx::begin() {
    // ── ピン設定 ────────────────────────────────────────────
    // OSRB38C9AA は内部 47kΩ プルアップを持つため、外部プルアップは不要。
    // INPUT (フロート) で設定する。
    pinMode(IR_RX_PIN, INPUT);

    // ── 状態機械の初期化 ─────────────────────────────────
    _state    = State::IDLE;
    _bitCount = 0u;
    _packet   = 0UL;
    _ready    = false;
    _prevTime = micros();  // 初回エッジ計算のベースタイムを設定

    // ── 外部割り込み登録 ─────────────────────────────────
    // digitalPinToInterrupt(IR_RX_PIN) で D2 → INT0 の番号に変換。
    // FALLING エッジ (受信モジュール出力が HIGH→LOW) でコールバックを呼ぶ。
    //
    // [シングルトンへのアクセス方法]
    // attachInterrupt() のコールバックは関数ポインタのため、
    // メンバ関数を直接渡せない。
    // キャプチャなしラムダ (→ 関数ポインタへ暗黙変換) 経由で
    // getInstance().onEdge() を呼び出す。
    //
    attachInterrupt(
        digitalPinToInterrupt(IR_RX_PIN),
        []() { Rx::getInstance().onEdge(); },  // ラムダ: 関数ポインタに変換可能
        FALLING
    );
}

// ============================================================
//  Rx::onEdge — FALLING エッジ割り込みハンドラ (ISR コンテキスト)
// ============================================================
void Rx::onEdge() {
    // ── 経過時間の計測 ──────────────────────────────────
    // micros() は ISR 内でも使用可能 (RA4M1 / Arduino UNO R4 WiFi)。
    // 32bit 符号なしの差分をとることで micros() のオーバーフロー (約71分周期)
    // を自動的に処理できる。
    const uint32_t now      = micros();
    const uint32_t interval = now - _prevTime;  // 前回 FALLING からの経過 [µs]
    _prevTime = now;                            // 次回の計算のために更新

    // ── デバッグカウンタ更新 ──────────────────────────────
    _dbgEdgeCount++;
    _dbgLastInterval = interval;
    if (interval > _dbgMaxInterval) _dbgMaxInterval = interval;

    // インターバルヒストグラム (6バケット)
    //   [0] <   700 µs   ノイズ
    //   [1]  700-1600 µs  ビット '0' 帯域
    //   [2] 1600-3000 µs  ビット '1' 帯域
    //   [3] 3000-11000 µs 中間帯域 (AGC 分割の証拠)
    //   [4] 11000-16000 µs リーダー帯域
    //   [5] > 16000 µs   起動直後 / パケット間専用
    if      (interval <  700)   _dbgHist[0]++;
    else if (interval <  1600)  _dbgHist[1]++;
    else if (interval <  3000)  _dbgHist[2]++;
    else if (interval <  11000) _dbgHist[3]++;
    else if (interval <= 16000) _dbgHist[4]++;
    else                        _dbgHist[5]++;

    // ── 状態機械 ─────────────────────────────────────────
    switch (_state) {

    // ────────────────────────────────────────────────────────
    //  IDLE 状態: パケット間の長い無信号期間 (> 16000 µs) を待つ
    // ────────────────────────────────────────────────────────
    case State::IDLE:
        if (interval >= IR_LEADER_INTV_MIN && interval <= IR_LEADER_INTV_MAX) {
            // 正規の NEC リーダー間隔 (13500 µs) を検出
            // → 次の FALLING エッジから即座にビット受信開始
            _dbgLeaderCount++;
            _state    = State::RECEIVING;
            _bitCount = 0u;
            _packet   = 0UL;
        } else if (interval > IR_LEADER_INTV_MAX) {
            // パケット間の長い無信号期間 (AGC によりリーダーが検出不能な場合もこちらに入る)
            // 現在の FALLING エッジ = リーダーマーク開始 or ビット 0 開始
            // → 最初のビット間隔が来るまで LEADER_DETECTED で待機
            _dbgLeaderCount++;
            _state    = State::LEADER_DETECTED;
            _bitCount = 0u;
            _packet   = 0UL;
        }
        // リーダー・ギャップ以外の間隔 (ノイズ・ビット・中間) は無視
        break;

    // ────────────────────────────────────────────────────────
    //  LEADER_DETECTED 状態: 最初のデータビットを待つ
    //
    //  パターン:
    //    [bit]  700-3000 µs → ビット 0 としてデコードし、RECEIVING へ
    //    [mid]  3000-16000 µs → まだリーダー/遷移域、LEADER_DETECTED で待機続行
    //    [gap]  > 16000 µs → 新たなパケット開始、バッファリセット
    //    [noise] < 700 µs → ノイズ、IDLE に戻る
    // ────────────────────────────────────────────────────────
    case State::LEADER_DETECTED:
        if (interval >= IR_BIT_INTV_MIN && interval <= IR_BIT_INTV_MAX) {
            // 最初のデータビット間隔を検出 → ビット 0 としてデコード開始
            // 現在の FALLING エッジはビット 1 のマーク開始なので、
            // この間隔値は「ビット 0 のマーク + スペース」を表す。
            const uint8_t bit0 = (interval >= IR_BIT_THRESHOLD) ? 1u : 0u;
            _packet   = (uint32_t)bit0;  // LSB 位置にビット 0 を格納
            _bitCount = 1u;              // ビット 0 受信済みとして開始
            _state    = State::RECEIVING;
        } else if (interval > IR_BIT_INTV_MAX) {
            // まだリーダー/中間/ギャップ帯域 → 待機続行
            if (interval > IR_LEADER_INTV_MAX) {
                // 新たなギャップ → バッファをリセットして再待機
                _bitCount = 0u;
                _packet   = 0UL;
            }
            // LEADER_DETECTED のまま待機
        } else {
            // interval < IR_BIT_INTV_MIN → ノイズ → IDLE に戻る
            _dbgErrorCount++;
            _dbgLastBadInterval = interval;
            _state    = State::IDLE;
            _bitCount = 0u;
            _packet   = 0UL;
        }
        break;

    // ────────────────────────────────────────────────────────
    //  RECEIVING 状態: ビット間隔 (1124 µs / 2249 µs) を収集
    // ────────────────────────────────────────────────────────
    case State::RECEIVING:
        if (interval >= IR_BIT_INTV_MIN && interval <= IR_BIT_INTV_MAX) {
            // 有効なビット間隔範囲内 → ビット値を判定
            //   interval <  IR_BIT_THRESHOLD (1600 µs) → ビット '0'
            //   interval >= IR_BIT_THRESHOLD (1600 µs) → ビット '1'
            const uint8_t bit = (interval >= IR_BIT_THRESHOLD) ? 1u : 0u;

            // パケットに LSB ファーストで格納
            // bit0: 宛先 LSB, ..., bit23: 検査バイト MSB
            _packet |= ((uint32_t)bit << _bitCount);
            _bitCount++;

            if (_bitCount >= IR_PACKET_BITS) {
                // 24 ビット揃った → パケット完成
                _ready = true;   // decode() に通知
                _state = State::IDLE;
                // ビットカウント・パケットバッファはそのまま保持
                // (decode() で読み出されるまで _packet を保護)
            }
        } else {
            // ビット間隔が範囲外 → 通信エラーまたはノイズ
            // 状態機械をリセットしてアイドルに戻る
            _dbgErrorCount++;             // エラーカウンタ
            _dbgLastBadInterval = interval;  // 異常な間隔を記録
            _state    = State::IDLE;
            _bitCount = 0u;
            _packet   = 0UL;
        }
        break;

    // ────────────────────────────────────────────────────────
    //  デフォルト: 安全のため IDLE にリセット
    // ────────────────────────────────────────────────────────
    default:
        _state = State::IDLE;
        break;
    }
}

// ============================================================
//  Rx::decode — パケット取り出し (loop() コンテキスト)
// ============================================================
bool Rx::decode(uint32_t &outPacket) {
    // 受信完了フラグのチェック (高速パス: 割り込み無効化不要)
    if (!_ready) return false;

    // ── アトミックなデータ取り出し ──────────────────────────
    // noInterrupts() / interrupts() で割り込みを一時停止し、
    // _ready フラグと _packet の読み出しを分割不可能な操作として実行する。
    // RA4M1 は 32bit アーキテクチャだが、volatile 変数への複数アクセスを
    // 安全にするために保護が必要。
    noInterrupts();
    outPacket = _packet;   // デコード済み 24 ビットパケットを返す
    _ready    = false;     // フラグをクリア (次のパケット受信を許可)
    interrupts();

    // 呼び出し元で Packet::parse(outPacket, dest, cmd, data) を使って
    // 宛先・コマンド・データへの分解と XOR 検査を行うこと。
    return true;
}
