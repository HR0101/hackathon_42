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
#include "LedCtrl.h"    // 演出 LED 制御

// ============================================================
//  定数
// ============================================================
constexpr uint16_t BPM_MIN     = 10;   ///< BPM 最小値
constexpr uint16_t BPM_MAX     = 600;  ///< BPM 最大値
constexpr uint16_t BPM_DEFAULT = 120;  ///< 起動時の初期 BPM
constexpr uint16_t BPM_STEP    = 5;    ///< bpm+/bpm- 1 回あたりの変化量

/** シリアル受信バッファのサイズ (コマンド最大文字数 + 1) */
constexpr uint8_t  SERIAL_BUF_SIZE = 32;

// ============================================================
//  輪唱スケジュール（★ ここで輪唱の入りタイミングを管理する ★）
// ============================================================
/**
 * 各スレーブが「再生開始してから何拍後に演奏を始めるか」を Master が管理する。
 * Master は PLAY コマンドを各スレーブへ個別に時間差で送ることで輪唱を作る。
 * スレーブ側は PLAY を受け取った瞬間に即演奏を開始する（自己遅延なし）。
 *
 *   ROUND_START_BEAT[i] … i 号機(1始まり)の開始拍
 *   ROUND_SLAVE_DEST[i] … i 号機の宛先アドレス
 *
 * 【機体構成】 1〜3号機 = 旋律（輪唱）, 4〜5号機 = リズム（ドラム）
 *   - 旋律 3 機は 8 拍（2小節）ずつずらして 3 部輪唱にする → 0, 8, 16
 *   - リズム 2 機は輪唱初回（拍 0）から演奏を開始する        → 0, 0
 *   ※ 各機の役割・楽器は Slave 側 Song.h の VOICES[] と対応させること。
 */
constexpr uint8_t  ROUND_SLAVE_COUNT = 5;
constexpr uint16_t ROUND_START_BEAT[ROUND_SLAVE_COUNT] = { 0, 8, 16, 0, 0 };
constexpr uint8_t  ROUND_SLAVE_DEST[ROUND_SLAVE_COUNT] = {
    IR_DEST_SLAVE1, IR_DEST_SLAVE2, IR_DEST_SLAVE3, IR_DEST_SLAVE4, IR_DEST_SLAVE5
};

// ============================================================
//  インスタンス / グローバル変数
// ============================================================
/** IR 送信オブジェクト */
static Tx tx;

/** 可視光 LED 制御オブジェクト */
static LedCtrl ledCtrl;

/** 現在の BPM 値 */
static uint16_t g_bpm        = BPM_DEFAULT;

/** 再生中フラグ */
static bool     g_playing    = false;

/** 最後に SYNC を送った時刻 [ms]（ドリフト補正のため累積加算で管理）*/
static uint32_t g_lastSyncMs = 0;

/** 再生開始からの経過拍数（輪唱スケジュールと SYNC の拍カウンタを兼ねる）*/
static uint16_t g_beat = 0;

/** 論理コマンド連番 (2bit, 0〜3 でラップ)。
 *  反復送信の重複除去に使用。SYNC 以外を送るたびに +1 する。*/
static uint8_t  g_msgSeq = 0;

