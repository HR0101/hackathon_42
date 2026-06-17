bool startSent = false;
bool stopSent = false;

void setup()
{
  Serial.begin(9600);
}

void loop()
{
  unsigned long t = millis();

  // 起動5秒後
  if (!startSent && t >= 5000)
  {
    Serial.println("START");
    Serial.println(120);   // BPMを120に設定

    startSent = true;
  }
if (t >= 10000 && t < 10100)
{
  Serial.println(140);
}



  // 起動20秒後（開始から15秒後）
  if (!stopSent && t >= 20000)
  {
    Serial.println("STOP");

    stopSent = true;
  }
}
