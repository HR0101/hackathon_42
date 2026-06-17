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
 *    3. LED出力ピンの初期化
 *
 *  【loop() でやること】
 *    1. Rx::getInstance().decode()  受信パケットが揃ったか確認
 *    2. Packet::parse()             パケットを分解して XOR 整合性を検査
 *    3. 宛先フィルタリング          自機宛またはブロードキャストのみ処理
 *    4. handlePacket()              コマンドを種類ごとのハンドラに振り分ける
 *    5. updateLed()                 LEDの消灯タイミングを管理
 *
 * =====================================================================
 *  【ピン配置】
 *   D2 : IR 受信 (OSRB38C9AA, Rx クラスが管理)
 *   D7 : LED 出力
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
// #include "LedCtrl.h"   // 演出 LED 制御

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
static uint8_t  g_bpm     = 120;

/** 再生中フラグ (PLAY で true / STOP で false) */
static bool     g_playing = false;

// ============================================================
//  LED制御用
// ============================================================

/** LEDを接続するピン */
constexpr uint8_t LED_PIN = 7;

/** SYNCを受けたときにLEDを点灯させる時間[ms] */
constexpr uint16_t LED_ON_MS = 80;

/** LEDが現在点灯しているか */
static bool g_ledOn = false;

/** LEDを消灯させる時刻[ms] */
static uint32_t g_ledOffMs = 0;

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

// LED制御用
static void flashLed();
static void updateLed();
static void stopLed();

// ============================================================
//  setup()
// ============================================================
void setup() {
    // ── 1. シリアル初期化（デバッグ用） ──────────────────────
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    // ── LED初期化 ────────────────────────────────────────
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.print(F("========================================\n"));
    Serial.print(F(" 赤外線同期輪唱システム --- Slave 起動\n"));
    Serial.print(F(" ID = 0x")); Serial.println(MY_SLAVE_ID, HEX);
    Serial.println(F("========================================"));

    // ── 2. IR 受信モジュール初期化 ────────────────────────────
    // D2 ピンを INPUT に設定し、FALLING エッジ割り込みを登録する。
    // これ以降、IR 信号が来るたびに onEdge() が自動で呼ばれる。
    Rx::getInstance().begin();

    // ── 3. 未実装モジュール初期化（実装後にコメントを外す） ───
    // player.begin();
    // ledCtrl.begin();

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

        // ── 2. パケットの解析 & XOR 整合性チェック ────────────
        // Packet::parse() は以下を行う:
        //   a) 24bit からフィールド（dest/cmd/data）を抽出
        //   b) XOR チェックバイトを再計算して受信値と照合
        //   c) 一致すれば true / ビット化けなら false を返す
        uint8_t dest, cmd, data;
        if (Packet::parse(rawPacket, dest, cmd, data)) {

            // ── 3. 宛先フィルタリング ──────────────────────────
            // 条件: dest == IR_DEST_ALL (ブロードキャスト)
            //    OR dest == MY_SLAVE_ID (自機宛の個別指定)
            // それ以外は他機宛なので無視する
            if (dest == IR_DEST_ALL || dest == MY_SLAVE_ID) {
                // ── 4. コマンドの振り分け ──────────────────────
                handlePacket(dest, cmd, data);
            }
            // else: 他機宛 → 何もしない（無視）

        } else {
            // XOR チェック不一致 → 通信エラー（ノイズ・ビット化け）
            // パケット全体を破棄して次の受信を待つ
            Serial.print(F("[ERR] XOR 不一致 raw=0x"));
            Serial.println(rawPacket, HEX);
        }
    }

    // ── 5. LED制御の更新 ───────────────────────────────────
    // delay() を使わず，LEDを消すタイミングだけを管理する
    updateLed();

    // ── 6. 未実装モジュールの更新（実装後にコメントを外す） ───
    // player.update();
    // ledCtrl.update();
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
    // 受信内容をシリアルモニタへ出力（デバッグ用）
    Serial.print(F("[RX] dest=0x")); Serial.print(dest, HEX);
    Serial.print(F(" cmd=0x"));     Serial.print(cmd, HEX);
    Serial.print(F(" data="));      Serial.println(data);

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

    Serial.println(F("[LED] SYNC同期点滅開始"));

    // TODO: player.play(g_bpm);
    // TODO: ledCtrl.startEffect();
}

/**
 * @brief STOP コマンドを受けたときの処理
 */
static void onStop() {
    if (!g_playing) return;  // 既に停止中なら何もしない

    g_playing = false;
    Serial.println(F("[STOP] 再生停止"));

    // LEDを停止して消灯する
    stopLed();

    // TODO: player.stop();
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
static void onBpm(uint8_t bpm) {
    // 異常な値が来たときの保険
    if (bpm < 40) {
        bpm = 40;
    }
    if (bpm > 240) {
        bpm = 240;
    }

    g_bpm = bpm;
    Serial.print(F("[BPM] ")); Serial.println(g_bpm);

    // 1拍の長さも確認用に表示する
    Serial.print(F("  1拍 = "));
    Serial.print(60000UL / g_bpm);
    Serial.println(F(" ms"));

    // TODO: player.setBpm(bpm);
    // TODO: scheduler.setBpm(bpm);  // 拍タイミング更新
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
    Serial.print(F("[SYNC] beat=")); Serial.println(beatCount);

    // 小節先頭（4拍ごと）の検出例
    if (beatCount % 4 == 0) {
        Serial.println(F("  => 小節先頭"));
    }

    // 再生中ならSYNCに合わせてLEDを光らせる
    if (g_playing) {
        flashLed();
    }

    // TODO: player.sync(beatCount);
    //       内部でドリフト量を計測し、必要なら再生速度を微調整する
}

// ============================================================
//  LED制御関数
// ============================================================

/**
 * @brief LEDを1回点灯させる
 *
 * @details
 *   SYNCコマンドを受信した瞬間に呼び出す。
 *   LED_ON_MSだけ点灯し，消灯はupdateLed()で行う。
 */
static void flashLed() {
    uint32_t now = millis();

    digitalWrite(LED_PIN, HIGH);
    g_ledOn = true;

    // LED_ON_MS後に消灯する
    g_ledOffMs = now + LED_ON_MS;
}

/**
 * @brief loop()内で常に呼び出すLED更新処理
 *
 * @details
 *   delay()を使わず，LEDの消灯タイミングだけを管理する。
 */
static void updateLed() {
    uint32_t now = millis();

    if (g_ledOn && (long)(now - g_ledOffMs) >= 0) {
        digitalWrite(LED_PIN, LOW);
        g_ledOn = false;
    }
}

/**
 * @brief STOP時にLEDを消灯する
 */
static void stopLed() {
    digitalWrite(LED_PIN, LOW);

    g_ledOn = false;
    g_ledOffMs = 0;

    Serial.println(F("[LED] 点滅停止"));
}
