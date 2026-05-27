import processing.sound.*;

WhiteNoise noise;

SinOsc snareTone;

Env env;

int bpm=100;
float beatTime;

int beat=0;
int lastTime=0;


void setup()
{
  size(500,300);

  noise=
  new WhiteNoise(this);

  snareTone=
  new SinOsc(this);

  env=
  new Env(this);

  calculateBeatTime();
}


void draw()
{
  background(220);

  updateMelody();

  textSize(25);

  text(
  "Snare BPM:"+bpm,
  100,
  100
  );
}


// =====================

void calculateBeatTime()
{
  beatTime=
  (60000.0/bpm)/2;
}


// =====================

void updateMelody()
{
  if(
  millis()-lastTime
  >=beatTime
  )
  {
    playSong();

    beat++;

    if(beat>=8)
    {
      noLoop();
    }

    lastTime=
    millis();
  }
}


// =====================

void playSong()
{
  // 2拍目と4拍目
  if(
  beat==0||
  beat==1||
  beat==2||
  beat==3||
  beat==4
  )
  {
    playSnare();
  }
}


// =====================

void playSnare()
{
  // 前の音停止
  noise.stop();
  snareTone.stop();

  // ノイズ部分
  noise.amp(0.5);

  // スネア胴部分
  snareTone.freq(180);
  snareTone.amp(0.25);

  noise.play();
  snareTone.play();

  // ADSR
  env.play(
  noise,
  0.001,
  0.03,
  0.2,
  0.05
  );

  env.play(
  snareTone,
  0.001,
  0.05,
  0.3,
  0.05
  );
}
