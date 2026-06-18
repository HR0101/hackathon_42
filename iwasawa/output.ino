// ============================================================
//  追加・変更箇所まとめ：遅延測定・音出力
// ============================================================

// ① グローバル変数に追加
// static LedCtrl ledCtrl; の下あたりに追加

constexpr uint8_t SPEAKER_PIN = 8;   // ブザーまたはスピーカー接続ピン

static uint32_t g_packetReadyUs = 0; // パケット受信完了時刻[us]
static uint32_t g_parseDoneUs   = 0; // パケット解析完了時刻[us]

static uint32_t g_delayCount = 0;    // 測定回数

static float g_sumParseDelayUs  = 0;
static float g_sumOutputDelayUs = 0;
static float g_sumTotalDelayUs  = 0;


// ② 前方宣言に追加
// static void onSync(uint8_t beatCount); の下に追加

static void printDelayLog(uint32_t soundStartUs);


// ③ setup() に追加
// ledCtrl.begin(); の下に追加

pinMode(SPEAKER_PIN, OUTPUT);
noTone(SPEAKER_PIN);


// ④ loop() の decode() 直後に追加
// if (Rx::getInstance().decode(rawPacket)) { の直後

g_packetReadyUs = micros();


// ⑤ Packet::parse() 成功直後に追加
// if (Packet::parse(rawPacket, dest, cmd, data)) { の直後

g_parseDoneUs = micros();


// ⑥ onPlay() をこの内容に変更
// 元の if (g_playing) return; は削除する

static void onPlay() {
    bool wasPlaying = g_playing;

    g_playing = true;

    if (wasPlaying) {
        Serial.println(F("[PLAY] 再生中にPLAYを再受信"));
    } else {
        Serial.print(F("[PLAY] 再生開始 BPM="));
        Serial.println(g_bpm);
    }

    uint32_t soundStartUs = micros();

    tone(SPEAKER_PIN, 523);

    printDelayLog(soundStartUs);

    // TODO: player.play(g_bpm);
    // TODO: ledCtrl.startEffect();
}


// ⑦ onStop() に追加
// g_playing = false; の後あたり

noTone(SPEAKER_PIN);


// ⑧ ファイルの一番下に追加

static void printDelayLog(uint32_t soundStartUs) {
    uint32_t parseDelayUs  = g_parseDoneUs - g_packetReadyUs;
    uint32_t outputDelayUs = soundStartUs - g_parseDoneUs;
    uint32_t totalDelayUs  = soundStartUs - g_packetReadyUs;

    g_delayCount++;

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