/** 各スレーブへ開始 PLAY を送信済みか（輪唱の二重送信防止）*/
static bool     g_started[ROUND_SLAVE_COUNT] = { false };

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
static void dispatchRoundStarts(uint16_t beat);
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
    ledCtrl.begin();

    // ── 起動時に全スレーブへ初期 BPM を送信 ─────────────────
    // スレーブ側の setup() 完了を待ってから送信する
    delay(500);
    sendCommand(IR_DEST_ALL, IrCmd::BPM, (uint8_t)((g_bpm - BPM_IR_MIN) / BPM_IR_STEP));

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
    ledCtrl.update();
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
        sendCommand(IR_DEST_ALL, IrCmd::BPM, (uint8_t)((g_bpm - BPM_IR_MIN) / BPM_IR_STEP));

        // STEP 2: BPM パケットの受信・処理が完了するまで少し待つ
        delay(30);

        // STEP 3: 輪唱スケジュールを初期化して再生状態へ
        //         PLAY は全員一斉ではなく、各スレーブへ時間差で個別送信する。
        //         輪唱タイミングはすべて Master(ROUND_START_BEAT[]) が管理する。
        g_beat       = 0;
        g_playing    = true;
        g_lastSyncMs = millis();  // SYNC/拍タイマーをここからリスタート
        for (uint8_t i = 0; i < ROUND_SLAVE_COUNT; i++) g_started[i] = false;

        // STEP 4: 開始拍 0 のスレーブを今すぐ始動させる（残りは loop で時間差送信）
        dispatchRoundStarts(g_beat);
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
            sendCommand(IR_DEST_ALL, IrCmd::BPM, (uint8_t)((g_bpm - BPM_IR_MIN) / BPM_IR_STEP));
        } else {
            Serial.print(F("[INFO] BPM 上限 (")); Serial.print(BPM_MAX);
            Serial.println(F(") に達しています。"));
        }
        printStatus();

    // ---------- bpm- ----------
    } else if (strcmp(lower, "bpm-") == 0) {
        if (g_bpm >= BPM_MIN + BPM_STEP) {
            g_bpm -= BPM_STEP;
            sendCommand(IR_DEST_ALL, IrCmd::BPM, (uint8_t)((g_bpm - BPM_IR_MIN) / BPM_IR_STEP));
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
            g_bpm = static_cast<uint16_t>(val);
            sendCommand(IR_DEST_ALL, IrCmd::BPM, (uint8_t)((g_bpm - BPM_IR_MIN) / BPM_IR_STEP));
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
 *   STEP 1: SEQ と反復回数を決定する
 *           SYNC は毎拍送信の自己回復型のため SEQ を進めず 1 回だけ送る。
 *           PLAY/STOP/BPM は論理コマンドごとに SEQ を 1 進め、
 *           同一フレームを IR_REPEAT_COUNT 回連送する（時間冗長）。
 *
 *   STEP 2: Packet::build() で 24bit パケットを生成する
 *           宛先・コマンド・データ・SEQ を所定ビット位置に配置し、
 *           拡張ハミング SEC-DED パリティを自動計算して付与する。
 *
 *   STEP 3: tx.sendFrame() で NEC フォーマットで送信する
 *           ブロッキング処理。最長フレームで約 68ms かかる。
 *           反復時はフレーム間に IR_REPEAT_GAP_MS の短いギャップを空ける。
 */
static void sendCommand(uint8_t dest, IrCmd cmd, uint8_t data) {
    // STEP 1: SEQ と反復回数を決定
    uint8_t seq;
    uint8_t repeat;
    if (cmd == IrCmd::SYNC) {
        // SYNC は重複除去対象外。SEQ は進めず現在値を流用し、1 回だけ送る。
        seq    = g_msgSeq;
        repeat = 1;
    } else {
        // 論理コマンドごとに SEQ を +1（2bit ラップ）。反復では増やさない。
        g_msgSeq = (g_msgSeq + 1) & IR_SEQ_MAX;
        seq      = g_msgSeq;
        repeat   = IR_REPEAT_COUNT;
    }

    // STEP 2: 24bit パケット生成（拡張ハミング SEC-DED 符号化）
    const uint32_t packet = Packet::build(
        dest,
        static_cast<uint8_t>(cmd),
        data,
        seq
    );

    // STEP 3: 赤外線送信（重要コマンドは同一フレームを連送）
    for (uint8_t r = 0; r < repeat; r++) {
        tx.sendFrame(packet);
        if (r < repeat - 1) delay(IR_REPEAT_GAP_MS);  // 反復フレーム間ギャップ
    }

    // デバッグ出力
    Serial.print(F("  [TX] dest=0x")); Serial.print(dest, HEX);
    Serial.print(F(" cmd=0x"));        Serial.print(static_cast<uint8_t>(cmd), HEX);
    Serial.print(F(" data="));         Serial.print(data);
    Serial.print(F(" seq="));          Serial.print(seq);
    Serial.print(F(" x"));             Serial.print(repeat);
    Serial.print(F(" pkt=0x"));        Serial.println(packet, HEX);
}

// ============================================================
//  updateSyncTiming() ── 1 拍ごとの SYNC 送信 + 輪唱ディスパッチ
// ============================================================
/**
 * @brief 再生中、1 拍ごとに輪唱の始動判定と SYNC 送信を行う
 *
 * @details
 *   1 拍の間隔 [ms] = 60000 / BPM
 *   (例) BPM=120 → 500ms ごと
 *
 *   【ドリフト補正】
 *     g_lastSyncMs += beatMs とすることで、sendCommand() の処理時間が
 *     タイミングに積み重なるドリフトを防ぐ。
 *
 *   【輪唱の管理（Master 側）】
 *     g_beat（経過拍）を 1 拍ごとに加算し、dispatchRoundStarts() で
 *     開始拍に達したスレーブへ個別 PLAY を送る。輪唱の入りタイミングは
 *     すべて Master が制御し、スレーブは PLAY 受信で即演奏を開始する。
 *
 *   【SYNC の役割】
 *     全スレーブへ毎拍ブロードキャストし、各スレーブの内部タイマの
 *     位相ズレ（遅延）を補正させる。data には g_beat の下位 8bit を載せる。
 */
static void updateSyncTiming() {
    if (!g_playing) return;

    const uint32_t beatMs = 60000UL / g_bpm;

    if (millis() - g_lastSyncMs >= beatMs) {
        g_lastSyncMs += beatMs;  // 累積加算でドリフト防止
        g_beat++;                // 再生開始からの経過拍

        // ── 1. 輪唱の入りタイミング: 開始拍に達したスレーブを個別始動 ──
        dispatchRoundStarts(g_beat);

        // ── 2. 全スレーブへ SYNC を送信（位相ドリフト補正用）──
        //      data には拍カウンタの下位 8bit を載せる
        sendCommand(IR_DEST_ALL, IrCmd::SYNC, (uint8_t)(g_beat & 0xFF));
        ledCtrl.flash();  // 送信タイミングで可視光 LED を点滅
    }
}

// ============================================================
//  dispatchRoundStarts() ── 開始拍に達したスレーブへ個別 PLAY を送る
// ============================================================
/**
 * @brief 経過拍 beat に開始タイミングが一致したスレーブへ PLAY を個別送信する
 *
 * @param beat 再生開始からの経過拍数
 *
 * @details
 *   ROUND_START_BEAT[i] <= beat になった未始動スレーブへ PLAY を送り、
 *   g_started[i] を立てて二重送信を防ぐ。これにより輪唱の入りタイミングを
 *   すべて Master 側で制御する。スレーブは PLAY 受信で即演奏を開始する。
 */
static void dispatchRoundStarts(uint16_t beat) {
    for (uint8_t i = 0; i < ROUND_SLAVE_COUNT; i++) {
        if (!g_started[i] && beat >= ROUND_START_BEAT[i]) {
            sendCommand(ROUND_SLAVE_DEST[i], IrCmd::PLAY, 0);
            g_started[i] = true;
            Serial.print(F("[ROUND] "));
            Serial.print(i + 1);
            Serial.print(F("号機 開始 (拍 "));
            Serial.print(ROUND_START_BEAT[i]);
            Serial.println(F(")"));
        }
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
    Serial.println(F("  bpm <値>    BPM を指定値 (10〜600, 5刻み) に変更"));
    Serial.println(F("  bpm+        BPM を +5"));
    Serial.println(F("  bpm-        BPM を -5"));
    Serial.println(F("  sync        SYNC を今すぐ手動送信"));
    Serial.println(F("  status      現在の状態を表示"));
    Serial.println(F("  help / ?    このヘルプを表示"));
    Serial.println(F("--------------------------------------------"));
}
