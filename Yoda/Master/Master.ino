/**
 * @file    main.ino  (Master)
 * @brief   赤外線同期輪唱システム — マスタ機 メインスケッチ
 *
 * =====================================================================
 *  【操作方法】シリアルモニタから以下のコマンドを入力して送信する
 *
 *   play          → 全スレーブへ BPM を送ってから PLAY を送信
 *   stop          → 全スレーブへ STOP を送信
 *   bpm <値>      → BPM を指定値 (40〜240) に変更して全スレーブへ送信
 *   bpm+          → BPM を 5 増加して全スレーブへ送信
 *   bpm-          → BPM を 5 減少して全スレーブへ送信
 *   sync          → SYNC を今すぐ 1 回全スレーブへ送信（手動）
 *   status        → 現在の BPM・再生状態をシリアルに表示
 *   help          → コマンド一覧を表示
 *
 *  【入力の注意】
 *   - 大文字・小文字どちらでも可（例: PLAY / play / Play すべて有効）
 *   - 行末は LF (\n) または CR+LF どちらでも動作する
 *   - シリアルモニタの「改行なし」設定には対応しない
 *
 * =====================================================================
 *  【setup() でやること】
 *    1. Serial.begin()   シリアル通信の開始（コマンド受信も兼ねる）
 *    2. tx.begin()       IR 送信モジュールの初期化（GPT タイマー確保）
 *    3. 起動後すぐに全スレーブへ初期 BPM を送信
 *
 *  【loop() でやること】
 *    1. handleSerial()      シリアルバッファを読み、コマンド行が完成したら実行
 *    2. updateSyncTiming()  再生中のとき 1 拍ごとに全スレーブへ SYNC を送信
 * =====================================================================
 */

// ============================================================
//  インクルード
// ============================================================
// #include "../Shared/IrDef.h"   // 共通定数・コマンド定義
// #include "../Shared/Packet.h"  // パケット生成・解析
#include "IrDef.h"
#include "Packet.h"

#include "Tx.h"      // IR 送信クラス
#include "Config.h"  // プロジェクト固有設定（現在は空ファイル）

// 以下は各モジュール実装後にコメントを外す
// #include "Scheduler.h"  // BPM スケジューラ（拍タイミング生成）
// #include "LedCtrl.h"    // 演出 LED 制御

// ============================================================
//  定数
// ============================================================
constexpr uint8_t  BPM_MIN     = 40;   ///< BPM 最小値
constexpr uint8_t  BPM_MAX     = 240;  ///< BPM 最大値
constexpr uint8_t  BPM_DEFAULT = 120;  ///< 起動時の初期 BPM
constexpr uint8_t  BPM_STEP    = 5;    ///< bpm+/bpm- 1 回あたりの変化量

/** シリアル受信バッファのサイズ (コマンド最大文字数 + 1) */
constexpr uint8_t  SERIAL_BUF_SIZE = 32;

// ============================================================
//  インスタンス / グローバル変数
// ============================================================
/** IR 送信オブジェクト */
static Tx tx;

/** 現在の BPM 値 */
static uint8_t  g_bpm        = BPM_DEFAULT;

/** 再生中フラグ */
static bool     g_playing    = false;

/** 最後に SYNC を送った時刻 [ms]（ドリフト補正のため累積加算で管理）*/
static uint32_t g_lastSyncMs = 0;

// ── シリアル受信バッファ ─────────────────────────────────────
/** 1 コマンド分の文字を溜めるバッファ */
static char     g_serialBuf[SERIAL_BUF_SIZE];
/** バッファに書き込んでいる現在位置 */
static uint8_t  g_serialPos  = 0;

// ============================================================
//  前方宣言
// ============================================================
static void sendCommand(uint8_t dest, IrCmd cmd, uint8_t data = 0);
static void handleSerial();
static void execCommand(const char* line);
static void updateSyncTiming();
static void printStatus();
static void printHelp();

