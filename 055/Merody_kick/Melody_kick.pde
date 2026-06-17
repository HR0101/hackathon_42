import ddf.minim.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;

String[] melody = new String[96];
float[] duration = new float[96];
float[] startTime = new float[96];

class KickInstrument implements Instrument {

  Oscil kick1;
  Oscil kick2;
  Summer sum;

  KickInstrument(float frequency) {

    // 低音だけ
    kick1 = new Oscil(
      frequency,
      0.9f,
      Waves.SINE
    );

    // 少し倍音
    kick2 = new Oscil(
      frequency * 2,
      0.3f,
      Waves.SINE
    );

    sum = new Summer();

    kick1.patch(sum);
    kick2.patch(sum);
  }

  void noteOn(float dur) {

    sum.patch(out);
  }

  void noteOff() {

    sum.unpatch(out);
  }
}

void setup() {

  size(512,200);

  minim = new Minim(this);
  out = minim.getLineOut();

  out.setTempo(120);

  // 元のメロディ
  for(int i=0; i<96; i++) {

    melody[i] = "C2";
    duration[i] = 0.2f;
    startTime[i] = i * 0.5f;
  }

  background(0);

  fill(255);
  textSize(20);

  text("Press K",180,100);
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

void playSong() {

  for(int i=0; i<melody.length; i++) {

    float frequency =
      Frequency.ofPitch(melody[i]).asHz();

    out.playNote(
      startTime[i],
      duration[i],
      new KickInstrument(frequency)
    );
  }
}

void keyPressed() {

  if(key == 'p') {

    out.pauseNotes();

    playSong();

    out.resumeNotes();
  }
}
