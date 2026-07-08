import ddf.minim.*;
import java.io.File;
import javax.sound.sampled.*;
import java.io.ByteArrayInputStream;

Minim minim;
AudioOutput out;
ExactKick kick;

int bpm = 120;
int totalBeats = 48;
int intervalBeats = 2;
float msPerBeat;
int lastCheckTime = 0;
float beatAccumulator = 0.0f;
int currentBeat = 0;

void setup() {
  size(500, 350);
  minim = new Minim(this);
  out = minim.getLineOut(Minim.STEREO, 1024);
  kick = new ExactKick();
  msPerBeat = (60.0f / bpm) * 1000.0f;
  lastCheckTime = millis();
}


void draw() {
  background(20);
  int now = millis();
  beatAccumulator += (float)(now - lastCheckTime) / msPerBeat;
  lastCheckTime = now;
  
  if (beatAccumulator >= 1.0f) {
    beatAccumulator -= 1.0f;
    
    // --- ここを変更 ---
    // 拍数そのものではなく、カウンターを回して判定する
    if (currentBeat % intervalBeats == 0) {
      kick.trigger();
    }
    
    currentBeat++;
    if (currentBeat >= totalBeats) {
      currentBeat = 0;
    }
  }
  
  // (以下、描画処理...)
}

// ------------------------------------
// クラスは draw() の外側に置く！
// ------------------------------------
class ExactKick implements AudioSignal {
  float[] waveData;
  int currentSample = 0;
  boolean isPlaying = false;
  float sampleRate = 44100.0f;

  final float[][] PROFILE_DATA = {
    {17, 0.0021362305f}, {70, 0.33547974f}, {116, 0.9727783f}, {209, 0.99819946f},
    {331, 0.99935913f}, {462, 0.99838257f}, {616, 0.99783325f}, {805, 0.9970703f},
    {1081, 0.997406f}, {1344, 0.9919739f}, {1634, 0.9964905f}, {2046, 0.9909973f},
    {2330, 0.90722656f}, {2647, 0.90734863f}, {3003, 0.92541504f}, {3388, 0.9143982f},
    {3795, 0.9213562f}, {4223, 0.85528564f}, {4661, 0.74295044f}, {5107, 0.47338867f},
    {5554, 0.28225708f}, {6001, 0.12005615f}, {6444, 0.054534912f}, {7175, 0.021514893f}
  };

  ExactKick() {
    int totalSamples = (int)PROFILE_DATA[PROFILE_DATA.length - 1][0];
    waveData = new float[totalSamples];
    int prev = 0;
    for (int i = 0; i < PROFILE_DATA.length; i++) {
      int target = (int)PROFILE_DATA[i][0];
      float amp = PROFILE_DATA[i][1];
      int len = target - prev;
      for (int s = 0; s < len; s++) {
        float val = (float)Math.sin(((float)s/len) * Math.PI) * amp;
        waveData[prev + s] = (i % 2 == 0) ? val : -val;
      }
      prev = target;
    }
  }

  void trigger() {
    currentSample = 0;
    if (!isPlaying) { out.addSignal(this); isPlaying = true; }
  }

  public void generate(float[] left, float[] right) {
    for (int i = 0; i < left.length; i++) {
      if (currentSample < waveData.length) {
        left[i] = right[i] = waveData[currentSample++]*2.0;//ここはもともと*1だった
      } else {
        left[i] = right[i] = 0;
        if (isPlaying) { out.removeSignal(this); isPlaying = false; }
      }
    }
  }
  public void generate(float[] s) { generate(s, s); }
}