// ============================================================
//  setup()
// ============================================================
void setup() {
    // ── シリアル初期化 ─────────────────────────────────────
    // ボーレート 115200bps。シリアルモニタも同じ値に設定すること。
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    Serial.println(F("========================================"));
    Serial.println(F(" 赤外線同期輪唱システム --- Master 起動"));
    Serial.println(F("========================================"));

    // ── IR 送信モジュール初期化 ────────────────────────────
    // 内部で FspTimer を使って GPT チャンネルを確保し、
    // 76kHz 周期割り込みを起動する（搬送波はまだ出力しない）
    tx.begin();

    // ── 未実装モジュール初期化（実装後にコメントを外す）──────
    // scheduler.begin();
    // ledCtrl.begin();

    // ── 起動時に全スレーブへ初期 BPM を送信 ─────────────────
    // スレーブ側の setup() 完了を待ってから送信する
    delay(500);
    sendCommand(IR_DEST_ALL, IrCmd::BPM, g_bpm);

    printStatus();
    printHelp();
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // ── 1. シリアル入力の処理 ──────────────────────────────
    // 受信文字をバッファに溜め、改行が来たらコマンドを実行する
    handleSerial();

    // ── 2. 定期 SYNC 送信 ──────────────────────────────────
    // 再生中のときだけ 1 拍ごとのタイミングで SYNC を全員に送る
    updateSyncTiming();

    // ── 3. 未実装モジュールの更新（実装後にコメントを外す）──
    // scheduler.update();
    // ledCtrl.update();
}

// ============================================================
//  handleSerial() ── シリアル受信とコマンドライン組み立て
// ============================================================
/**
 * @brief シリアルバッファを読み取り、改行が来たらコマンドを実行する
 *
 * @details
 *   loop() が呼ばれるたびに Serial.available() で受信文字を確認し、
 *   文字が来ていれば g_serialBuf に追記する。
 *   '\n'（LF）または '\r'（CR）を受け取ったら 1 行完成とみなし、
 *   execCommand() でコマンド文字列を解析・実行する。
 *
 *   バッファオーバーフロー防止:
 *     SERIAL_BUF_SIZE - 1 文字を超えた場合はバッファをクリアする。
 */
static void handleSerial() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());

        if (c == '\n' || c == '\r') {
            // 改行 → 1 行完成
            if (g_serialPos > 0) {
                g_serialBuf[g_serialPos] = '\0';  // 終端文字を追加
                execCommand(g_serialBuf);          // コマンドを実行
                g_serialPos = 0;                   // バッファをリセット
            }
            // g_serialPos == 0 のとき（空行）は何もしない

        } else if (g_serialPos < SERIAL_BUF_SIZE - 1) {
            // 通常文字 → バッファに追記
            g_serialBuf[g_serialPos++] = c;

        } else {
            // バッファ満杯 → 不正入力としてリセット
            Serial.println(F("[ERR] コマンドが長すぎます。バッファをクリアしました。"));
            g_serialPos = 0;
        }
    }
}

// ============================================================
//  execCommand() ── コマンド文字列の解析と実行
// ============================================================
/**
 * @brief 1 行のコマンド文字列を解析して対応する処理を実行する
 *
 * @param line  '\0' 終端のコマンド文字列（例: "play", "bpm 140"）
 *
 * @details
 *   受け取った文字列全体を小文字化してから比較する。
 *   "bpm <数値>" のように引数を持つコマンドは strtol() で数値を解析する。
 *
 *   対応コマンド一覧:
 *     play        PLAY シーケンスを実行（BPM送信 → PLAY送信）
 *     stop        STOP を全スレーブへ送信
 *     bpm <値>    BPM を指定値に変更して全スレーブへ送信
 *     bpm+        BPM を BPM_STEP 増加
 *     bpm-        BPM を BPM_STEP 減少
 *     sync        SYNC を今すぐ 1 回送信（手動トリガー）
 *     status      現在状態をシリアルに表示
 *     help / ?    コマンド一覧を表示
 */
