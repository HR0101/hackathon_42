/**
 * @file    main.ino  (Slave)
 * @brief   赤外線同期輪唱システム — スレーブ機 メインスケッチ
 *
 * =====================================================================
 *  【このスケッチを書き込む前に必ずやること】
 *
 *    ★★★ MY_SLAVE_ID を機体番号に合わせて書き換える ★★★
 *        1号機: IR_DEST_SLAVE1
 *        2号機: IR_DEST_SLAVE2
 *        3号機: IR_DEST_SLAVE3
 *        4号機: IR_DEST_SLAVE4
 *        5号機: IR_DEST_SLAVE5
 *
 * =====================================================================
 *  【setup() でやること】
 *    1. Serial.begin()          デバッグ用シリアル通信の開始
 *    2. Rx::getInstance().begin() IR 受信の初期化（FALLING 割り込み登録）
 *    3. 未実装モジュールの初期化呼び出し
 *
 *  【loop() でやること】
 *    1. Rx::getInstance().decode()  受信パケットが揃ったか確認
 *    2. Packet::parse()             パケットを復号し誤り訂正/検出 (SEC-DED)
 *    3. 宛先フィルタリング          自機宛またはブロードキャストのみ処理
 *    4. handlePacket()              コマンドを種類ごとのハンドラに振り分ける
 *
 *  【パケット受信の流れ】
 *
 *    [割り込み層: onEdge()]
 *      FALLING エッジ発生 → micros() でエッジ間隔を計測
 *      → 状態機械でリーダー検出 / ビット判定 / パケット組み立て
 *      → 24bit 揃ったら _ready フラグを立てる
 *
 *    [アプリ層: loop()]
 *      decode()         → _ready を確認してアトミックにパケット取り出し
 *      Packet::parse()  → フィールド抽出 + 誤り訂正/検出 (SEC-DED)
 *      handlePacket()   → PLAY/STOP/BPM/SYNC に応じた処理を実行
 *
 * =====================================================================
 *  【ピン配置】
 *   D2 : IR 受信 (OSRB38C9AA, Rx クラスが管理)
 * =====================================================================
 */

// ============================================================
//  インクルード
// ============================================================
// #include "../Shared/IrDef.h"   // 共通定数・コマンド定義
// #include "../Shared/Packet.h"  // パケット生成・解析
#include "IrDef.h"
#include "Packet.h"


#include "Rx.h"      // IR 受信クラス（シングルトン）
#include "Config.h"  // プロジェクト固有設定（現在は空ファイル）

// 以下は各モジュール実装後にコメントを外す
#include "Player.h"    // 音楽・メトロノーム再生
#include "LedCtrl.h"   // 演出 LED 制御

// ============================================================
//  ★ 機体ごとに書き換える: このスレーブ機の ID ★
// ============================================================
/**
 * @brief このスレーブ機のアドレス
 *
 * マスタから宛先 = この値 または IR_DEST_ALL(0x0) のパケットだけ処理する。
 * 5 台それぞれに異なる値を設定してください。
 */
constexpr uint8_t MY_SLAVE_ID = IR_DEST_SLAVE1;  // ← ここを 1〜5 で変更

// ============================================================
//  グローバル変数（アプリケーション状態）
// ============================================================
/** 受信した最新の BPM 値 */
static uint16_t g_bpm     = 120;

/** 再生中フラグ (PLAY で true / STOP で false) */
static bool     g_playing = false;

/** ── 反復送信の重複除去用: 直前に実行した論理コマンド ──
 *  (cmd, data, seq) が直前の実行と一致したら反復フレームとみなして無視する。
 *  初期値 0xFF はどのコマンド値とも一致しないため、初回は必ず実行される。*/
static uint8_t  g_lastCmd  = 0xFF;
static uint8_t  g_lastData = 0xFF;
static uint8_t  g_lastSeq  = 0xFF;

/** 音楽再生オブジェクト */
static Player   player;

/** 可視光 LED 制御オブジェクト */
static LedCtrl  ledCtrl;

// デバッグダンプ用変数（使用時はコメントを外す）
// static uint32_t g_lastDebugMs = 0;
// constexpr uint32_t DEBUG_INTERVAL_MS = 2000;

