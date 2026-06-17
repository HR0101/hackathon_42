import ddf.minim.*;
import ddf.minim.analysis.*;
import ddf.minim.effects.*;
import ddf.minim.signals.*;
import ddf.minim.spi.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;
Waveform currentWaveform; 

// 汎用楽器（HackInstrument）
class HackInstrument implements Instrument {
  Oscil wave;
  Line ampEnv;
  float maxAmp;
  
  HackInstrument ( float frequency , float maxAmp , Waveform wf ) { 
    wave = new Oscil ( frequency , 0, wf );
    this.maxAmp = maxAmp;
    ampEnv = new Line ();
    ampEnv.patch( wave.amplitude );
  }

  void noteOn ( float duration ) { 
    ampEnv.activate ( duration , this.maxAmp , 0);
    wave.patch ( out );
  }
  
  void noteOff () { 
    wave.unpatch ( out );
  }
}

// キックドラム
class KickInstrument implements Instrument {
  Oscil kick1;
  Oscil kick2;
  Noise attack;
  Line ampEnv;
  Summer sum;

  KickInstrument(float frequency) {
    kick1 = new Oscil(frequency, 0.8f, Waves.SINE);
    kick2 = new Oscil(frequency*2, 0.4f, Waves.SINE);
    attack = new Noise(0.2f);
    ampEnv = new Line();
    sum = new Summer();

    kick1.patch(sum);
    kick2.patch(sum);
    attack.patch(sum);
  }

  void noteOn(float dur) {
    ampEnv.activate(dur, 1.0f, 0.0f);
    sum.patch(out);
  }

  void noteOff() {
    sum.unpatch(out);
  }
}

// スネアドラム
class SnareInstrument implements Instrument {
  Noise whiteNoise;
  Line ampEnv;

  SnareInstrument() {
    whiteNoise = new Noise(0.4f, Noise.Tint.WHITE);
    ampEnv = new Line();
    ampEnv.patch(whiteNoise.amplitude);
  }

  void noteOn(float dur) {
    ampEnv.activate(dur, 1.0f, 0.0f);
    whiteNoise.patch(out);
  }

  void noteOff() {
    whiteNoise.unpatch(out);
  }
}

//メインの処理

String[] melody = new String[96];
float[] duration = new float[96];
float[] startTime = new float[96];

void setup() {
  size(512,200);

// 1曲64拍(輪唱曲のため1.5倍{96})
  for(int i=0; i<96; i++) {
    melody[i] = "C4";
    duration[i] = 0.2f;
    startTime[i] = i * 0.5f;
  }

  minim = new Minim(this);
  out = minim.getLineOut();
  out.setTempo(120);

  currentWaveform = Waves.SINE;
}

void playSong() {
  for(int i=0; i<melody.length; i++) {
    float frequency = Frequency.ofPitch(melody[i]).asHz();
    playKick(frequency, startTime[i]);
  }
}

void playKick(float freq, float time) {
  out.playNote(time, 0.15f, new KickInstrument(freq));
}

void playDrum() {
  for (int beat = 0; beat < 96; beat++) {
    out.playNote(
      (float)beat * 0.5f, 
      0.2f,
      new HackInstrument(60, 0.8f, currentWaveform)
    );
  }
}

// スネアを 3, 7, 11, 15... 拍目に鳴らす関数
// 1曲32拍(輪唱曲のため1.5倍{48})
void playSnare() {
  for (int beat = 0; beat < 48; beat++) {
    if (beat % 4 == 1 || beat % 4 == 3) {
      out.playNote(
        (float)beat, 
        0.15f,
        new SnareInstrument() 
      );
    }
  }
}

void draw () {
  background (0);
  stroke (255);

  for(int i = 0; i < out.bufferSize() - 1; i++) {
    line ( i, 50 + out.left.get(i)*50 , i+1, 50 + out.left.get(i+1)*50 );
    line ( i, 150 + out.right.get(i)*50 , i+1, 150 + out.right.get(i+1)*50 );
  }
}

void keyPressed () {
  switch (key) {  
    case 'p': // 'p' キーでキックドラムを再生
      currentWaveform = WavetableGenerator.gen10 (
        4096 ,
        new float[] { 1.00f, 0.60f, 0.35f, 0.20f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f }
      );
      out.pauseNotes();
      playDrum();
      out.resumeNotes();
      break;
      
    case 's': // 's' キーでスネアドラムを再生
      out.pauseNotes();
      playSnare();
      out.resumeNotes();
      break;
        
    default : break ;
  }
}