static void execCommand(const char* line) {
    // ── 文字列全体を小文字化（コピーを作って操作）─────────
    char lower[SERIAL_BUF_SIZE];
    uint8_t i = 0;
    while (line[i] != '\0' && i < SERIAL_BUF_SIZE - 1) {
        // 'A'〜'Z' を 'a'〜'z' に変換（それ以外はそのまま）
        lower[i] = (line[i] >= 'A' && line[i] <= 'Z')
                   ? static_cast<char>(line[i] + 32)
                   : line[i];
        i++;
    }
    lower[i] = '\0';

    // ── 入力をシリアルにエコー表示 ─────────────────────────
    Serial.print(F("> ")); Serial.println(lower);

    // ── コマンド分岐 ────────────────────────────────────────

    // ---------- play ----------
    if (strcmp(lower, "play") == 0) {
        if (g_playing) {
            Serial.println(F("[INFO] すでに再生中です。"));
            return;
        }
        // STEP 1: 最新 BPM を全スレーブへ送る
        //         → スレーブが古い BPM で再生するのを防ぐ
        sendCommand(IR_DEST_ALL, IrCmd::BPM, g_bpm);

        // STEP 2: BPM パケットの受信・処理が完了するまで少し待つ
        delay(30);

        // STEP 3: PLAY コマンドを全スレーブへ送る
        //         → 全スレーブが同じタイミングで一斉に再生を開始する
        sendCommand(IR_DEST_ALL, IrCmd::PLAY, 0);

        g_playing    = true;
        g_lastSyncMs = millis();  // SYNC タイマーをここからリスタート
        printStatus();

    // ---------- stop ----------
    } else if (strcmp(lower, "stop") == 0) {
        if (!g_playing) {
            Serial.println(F("[INFO] すでに停止中です。"));
            return;
        }
        sendCommand(IR_DEST_ALL, IrCmd::STOP, 0);
        g_playing = false;
        printStatus();

    // ---------- bpm+ ----------
    } else if (strcmp(lower, "bpm+") == 0) {
        if (g_bpm <= BPM_MAX - BPM_STEP) {
            g_bpm += BPM_STEP;
            sendCommand(IR_DEST_ALL, IrCmd::BPM, g_bpm);
        } else {
            Serial.print(F("[INFO] BPM 上限 (")); Serial.print(BPM_MAX);
            Serial.println(F(") に達しています。"));
        }
        printStatus();

    // ---------- bpm- ----------
    } else if (strcmp(lower, "bpm-") == 0) {
        if (g_bpm >= BPM_MIN + BPM_STEP) {
            g_bpm -= BPM_STEP;
            sendCommand(IR_DEST_ALL, IrCmd::BPM, g_bpm);
        } else {
            Serial.print(F("[INFO] BPM 下限 (")); Serial.print(BPM_MIN);
            Serial.println(F(") に達しています。"));
        }
        printStatus();

    // ---------- bpm <値> ----------
    // "bpm" の後にスペースと数値が続く形式（例: "bpm 140"）
    } else if (strncmp(lower, "bpm ", 4) == 0) {
        // 数値部分を "bpm " (4文字) の直後から取り出す
        const long val = strtol(lower + 4, nullptr, 10);

        if (val < BPM_MIN || val > BPM_MAX) {
            Serial.print(F("[ERR] BPM は "));
            Serial.print(BPM_MIN);
            Serial.print(F("〜"));
            Serial.print(BPM_MAX);
            Serial.println(F(" の範囲で指定してください。"));
        } else {
            g_bpm = static_cast<uint8_t>(val);
            sendCommand(IR_DEST_ALL, IrCmd::BPM, g_bpm);
            printStatus();
        }

    // ---------- sync ----------
    // 手動で今すぐ SYNC を 1 回送信する（自動 SYNC とは別）
    } else if (strcmp(lower, "sync") == 0) {
        static uint8_t manualBeat = 0;
        sendCommand(IR_DEST_ALL, IrCmd::SYNC, manualBeat++);
        Serial.println(F("[INFO] SYNC を手動送信しました。"));

    // ---------- status ----------
    } else if (strcmp(lower, "status") == 0) {
        printStatus();

    // ---------- help / ? ----------
    } else if (strcmp(lower, "help") == 0 || strcmp(lower, "?") == 0) {
        printHelp();

    // ---------- 不明なコマンド ----------
    } else {
        Serial.print(F("[ERR] 不明なコマンド: \""));
        Serial.print(lower);
        Serial.println(F("\"  \"help\" でコマンド一覧を確認できます。"));
    }
}