// ============================================================
//  前方宣言
// ============================================================
static bool isDuplicate(uint8_t cmd, uint8_t data, uint8_t seq);
static void handlePacket(uint8_t dest, uint8_t cmd, uint8_t data);
static void onPlay();
static void onStop();
static void onBpm(uint8_t bpm);
static void onSync(uint8_t beatCount);

// ============================================================
//  setup()
// ============================================================
void setup() {
    // ── 1. シリアル初期化（デバッグ用） ──────────────────────
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    Serial.print(F("========================================\n"));
    Serial.print(F(" 赤外線同期輪唱システム --- Slave 起動\n"));
    Serial.print(F(" ID = 0x")); Serial.println(MY_SLAVE_ID, HEX);
    Serial.println(F("========================================"));

    // ── 2. IR 受信モジュール初期化 ────────────────────────────
    // D2 ピンを INPUT に設定し、FALLING エッジ割り込みを登録する。
    // これ以降、IR 信号が来るたびに onEdge() が自動で呼ばれる。
    Rx::getInstance().begin();

    // ── 3. 演奏・LED モジュール初期化 ───────────────────────
    player.begin();
    // 機体番号 (MY_SLAVE_ID) に応じて役割（旋律/リズム）と楽器を選択する。
    // 対応表は Song.h の VOICES[] を参照・編集すること。
    // 輪唱の入りタイミングは Master が管理し、PLAY 受信で即演奏を開始する。
    player.setVoice(MY_SLAVE_ID);
    ledCtrl.begin();

    Serial.println(F("受信待機中..."));
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // ── 1. 受信パケットの取り出し ──────────────────────────────
    // decode() は 24bit が揃ったときだけ true を返す。
    // 揃っていなければ即座に false を返すので loop() をブロックしない。
    uint32_t rawPacket;
    if (Rx::getInstance().decode(rawPacket)) {

        // ── 2. パケットの復号 & 誤り訂正/検出 (SEC-DED) ────────
        // Packet::parse() は以下を行う:
        //   a) 24bit からフィールド（dest/cmd/data/seq）を抽出
        //   b) 拡張ハミングでシンドロームを計算し誤りを判定
        //   c) OK / CORRECTED（単一誤り訂正済み）/ UNCORRECTABLE を返す
        uint8_t dest, cmd, data, seq;
        const ParseResult res = Packet::parse(rawPacket, dest, cmd, data, seq);

        if (res == ParseResult::UNCORRECTABLE) {
            // 2bit 誤り等で訂正不能 → 安全側で破棄する。
            // 欠落・廃棄した分は Master の反復送信（連送）が救済する。
            Serial.print(F("[ERR] 訂正不能パケット破棄 raw=0x"));
            Serial.println(rawPacket, HEX);

        } else {
            // OK または CORRECTED（単一誤りを訂正済み）→ 受理する
            if (res == ParseResult::CORRECTED) {
                Serial.print(F("[FEC] 単一ビット誤りを訂正 raw=0x"));
                Serial.println(rawPacket, HEX);
            }

            // ── 3. 宛先フィルタリング ──────────────────────────
            // 条件: dest == IR_DEST_ALL (ブロードキャスト)
            //    OR dest == MY_SLAVE_ID (自機宛の個別指定)
            // それ以外は他機宛なので無視する
            if (dest == IR_DEST_ALL || dest == MY_SLAVE_ID) {
                // ── 4. SEQ による重複除去 → コマンド振り分け ──
                // PLAY/STOP/BPM は 3 連送されるため、(cmd,data,seq) が直前と
                // 同一の反復フレームは無視して 1 回だけ実行する。
                // SYNC は毎拍送信のため重複除去の対象外とする。
                const bool isSync = (cmd == static_cast<uint8_t>(IrCmd::SYNC));
                if (isSync || !isDuplicate(cmd, data, seq)) {
                    handlePacket(dest, cmd, data);
                }
            }
            // else: 他機宛 → 何もしない（無視）
        }
    }

    // ── 5. 未実装モジュールの更新（実装後にコメントを外す） ───
    player.update();
    ledCtrl.update();

    // ── 6. 赤外線受信状況の定期ダンプ（デバッグ時はコメントを外す） ──
    // const uint32_t now = millis();
    // if (now - g_lastDebugMs >= DEBUG_INTERVAL_MS) {
    //     g_lastDebugMs = now;
    //     Rx &rx = Rx::getInstance();
    //     uint32_t hist[Rx::HIST_BUCKETS];
    //     rx.getHistogram(hist);
    //     Serial.println(F("-------- IR RX STATUS --------"));
    //     Serial.print(F("  FALLING edges   : ")); Serial.println(rx.getEdgeCount());
    //     Serial.print(F("  Leader detected : ")); Serial.println(rx.getLeaderCount());
    //     Serial.print(F("  Bit errors      : ")); Serial.println(rx.getErrorCount());
    //     Serial.print(F("  Last interval   : ")); Serial.print(rx.getLastInterval()); Serial.println(F(" us"));
    //     Serial.print(F("  Max  interval   : ")); Serial.print(rx.getMaxInterval());  Serial.println(F(" us"));
    //     if (rx.getErrorCount() > 0) {
    //         Serial.print(F("  Last bad intv   : ")); Serial.print(rx.getLastBadInterval()); Serial.println(F(" us"));
    //     }
    //     Serial.print(F("  State           : "));
    //     if (rx.isReceiving()) {
    //         Serial.print(F("RECEIVING (bit ")); Serial.print(rx.getBitProgress()); Serial.println(F("/24)"));
    //     } else if (rx.isLeaderDetected()) {
    //         Serial.println(F("LEADER_DETECTED"));
    //     } else {
    //         Serial.println(F("IDLE"));
    //     }
    //     Serial.println(F("-- Interval histogram ---------"));
    //     Serial.print(F("  [noise] <700us     : ")); Serial.println(hist[0]);
    //     Serial.print(F("  [bit0]  700-1600us : ")); Serial.println(hist[1]);
    //     Serial.print(F("  [bit1]  1600-3000us: ")); Serial.println(hist[2]);
    //     Serial.print(F("  [mid]   3000-11000 : ")); Serial.println(hist[3]);
    //     Serial.print(F("  [leader]11000-16000: ")); Serial.println(hist[4]);
    //     Serial.print(F("  [gap]   >16000us   : ")); Serial.println(hist[5]);
    //     Serial.println(F("------------------------------"));
    // }
}

