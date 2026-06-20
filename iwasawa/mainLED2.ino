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
 *    3. LED制御・音出力ピンの初期化
 *
 *  【loop() でやること】
 *    1. Rx::getInstance().decode()  受信パケットが揃ったか確認
 *    2. Packet::parse()             パケットを分解して XOR 整合性を検査
 *    3. 宛先フィルタリング          自機宛またはブロードキャストのみ処理
 *    4. handlePacket()              コマンドを種類ごとのハンドラに振り分ける
 *
 * =====================================================================
 *  【ピン配置】
 *   D2 : IR 受信 (OSRB38C9AA, Rx クラスが管理)
 *   D8 : ブザー／スピーカー出力
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
// #include "Player.h"    // 音楽・メトロノーム再生
#include "LedCtrl.h"   // 演出 LED 制御

// ============================================================
//  ★ 機体ごとに書き換える: このスレーブ機の ID ★
// ============================================================
constexpr uint8_t MY_SLAVE_ID = IR_DEST_SLAVE1;  // ← ここを 1〜5 で変更

// ============================================================
//  グローバル変数（アプリケーション状態）
// ============================================================

/** 受信した最新の BPM 値 */
static uint8_t  g_bpm     = 120;

/** 再生中フラグ (PLAY で true / STOP で false) */
static bool     g_playing = false;

/** 可視光 LED 制御オブジェクト */
static LedCtrl  ledCtrl;

// ============================================================
//  遅延測定・音出力用
// ============================================================

/** ブザーまたはスピーカーを接続するピン */
constexpr uint8_t SPEAKER_PIN = 8;

/** パケット受信完了時刻[us] */
static uint32_t g_packetReadyUs = 0;

/** パケット解析完了時刻[us] */
static uint32_t g_parseDoneUs = 0;

/** 遅延測定回数 */
static uint32_t g_delayCount = 0;

/** 平均計算用 */
static float g_sumParseDelayUs  = 0;
static float g_sumOutputDelayUs = 0;
static float g_sumTotalDelayUs  = 0;

// デバッグダンプ用変数（使用時はコメントを外す）
// static uint32_t g_lastDebugMs = 0;
// constexpr uint32_t DEBUG_INTERVAL_MS = 2000;

// ============================================================
//  前方宣言
// ============================================================
static void handlePacket(uint8_t dest, uint8_t cmd, uint8_t data);
static void onPlay();
static void onStop();
static void onBpm(uint8_t bpm);
static void onSync(uint8_t beatCount);

// 遅延測定用
static void printDelayLog(uint32_t soundStartUs);

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
    Rx::getInstance().begin();

    // ── 3. LED制御・音出力ピン初期化 ────────────────────────
    ledCtrl.begin();

    pinMode(SPEAKER_PIN, OUTPUT);
    noTone(SPEAKER_PIN);

    Serial.println(F("受信待機中..."));
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // ── 1. 受信パケットの取り出し ──────────────────────────────
    uint32_t rawPacket;
    if (Rx::getInstance().decode(rawPacket)) {

        // パケット受信完了時刻を記録
        g_packetReadyUs = micros();

        // ── 2. パケットの解析 & XOR 整合性チェック ────────────
        uint8_t dest, cmd, data;
        if (Packet::parse(rawPacket, dest, cmd, data)) {

            // パケット解析完了時刻を記録
            g_parseDoneUs = micros();

            // ── 3. 宛先フィルタリング ──────────────────────────
            if (dest == IR_DEST_ALL || dest == MY_SLAVE_ID) {
                // ── 4. コマンドの振り分け ──────────────────────
                handlePacket(dest, cmd, data);
            }

        } else {
            Serial.print(F("[ERR] XOR 不一致 raw=0x"));
            Serial.println(rawPacket, HEX);
        }
    }

    // ── 5. 未実装モジュールの更新（実装後にコメントを外す） ───
    // player.update();
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
//  handlePacket() ── コマンドの種類ごとに処理を振り分ける
// ============================================================
static void handlePacket(uint8_t dest, uint8_t cmd, uint8_t data) {
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
        Serial.print(F("[WARN] 未知の cmd=0x"));
        Serial.println(cmd, HEX);
        break;
    }
}