// ============================================================
//  sendCommand() ── パケット生成 + IR 送信のラッパー
// ============================================================
/**
 * @brief 指定コマンドを宛先スレーブへ送信する
 *
 * @param dest  宛先アドレス (IR_DEST_ALL = 全員, IR_DEST_SLAVE1〜5 = 個別)
 * @param cmd   コマンド (IrCmd::PLAY / STOP / BPM / SYNC)
 * @param data  コマンドに付随するデータ (BPM 値など。不要なら 0)
 *
 * @details
 *   STEP 1: Packet::build() で 24bit パケットを生成する
 *           宛先・コマンド・データを所定ビット位置に配置し、
 *           XOR チェックバイトを自動計算して付与する。
 *
 *   STEP 2: tx.sendFrame() で NEC フォーマットで送信する
 *           ブロッキング処理。最長フレームで約 68ms かかる。
 */
static void sendCommand(uint8_t dest, IrCmd cmd, uint8_t data) {
    // STEP 1: 24bit パケット生成
    const uint32_t packet = Packet::build(
        dest,
        static_cast<uint8_t>(cmd),
        data
    );

    // STEP 2: 赤外線送信
    tx.sendFrame(packet);

    // デバッグ出力
    Serial.print(F("  [TX] dest=0x")); Serial.print(dest, HEX);
    Serial.print(F(" cmd=0x"));        Serial.print(static_cast<uint8_t>(cmd), HEX);
    Serial.print(F(" data="));         Serial.print(data);
    Serial.print(F(" pkt=0x"));        Serial.println(packet, HEX);
}

// ============================================================
//  updateSyncTiming() ── 自動 SYNC 定期送信
// ============================================================
/**
 * @brief 再生中のとき BPM から計算した間隔で SYNC を全スレーブへ送信する
 *
 * @details
 *   1 拍の間隔 [ms] = 60000 / BPM
 *   (例) BPM=120 → 500ms ごとに SYNC を送信
 *
 *   【ドリフト補正】
 *     g_lastSyncMs += beatMs とすることで、sendCommand() の処理時間が
 *     タイミングに積み重なるドリフトを防ぐ。
 *
 *   【data = beatCount の使い方】
 *     beatCount % 4 == 0 → 4/4 拍子の小節先頭
 *     beatCount % 2 == 0 → 2 拍ごとのダウンビート
 *     スレーブ側で「(N-1) × 4 拍後に PLAY する」ように設計すれば
 *     beatCount を使って輪唱の入りタイミングを制御できる。
 */
static void updateSyncTiming() {
    if (!g_playing) return;

    const uint32_t beatMs = 60000UL / g_bpm;

    if (millis() - g_lastSyncMs >= beatMs) {
        g_lastSyncMs += beatMs;  // 累積加算でドリフト防止

        static uint8_t beatCount = 0;
        sendCommand(IR_DEST_ALL, IrCmd::SYNC, beatCount);
        beatCount++;  // uint8_t のオーバーフローで 0 に自動リセット
    }
}

// ============================================================
//  printStatus() ── 現在状態の表示
// ============================================================
static void printStatus() {
    Serial.println(F("+-----------------+"));
    Serial.print(F("|  BPM   : "));
    // 3桁表示のために空白でパディング
    if (g_bpm < 100) Serial.print(F(" "));
    if (g_bpm <  10) Serial.print(F(" "));
    Serial.print(g_bpm);
    Serial.println(F("         |"));
    Serial.print(F("|  再生  : "));
    Serial.println(g_playing ? F("ON          |") : F("OFF         |"));
    Serial.println(F("+-----------------+"));
}

// ============================================================
//  printHelp() ── コマンド一覧の表示
// ============================================================
static void printHelp() {
    Serial.println(F("--- コマンド一覧 ----------------------------"));
    Serial.println(F("  play        全スレーブへ BPM 送信 -> PLAY"));
    Serial.println(F("  stop        全スレーブへ STOP 送信"));
    Serial.println(F("  bpm <値>    BPM を指定値 (40〜240) に変更"));
    Serial.println(F("  bpm+        BPM を +5"));
    Serial.println(F("  bpm-        BPM を -5"));
    Serial.println(F("  sync        SYNC を今すぐ手動送信"));
    Serial.println(F("  status      現在の状態を表示"));
    Serial.println(F("  help / ?    このヘルプを表示"));
    Serial.println(F("--------------------------------------------"));
}
