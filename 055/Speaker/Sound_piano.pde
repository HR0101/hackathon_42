// 【変更対象ファイル：Sound_piano.pde を書き換え】
import ddf.minim.*;
import ddf.minim.analysis.*;
import ddf.minim.effects.*;
import ddf.minim.signals.*;
import ddf.minim.spi.*;
import ddf.minim.ugens.*;
import processing.serial.*; // シリアルライブラリ

Minim minim;
AudioOutput out;
Waveform currentWaveform;
Serial myPort;              

// 楽譜データ（melody, duration, startTime は元のまま維持）
String [] melody = { "C4","D4","E4","F4","E4","D4","C4", "E4","F4","G4","A4","G4","F4","E4", "C4","C4","C4","C4", "C4","C4","D4","D4","E4","E4","F4","F4", "E4","D4","C4" };
float [] duration = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f , 0.5f, 0.5f, 0.5f ,0.5f, 0.5f, 0.5f, 0.5f , 0.5f, 0.5f, 0.5f ,0.5f, 0.5f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.5f,0.5f,0.5f };
float [] startTime = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,  8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 16.0f,18.0f,20.0f,22.0f, 24.0f,24.5f,25.0f,25.5f,26.0f,26.5f,27.0f,27.5f, 28.0f, 29.0f, 30.0f };

class HackInstrument implements Instrument {
  Oscil wave; ADSR adsr; float frequencyHz;
  HackInstrument ( float frequency , float maxAmp , Waveform wf ) {
    frequencyHz = frequency;
    wave = new Oscil ( frequency , 1.0f, wf );
    adsr = new ADSR( maxAmp, 0.005f, 0.50f, 0.08f, 0.15f ); // 5ms, 500ms, 8%, 150ms
    wave.patch( adsr );
  }
  void noteOn ( float duration ) {
    adsr.noteOn(); adsr.patch( out );
    if (myPort != null) myPort.println("N," + frequencyHz); // ② スレーブに音を鳴らすよう即座に送信
  }
  void noteOff () {
    adsr.noteOff(); adsr.unpatchAfterRelease( out );
    if (myPort != null) myPort.println("R");               // ② スレーブに音を止めるよう即座に送信
  }
}

void setup () {
  size (512 , 200); 
  try {
    myPort = new Serial(this, Serial.list()[0], 115200); // スレーブ機が繋がっているCOMポートを開く
    println("スレーブ機との接続を確立しました。赤外線トリガーを待っています...");
  } catch (Exception e) {
    println("ポートオープン失敗: " + e.getMessage());
  }
  minim = new Minim ( this );
  out = minim.getLineOut (); 
  out.setTempo ( 120 ); 
  currentWaveform = WavetableGenerator.gen10 ( 4096, new float[] { 1.00f, 0.60f, 0.35f, 0.20f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f } );
  
  // ★ ここでは playSong() を呼ばずに、シリアル通信からの受信を待ちます
}

void draw () {
  background (0); stroke (255);
  for(int i = 0; i < out.bufferSize () - 1; i++) {
   line ( i, 50 + out.left.get(i)*50 , i+1, 50 + out.left.get(i+1)*50 );
  }

  // ★ バックグラウンドでスレーブ機からの「START」合図を監視し続ける
  if (myPort != null && myPort.available() > 0) {
    String val = myPort.readStringUntil('\n');
    if (val != null) {
      val = val.trim();
      if (val.equals("START")) {
        println("赤外線受信の合図を検知！ 楽曲再生を開始します。");
        playSong(); // 合図が来たら自動スタート
      }
    }
  }
}

void playSong () { 
  out.pauseNotes (); 
  for (int i = 0; i < melody.length ; i++) { 
    out.playNote ( startTime [i], duration [i], new HackInstrument ( Frequency.ofPitch ( melody [i] ).asHz (), 0.5f, currentWaveform ));
  }
  out.resumeNotes ();
}
