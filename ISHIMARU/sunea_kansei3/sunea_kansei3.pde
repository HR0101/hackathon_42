import ddf.minim.*;

Minim minim;
AudioOutput out;
SnareSynth snare;

void setup() {
  size(400, 200);
  minim = new Minim(this);
  // ステレオ出力用のラインを設定
  out = minim.getLineOut(Minim.STEREO, 1024);
  snare = new SnareSynth();
  out.addSignal(snare);
}

void draw() {
  background(0);
  fill(255);
  text("スネアドラム再生中...", 20, 100);
}

class SnareSynth implements AudioSignal {
  int samplesPerBeat = 22050; 
  int pos = 0;
  float phase = 0;

  // 1. ステレオ用のメソッド（今のまま）
  void generate(float[] left, float[] right) {
    for (int i = 0; i < left.length; i++) {
      float sample = calculateSample(pos++);
      left[i] = right[i] = sample;
    }
  }

  // 2. ★追加が必要なモノラル用のメソッド（これがないとエラーになります）
  void generate(float[] s) {
    for (int i = 0; i < s.length; i++) {
      s[i] = calculateSample(pos++);
    }
  }

  float calculateSample(int p) {
    // リズム制御
    int beatIndex = p / samplesPerBeat;
    if (beatIndex < 1 || beatIndex % 2 == 0) {
      if (p % samplesPerBeat == 0) phase = 0; 
      return 0;
    }

    float localP = (float)(p % samplesPerBeat);
    float t = localP / 22050.0;

    // 音の成分
    float stick = random(-1, 1) * exp(-t * 100.0);
    float freq = 200.0 * exp(-t * 20.0) + 100.0;
    phase += (freq * TWO_PI) / 44100.0;
    float body = sin(phase) * exp(-t * 5.0);
    float noise = random(-1, 1) * exp(-t * 10.0);
    
    float env = exp(-t * 15.0);
    return (stick * 0.3 + body * 0.4 + noise * 0.3) * env;
  }
}
