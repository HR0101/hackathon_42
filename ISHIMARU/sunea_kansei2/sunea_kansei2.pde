import ddf.minim.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;
boolean[] snare = new boolean[48];
int currentStep = 0;

float bpm = 120;

float stepTime;

long lastStep;

void setup() {
  size(400, 200);

  minim = new Minim(this);
  out = minim.getLineOut();
  stepTime = (60000.0 / bpm) / 12.0;

lastStep = millis();
  // スネア
snare[0]  = true;
snare[12]  = true;
snare[24] = true;
snare[36] = true;

}

void draw() {

  background(30);

  if (millis() - lastStep >= stepTime) {

    lastStep = millis();

    if (snare[currentStep]) {
      playSnare();
    }

    currentStep++;

    if (currentStep >= 48)
      currentStep = 0;
  }

  fill(255);

  text("Step : " + currentStep,20,30);
}


void playSnare() {
  Snare snare = new Snare();
  out.addSignal(snare);
}

void stop() {
  out.close();
  minim.stop();
  super.stop();
}
class Snare implements AudioSignal {

  float phase1 = 0;
  float phase2 = 0;
  int sample = 0;

  float sampleRate = 44100.0;

  public void generate(float[] left, float[] right) {

    for (int i = 0; i < left.length; i++) {

      float t = sample / sampleRate;

      //-----------------------
      // 音量エンベロープ
      //-----------------------

      float amp;

      if (t < 0.002) {
        amp = t / 0.002;
      } else {
        amp = exp(-11 * (t - 0.002));
      }

      //-----------------------
      // パンチ
      //-----------------------

      float freq;

      if (t < 0.03) {
        freq = 180 + 180 * exp(-90 * t);
      } else {
        freq = 180;
      }

      phase1 += TWO_PI * freq / sampleRate;

      //-----------------------
      // 胴鳴り
      //-----------------------

      phase2 += TWO_PI * 900 / sampleRate;

      float body =
        0.50 * sin(phase1) +
        0.20 * sin(phase2);

      //-----------------------
      // スナッピー
      //-----------------------

      float noise = 0;

      if (t < 0.40) {
        noise = random(-1, 1);
        noise *= exp(-8 * t);
      }

      //-----------------------
      // アタック
      //-----------------------

      float click = 0;

      if (t < 0.005) {

        float env = 1.0 - t / 0.005;

        click += random(-1, 1) * 0.35 * env;

        click +=
          sin(TWO_PI * 7000 * t)
          * 0.25
          * env;
      }

      //-----------------------
      // 合成
      //-----------------------

      float sampleValue =
        body * amp +
        noise * 0.35 +
        click;

      //-----------------------
      // ソフトクリッピング
      //-----------------------

sampleValue = (float)Math.tanh(sampleValue * 2.0);

      left[i] = sampleValue;
      right[i] = sampleValue;

      sample++;

      if (t > 0.4) {

        left[i] = 0;
        right[i] = 0;
      }
    }
  }

  public void generate(float[] signal) {

    float[] right = new float[signal.length];

    generate(signal, right);
  }
}