// ============================================================
//  コマンドハンドラ群
// ============================================================

/**
 * @brief PLAY コマンドを受けたときの処理
 */
static void onPlay() {
    // 2回目以降のPLAYでも測定できるように return しない
    bool wasPlaying = g_playing;

    g_playing = true;

    if (wasPlaying) {
        Serial.println(F("[PLAY] 再生中にPLAYを再受信"));
    } else {
        Serial.print(F("[PLAY] 再生開始 BPM="));
        Serial.println(g_bpm);
    }

    // 音出力開始時刻を記録
    uint32_t soundStartUs = micros();

    // 音を出力する
    // 例：かえるの合唱の最初の音として C5 付近を鳴らす
    tone(SPEAKER_PIN, 523);

    // 遅延測定結果を表示
    printDelayLog(soundStartUs);

    // TODO: player.play(g_bpm);
    // TODO: ledCtrl.startEffect();
}

/**
 * @brief STOP コマンドを受けたときの処理
 */
static void onStop() {
    if (!g_playing) return;

    g_playing = false;
    Serial.println(F("[STOP] 再生停止"));

    // 音を停止
    noTone(SPEAKER_PIN);

    // TODO: player.stop();
    // TODO: ledCtrl.stopEffect();
}

/**
 * @brief BPM コマンドを受けたときの処理
 */
static void onBpm(uint8_t bpm) {
    g_bpm = bpm;
    Serial.print(F("[BPM] "));
    Serial.println(g_bpm);

    // TODO: player.setBpm(bpm);
    // TODO: scheduler.setBpm(bpm);
}

/**
 * @brief SYNC コマンドを受けたときの処理
 */
static void onSync(uint8_t beatCount) {
    Serial.print(F("[SYNC] beat="));
    Serial.println(beatCount);

    ledCtrl.flash();  // 受信タイミングで可視光 LED を点滅

    if (beatCount % 4 == 0) {
        Serial.println(F("  => 小節先頭"));
    }

    // TODO: player.sync(beatCount);
}

// ============================================================
//  遅延測定ログ出力
// ============================================================

static void printDelayLog(uint32_t soundStartUs) {
    uint32_t parseDelayUs  = g_parseDoneUs - g_packetReadyUs;
    uint32_t outputDelayUs = soundStartUs - g_parseDoneUs;
    uint32_t totalDelayUs  = soundStartUs - g_packetReadyUs;

    // 測定回数を更新
    g_delayCount++;

    // 平均計算用に加算
    g_sumParseDelayUs  += parseDelayUs;
    g_sumOutputDelayUs += outputDelayUs;
    g_sumTotalDelayUs  += totalDelayUs;

    Serial.println(F("----- 遅延測定結果 -----"));

    Serial.print(F("測定回数: "));
    Serial.println(g_delayCount);

    Serial.print(F("受信完了 -> 解析完了: "));
    Serial.print(parseDelayUs);
    Serial.println(F(" us"));

    Serial.print(F("解析完了 -> 音出力開始: "));
    Serial.print(outputDelayUs);
    Serial.println(F(" us"));

    Serial.print(F("受信完了 -> 音出力開始: "));
    Serial.print(totalDelayUs);
    Serial.println(F(" us"));

    // 2回目以降は平均も表示
    if (g_delayCount >= 2) {
        Serial.println(F("----- 平均遅延 -----"));

        Serial.print(F("平均 受信完了 -> 解析完了: "));
        Serial.print(g_sumParseDelayUs / g_delayCount);
        Serial.println(F(" us"));

        Serial.print(F("平均 解析完了 -> 音出力開始: "));
        Serial.print(g_sumOutputDelayUs / g_delayCount);
        Serial.println(F(" us"));

        Serial.print(F("平均 受信完了 -> 音出力開始: "));
        Serial.print(g_sumTotalDelayUs / g_delayCount);
        Serial.println(F(" us"));
    }

    Serial.println(F("------------------------"));
}