// ============================================================
//  isDuplicate() ── 反復送信フレームの重複除去
// ============================================================
/**
 * @brief 直前に実行した論理コマンドと同一かを判定し、状態を更新する
 *
 * @param cmd  コマンド値
 * @param data データ
 * @param seq  論理コマンド連番 (2bit)
 * @return 直前の実行と (cmd,data,seq) が一致すれば true（＝反復フレーム→無視）
 *
 * @details
 *   Master は重要コマンドを同一 SEQ で 3 連送する。最初に検証を通った 1 枚で
 *   実行し、残りの反復フレームは本関数が true を返して無視させる。
 *   新しい論理コマンドは SEQ が変わるため false となり、正しく 1 回だけ実行される。
 *   （SYNC は毎拍送信のため、呼び出し側で本判定の対象外としている。）
 */
static bool isDuplicate(uint8_t cmd, uint8_t data, uint8_t seq) {
    if (cmd == g_lastCmd && data == g_lastData && seq == g_lastSeq) {
        return true;  // 直前と同一 → 反復フレームとみなす
    }
    // 新しい論理コマンド → 状態を更新して実行を許可する
    g_lastCmd  = cmd;
    g_lastData = data;
    g_lastSeq  = seq;
    return false;
}

// ============================================================
//  handlePacket() ── コマンドの種類ごとに処理を振り分ける
// ============================================================
/**
 * @brief 受信・検証済みパケットのコマンドを対応ハンドラへ振り分ける
 *
 * @param dest パケットの宛先（ログ出力用）
 * @param cmd  コマンド値 (IrCmd::PLAY 等を uint8_t でキャストした値)
 * @param data コマンドに付随するデータ
 */
