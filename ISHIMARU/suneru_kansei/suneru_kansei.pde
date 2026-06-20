import processing.sound.*;
import processing.serial.*;

WhiteNoise noise;
SinOsc snareTone;
Env env;

Serial myPort;

//=====================
// BPM
//=====================

int bpm = 100;
float beatTime;

int beat = 0;
int lastTime = 0;

boolean playing = false;

//=====================
// setup
//=====================

void setup()
{
  size(500,300);

  noise = new WhiteNoise(this);
  snareTone = new SinOsc(this);
  env = new Env(this);

  calculateBeatTime();

  // シリアルポート確認
  println(Serial.list());

  // 必要に応じて番号を変更
  myPort =
    new Serial(
      this,
      Serial.list()[3],
      9600
    );
}

//=====================
// draw
//=====================

void draw()
{
  background(220);

  receiveSerial();

  if(playing)
  {
    updateMelody();
  }

  fill(0);

  textSize(25);

  text("Snare BPM : " + bpm,100,100);

  if(playing)
  {
    text("PLAY",170,150);
  }
  else
  {
    text("STOP",170,150);
  }
}

//=====================
// シリアル受信
//=====================

void receiveSerial()
{
  while(myPort.available()>0)
  {
    String data =
      myPort.readStringUntil('\n');

    if(data==null)
      return;

    data = trim(data);

    println(data);

    if(data.equals("START"))
    {
      playing = true;
      beat = 0;
      lastTime = millis();
    }
    else if(data.equals("STOP"))
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

//=====================
// BPM計算
//=====================

void calculateBeatTime()
{
  beatTime =
    (60000.0 / bpm) / 2;
}

//=====================
// リズム更新
//=====================

void updateMelody()
{
  if(millis() - lastTime >= beatTime)
  {
    playSong();

    beat++;

    if(beat >= 20)
    {
      beat = 0;
    }

    lastTime += beatTime;
  }
}

//=====================
// パターン
//=====================

void playSong()
{
  if(
    beat == 0 ||
    beat == 5 ||
    beat == 10 ||
    beat == 15
    )
  {
    playSnare();
  }
}

//=====================
// スネア
//=====================

void playSnare()
{
  noise.stop();

  noise.amp(0.5);

  noise.play();

  env.play(
    noise,
    0.1,
    0.3,
    0.2,
    0.05
  );
}
