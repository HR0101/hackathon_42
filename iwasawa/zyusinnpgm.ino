const int IR_RECEIVE = 2;
const int LED_PIN = 7;

void setup() {

  pinMode(IR_RECEIVE, INPUT);
  pinMode(LED_PIN, OUTPUT);

  Serial.begin(9600);

  Serial.println("赤外線受信待機中");

}

void loop() {

  if (digitalRead(IR_RECEIVE) == LOW) {

    unsigned long receiveTime = millis();

    Serial.print("受信: ");
    Serial.print(receiveTime);
    Serial.println(" ms");

    Serial.println("演奏開始命令を受信");

    // LED点灯
    digitalWrite(LED_PIN, HIGH);

    delay(1000);

    // LED消灯
    digitalWrite(LED_PIN, LOW);

  }

}