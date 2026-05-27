import processing.sound.*;

SinOsc kick1;
SinOsc kick2;
WhiteNoise attack;
Env env;

int bpm=100;
float beatTime;

int beat=0;
int lastTime=0;


void setup()
{
  size(500,300);

  kick1=new SinOsc(this);
  kick2=new SinOsc(this);

  attack=new WhiteNoise(this);

  env=new Env(this);

  calculateBeatTime();
}


void draw()
{
  background(220);

  updateBPMByIR();

  updateMelody();

  textSize(25);

  text(
  "Kick BPM:"+bpm,
  120,
  100
  );
}


// =====================
// BPM更新
// =====================

void updateBPMByIR()
{
  /*
  Arduino受信時

  bpm=
  int(trim(data));
  */

  bpm=
  getSmoothBPM(bpm);

  calculateBeatTime();
}


// =====================

int getSmoothBPM(int value)
{
  return constrain(
  value,
  60,
  180
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
  if(
  beat==0||
  beat==2||
  beat==4||
  beat==6
  )
  {
    playKick();
  }
}


// =====================

void playKick()
{
  kick1.freq(55);
  kick1.amp(1.5);

  kick2.freq(110);
  kick2.amp(1.5);

  attack.amp(1.5);

  kick1.stop();
  kick2.stop();
  attack.stop();

  env.play(
  kick1,
  0.001,
  0.10,
  0.8,
  0.10
  );
}