static void handlePacket(uint8_t dest, uint8_t cmd, uint8_t data) {
    // 低レベルな生パケットログ（デバッグ時はコメントを外す）
    // Serial.print(F("[RX] dest=0x")); Serial.print(dest, HEX);
    // Serial.print(F(" cmd=0x"));     Serial.print(cmd, HEX);
    // Serial.print(F(" data="));      Serial.println(data);

    // コマンド値で分岐
    // IrCmd を uint8_t にキャストして switch の定数式として使う
    switch (cmd) {

    case static_cast<uint8_t>(IrCmd::PLAY):
        onPlay();
        break;

    case static_cast<uint8_t>(IrCmd::STOP):
        onStop();
        break;

    case static_cast<uint8_t>(IrCmd::BPM):
        onBpm(data);
        break;

    case static_cast<uint8_t>(IrCmd::SYNC):
        onSync(data);
        break;

    default:
        // 定義外のコマンド値は警告を出して無視
        Serial.print(F("[WARN] 未知の cmd=0x"));
        Serial.println(cmd, HEX);
        break;
    }
}

// ============================================================
//  コマンドハンドラ群
//  ── Player / LedCtrl の実装後に TODO 行を肉付けする ──
// ============================================================

/**
 * @brief PLAY コマンドを受けたときの処理
 *
 * @details
 *   この関数が呼ばれるときには既に BPM コマンドを受信済みのはず
 *   （マスタが PLAY の直前に BPM を送る設計のため）。
 *   g_bpm を使って演奏を開始する。
 */
static void onPlay() {
    if (g_playing) return;  // 既に再生中なら二重起動を防ぐ

    g_playing = true;
    Serial.print(F("[PLAY] 再生開始 BPM=")); Serial.println(g_bpm);

    player.play(g_bpm);
    // TODO: ledCtrl.startEffect();
}

/**
 * @brief STOP コマンドを受けたときの処理
 */
static void onStop() {
    if (!g_playing) return;  // 既に停止中なら何もしない

    g_playing = false;
    Serial.println(F("[STOP] 再生停止"));

    player.stop();
    // TODO: ledCtrl.stopEffect();
}

/**
 * @brief BPM コマンドを受けたときの処理
 *
 * @param bpm 新しい BPM 値 (40〜240)
 *
 * @details
 *   再生中に受信した場合はテンポを即座に変更する。
 *   再生前に受信した場合は次回 PLAY 時のテンポとして保持する。
 */
static void onBpm(uint8_t encodedBpm) {
    // デコード: encodedBpm * BPM_IR_STEP + BPM_IR_MIN
    g_bpm = (uint16_t)encodedBpm * BPM_IR_STEP + BPM_IR_MIN;
    Serial.print(F("[BPM] ")); Serial.println(g_bpm);

    player.setBpm(g_bpm);
    // TODO: scheduler.setBpm(g_bpm);  // 拍タイミング更新
}

/**
 * @brief SYNC コマンドを受けたときの処理
 *
 * @param beatCount マスタの拍カウンタ (0〜255 の繰り返し)
 *
 * @details
 *   マスタが 1 拍ごとに送ってくるタイミング信号。
 *   演奏のズレをここで補正する。
 *
 *   【beatCount の活用例】
 *     beatCount % 4 == 0  → 4/4 拍子の小節先頭 → 位相リセット
 *     beatCount % 2 == 0  → 2 拍ごと → ダウンビート処理
 *     beatCount == 0      → 最初の拍 → フルリセット
 *
 *   【輪唱への応用】
 *     各スレーブに「何拍ずらして再生するか」のオフセットを持たせる。
 *     例: スレーブ N 号機は (N-1) × 4 拍遅らせて PLAY する設計にすれば
 *     自動的に輪唱になる。
 */
static void onSync(uint8_t beatCount) {
    ledCtrl.flash();       // 受信タイミングで可視光 LED を点滅
    player.sync(beatCount); // drift 測定・位相補正・ログは Player 内で行う
    //       内部でドリフト量を計測し、必要なら再生速度を微調整する（将来実装）
}
