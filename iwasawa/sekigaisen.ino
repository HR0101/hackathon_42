// 赤外線送信側プログラム
// Arduino UNO R4 WiFi
// 赤外線LED : OSI5LA5113A
// 出力ピン : D9

const int IR_LED = 9;

void setup() {

  pinMode(IR_LED, OUTPUT);

}

void loop() {

  // 38kHzキャリア信号を送信
  tone(IR_LED, 38000);

  // 送信時間
  delay(100);

  // 送信停止
  noTone(IR_LED);

  // 次の送信まで待機
  delay(1000);

}