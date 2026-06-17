import ddf.minim.*;
import ddf.minim.ugens.*;
import processing.serial.*;

//======================
// Audio
//======================
Minim minim;
AudioOutput out;

// キック音
Oscil kickFund;
Noise click;
ADSR kickEnv;

//======================
// Serial
//======================
Serial myPort;

//======================
// BPM
//======================
int bpm = 100;
float beatTime;

boolean playing = false;

int beat = 0;
int lastTime = 0;

//======================
// setup
//======================
void setup()
{
  size(800, 400);

  minim = new Minim(this);
  out = minim.getLineOut();

  out.setGain(10);

  // キック基本音
  kickFund =
    new Oscil(
      60,
      1.5f,
      Waves.SINE
    );

  // アタックノイズ

  // エンベロープ
  kickEnv =
    new ADSR(
      1.0f,
      0.1f,
      0.1f,
      0.1f,
      0.2f
    );

  kickFund.patch(kickEnv);
  kickEnv.patch(out);

  //======================
  // Arduino接続
  //======================

  println(Serial.list());

  myPort =
    new Serial(
      this,
      Serial.list()[3],   // ←必要なら番号を変更
      9600
    );

  calculateBeatTime();
}

//======================
// draw
//======================
void draw()
{
  background(0);

  receiveSerial();

  if (playing)
  {
    updateRhythm();
  }

  drawWave();

  fill(255);
  textSize(20);

  text("BPM : " + bpm, 20, 30);

  if (playing)
  {
    text("PLAYING", 20, 60);
  }
  else
  {
    text("STOP", 20, 60);
  }
}

//======================
// Arduino受信
//======================
void receiveSerial()
{
  while (myPort.available() > 0)
  {
    String data =
      myPort.readStringUntil('\n');

    if (data == null)
      return;

    data = trim(data);

    println("受信 : " + data);

    if (data.equals("START"))
    {
      playing = true;
      beat = 0;
      lastTime = millis();
    }
    else if (data.equals("STOP"))
    {
      playing = false;
    }
    else
    {
      bpm = int(data);
      calculateBeatTime();
    }
  }
}

//======================
// BPM計算
//======================
void calculateBeatTime()
{
  beatTime =
    (60000.0 / bpm) / 2;
}

//======================
// 波形表示
//======================
void drawWave()
{
  stroke(0, 255, 0);

  int maxDraw =
    min(width - 1,
        out.bufferSize() - 1);

  for (int i = 0; i < maxDraw; i++)
  {
    line(
      i,
      height/2 + out.left.get(i)*150,
      i+1,
      height/2 + out.left.get(i+1)*150
    );
  }
}
//======================
// リズム更新
//======================
void updateRhythm()
{
  if (millis() - lastTime >= beatTime)
  {
    playSong();

    beat++;

    if (beat >= 15)
    {
      beat = 0;
    }

    lastTime = millis();
  }
}

//======================
// リズムパターン
//======================
void playSong()
{
  switch(beat)
  {
  case 0:
    playKick();
    break;

  case 5:
    playKick();
    break;

  case 10:
    playKick();
    break;

  case 15:
    playKick();
    break;
  }
}

//======================
// キック再生
//======================
void playKick()
{
  thread("kickThread");
}

//======================
// キック音
//======================
void kickThread()
{
  // 初期周波数
  kickFund.setFrequency(180);

  // エンベロープ開始
  kickEnv.noteOn();

  // アタックノイズ


  // ピッチ降下
  for (int i = 0; i < 50; i++)
  {
    float f =
      lerp(
      180,
      120,
      i / 19.0f
      );

    kickFund.setFrequency(f);

    delay(2);
  }

  delay(400);

  kickEnv.noteOff();

  // 次の音のために戻す
  kickFund.setFrequency(60);
}
