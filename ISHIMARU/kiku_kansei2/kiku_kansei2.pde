import ddf.minim.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;

boolean[] pattern = new boolean[48];

int currentStep = 0;

float bpm = 120;

long lastStep = 0;
float stepInterval;

void setup() {
  size(400,200);

  minim = new Minim(this);
  out = minim.getLineOut();
  stepInterval = (60000.0 / bpm) / 12.0;

// 4つ打ち
pattern[6]  = true;
pattern[18] = true;
pattern[30] = true;
pattern[42] = true;

}

void draw() {

  background(30);

  fill(255);
  text("48 Step Kick Sequencer",20,30);

  if(millis() - lastStep >= stepInterval){

    lastStep = millis();

    if(pattern[currentStep]){
      playKick();
    }

    currentStep++;

    if(currentStep >= 48){
      currentStep = 0;
    }
  }

  drawPattern();

}
void drawPattern(){

  for(int i=0;i<48;i++){

    if(i == currentStep){
      fill(255,0,0);
    }
    else if(pattern[i]){
      fill(0,255,0);
    }
    else{
      fill(80);
    }

    rect(10 + i*8, 100, 6, 30);
  }
}

void keyPressed(){
  if(key==' '){
    playKick();
  }
}

void playKick(){
  Kick kick = new Kick();
  out.addSignal(kick);
}
class Kick implements AudioSignal{

  float phase=0;
  float time=0;

  float sampleRate=44100.0;

  public void generate(float[] left,float[] right){

    for(int i=0;i<left.length;i++){

      float t=time/sampleRate;

      //-----------------------
      // 周波数変化
      //-----------------------

      float freq;

if (t < 0.01) {

  // 300Hz → 150Hz
  freq = 150 + 150 * exp(-350 * t);

} else if (t < 0.08) {

  // 150Hz → 60Hz
  freq = 60 + 90 * exp(-45 * (t - 0.01));

} else {

  // 60Hz → 40Hz
  freq = 40 + 20 * exp(-8 * (t - 0.08));





      }

      phase+=TWO_PI*freq/sampleRate;

      //-----------------------
      // 音量
      //-----------------------

      float amp;

if (t < 0.003) {

  amp = t / 0.003;

} else {

  amp = exp(-12 * (t - 0.003));

}

      //-----------------------
      // アタック
      //-----------------------

float click = 0;

if (t < 0.004) {

  float env = 1.0 - t / 0.004;

  // ノイズ
  click += random(-1,1) * 0.25 * env;

  // 7kHzクリック
  click += sin(TWO_PI * 7000 * t) * 0.20 * env;



      }

      float sample=sin(phase)*amp+click;

      left[i]=sample;
      right[i]=sample;

      time++;

      if(t>0.30){

        left[i]=0;
        right[i]=0;

      }

    }

  }

  public void generate(float[] signal){
    float[] dummy=new float[signal.length];
    generate(signal,dummy);
  }

}
