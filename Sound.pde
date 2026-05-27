import ddf.minim.*;
import ddf.minim.analysis.*;
import ddf.minim.effects.*;
import ddf.minim.signals.*;
import ddf.minim.spi.*;
import ddf.minim.ugens.*;


Minim minim ;
AudioOutput out;
Waveform currentWaveform ; // 音色格納用変数

// 各音の音階
String [] melody = {
"C4","D4","E4","F4","E4","D4","C4",
"E4","F4","G4","A4","G4","F4","E4",
"C4","C4","C4","C4",
"C4","C4","D4","D4","E4","E4","F4","F4",
"E4","D4","C4"
};

// 各音の長さ（拍）[検証用]
float [] duration = {
0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f , 0.5f,
0.5f, 0.5f ,0.5f, 0.5f, 0.5f, 0.5f , 0.5f,
0.5f, 0.5f ,0.5f, 0.5f,
0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
0.5f,0.5f,0.5f,
};

// 各音の開始位置[検証用]
float [] startTime = {
0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 
8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f,
16.0f,18.0f,20.0f,22.0f,
24.0f,24.5f,25.0f,25.5f,26.0f,26.5f,27.0f,27.5f,
28.0f, 29.0f, 30.0f
};
  
// 音色を変更するためにInstrument インタフェースを実装する
class HackInstrument implements Instrument
{
 Oscil wave ;
 Line ampEnv ;
 float maxAmp ;
 
 HackInstrument (float frequency , float maxAmp , Waveform wf )
 { // Oscilを使って音信号を作成（周波数, 振幅, 音色）
   wave = new Oscil ( frequency , 0, wf );
   // 引数で渡された最大振幅をクラスの変数に代入
   this.maxAmp = maxAmp ;
   // 振幅変調を与える（初期値は1から0への減衰）
   ampEnv = new Line ( );
   // 作成した音信号を振幅変調の出力に送る
   ampEnv.patch ( wave.amplitude );
 }

 // コールバック関数：再生開始
 void noteOn ( float duration )
 { // 振幅変調の開始（長さ，開始時の振幅，終了時の振幅）
   ampEnv.activate ( duration , this.maxAmp , 0);
   // 音の再生
   wave . patch ( out );
 }
 
 //コールバック関数：再生停止
 void noteOff ()
 { //再生の停止
   wave.unpatch ( out );
 }
}


void setup ()
{
  size (512 , 200);
  minim = new Minim ( this );
  // minimのインスタンスを用意
  minim = new Minim ( this );
  // minimのgetLineOutメソッドを呼び出し，AudioOutputオブジェクトを受け取る
  out = minim . getLineOut ();
  // テンポの設定，BPM=120　[検証用]
  out. setTempo ( 120 );
  // 音色の初期値（正弦波）
  currentWaveform = Waves . SINE ;
}


void playSong () {
  // 再生を停止
  out. pauseNotes ();
  // 繰り返し処理を使って異なる音を追加
  for (int i = 0; i < melody . length ; i++) {
    out. playNote ( startTime [i], duration [i],
    new HackInstrument ( Frequency . ofPitch ( melody [i] ). asHz (),
    0.5f, currentWaveform ));
  }
  // 再生
  out. resumeNotes ();
}


void draw ()
{
  background (0);
  stroke (255);

  // 左チャンネルと右チャンネルに入っている波形を描画
  for(int i = 0; i < out. bufferSize () - 1; i++)
  {
   line ( i, 50 + out. left .get(i)*50 , i+1, 50 + out. left .get(i +1)*50 );
   line ( i, 150 + out. right .get(i)*50 , i+1, 150 + out. right .get(i +1)*50 );
  }
}




//applyADSR() {}



void keyPressed () {
  switch (key)
 { 
  case '1':
    // ピアノの倍音構造
    currentWaveform = WavetableGenerator . gen10 (
     4096 , // サンプルサイズ
     new float[] {   
       1.00f,  // 基音
       0.60f,  // 2倍音
       0.35f,  // 3倍音
       0.20f,  // 4倍音
       0.12f,  // 5倍音
       0.08f,  // 6倍音
       0.05f,  // 7倍音
       0.03f,  // 8倍音
       0.02f,  // 9倍音
       0.01f   // 10倍音 
        } 
     );
    break ;
    
    case'2':
    // トランペットの倍音構造
     currentWaveform = WavetableGenerator.gen10 (
     4096 , 
     new float[] {   
        1.00f,
        1.00f,
        0.85f,
        0.85f,
        0.75f,
        0.75f,
        0.65f,
        0.55f,
        0.45f,
        0.30f
        } 
     );
    break ;
       
    case'3':
    // 木琴の倍音構造
     currentWaveform = WavetableGenerator.gen10 (
     4096, 
     new float[] {   
      1.00f,  
      0.00f, 
      0.50f, 
        } 
     );
    break ;
    
    
   
  case 'p':
    // 出力
    playSong ();
    break ;
  default : break ;
 }
}
