import ddf.minim.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;

class SnareInstrument implements Instrument {

  Noise whiteNoise;
  Line ampEnv;

  SnareInstrument() {

    whiteNoise =
      new Noise(0.4f, Noise.Tint.WHITE);

    ampEnv = new Line();

    ampEnv.patch(whiteNoise.amplitude);
  }

  void noteOn(float dur) {

    ampEnv.activate(
      dur,
      1.0f,
      0.0f
    );

    whiteNoise.patch(out);
  }

  void noteOff() {

    whiteNoise.unpatch(out);
  }
}

void setup() {

  size(512,200);

  minim = new Minim(this);
  out = minim.getLineOut();

  out.setTempo(120);

  background(0);

  fill(255);
  textSize(20);

  text("Press S",180,100);
}

void draw() {

  background(0);

  stroke(255);

  for(int i = 0; i < out.bufferSize()-1; i++) {

    line(
      i,
      50 + out.left.get(i)*50,
      i+1,
      50 + out.left.get(i+1)*50
    );

    line(
      i,
      150 + out.right.get(i)*50,
      i+1,
      150 + out.right.get(i+1)*50
    );
  }
}

void playSnare() {

  // 元コードのスネア配置
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

void keyPressed() {

  if(key == 'p') {

    out.pauseNotes();

    playSnare();

    out.resumeNotes();
  }
}
