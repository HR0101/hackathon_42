import ddf.minim.*;
import ddf.minim.analysis.*;
import ddf.minim.effects.*;
import ddf.minim.signals.*;
import ddf.minim.spi.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;
Waveform currentWaveform;

// 各音の音階 
String [] melody = {
"C4","D4","E4","F4","E4","D4","C4",
"E4","F4","G4","A4","G4","F4","E4",
"C4","C4","C4","C4",
"C4","C4","D4","D4","E4","E4","F4","F4",
"E4","D4","C4"
};

// 各音の長さ（拍） 
float [] duration = {
0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f , 0.5f,
0.5f, 0.5f ,0.5f, 0.5f, 0.5f, 0.5f , 0.5f,
0.5f, 0.5f ,0.5f, 0.5f,
0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
0.5f,0.5f,0.5f,
};

// 各音の開始位置 
float [] startTime = {
0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 
8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f,
16.0f,18.0f,20.0f,22.0f,
24.0f,24.5f,25.0f,25.5f,26.0f,26.5f,27.0f,27.5f,
28.0f, 29.0f, 30.0f
};

// Instrument インタフェースの実装 
class HackInstrument implements Instrument
{
  Oscil wave;
  ADSR adsr;

  HackInstrument ( float frequency , float maxAmp , Waveform wf ) 
  {
    wave = new Oscil ( frequency , 1.0f, wf ); 
    // ADSRのパラメータ設定 
    float attackTime   = 0.001f; // アタック時間 (立ち上がり)
    float decayTime    = 0.08f;  // ディケイ時間 (最大音量からサスティンへの減衰)
    float sustainLevel = 0.0f;  // サスティンレベル (保持される音量の割合 0.0〜1.0)
    float releaseTime  = 0.06f;  // リリース時間 (ノートオフ後の余韻)
   
    // ADSRインスタンスの作成 (最大音量, アタック, ディケイ, サスティン, リリース)
    adsr = new ADSR( maxAmp, attackTime, decayTime, sustainLevel, releaseTime );
    // オシレーターの出力をADSRに接続する
    wave.patch( adsr );
  }

  void noteOn ( float duration ) 
  {
    adsr.noteOn();
    adsr.patch( out );
  }
  
  void noteOff ()
  {
    adsr.noteOff();
    adsr.unpatchAfterRelease( out );
  }
}


void setup () 
{
  size (512 , 200); 
  minim = new Minim ( this ); 
  out = minim.getLineOut (); 
  out.setTempo ( 120 ); 
  
  // 初期値として木琴の倍音構造を設定 
  currentWaveform = WavetableGenerator.gen10 ( 
     4096, 
     new float[] { 
  1.00f,
  0.00f,
  0.70f,
  0.00f,
  0.50f,
  0.00f,
  0.30f,
  0.00f,
  0.15f
       } 
  );
}

void playSong () 
{ 
  out.pauseNotes (); 
  for (int i = 0; i < melody.length ; i++) { 
    out.playNote ( startTime [i], duration [i], 
    new HackInstrument ( Frequency.ofPitch ( melody [i] ).asHz (), 0.5f, currentWaveform )); 
  }
  out.resumeNotes ();
}

void draw () 
{
  background (0);
  stroke (255); 
  for(int i = 0; i < out.bufferSize () - 1; i++) 
  {
   line ( i, 50 + out.left.get(i)*50 , i+1, 50 + out.left.get(i+1)*50 ); 
   line ( i, 150 + out.right.get(i)*50 , i+1, 150 + out.right.get(i+1)*50 ); 
  }
}

void keyPressed () 
{ 
  if (key == 'p') { 
    playSong (); 
  }
}
