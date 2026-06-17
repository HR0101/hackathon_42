import processing.serial.*;

Serial myPort;

int bpm = 120;      // 初期BPM
float beatTime;


void setup()
{
  size(400,200);

  // Arduino接続
  myPort=
  new Serial(
    this,
    Serial.list()[0],
    9600
  );

  calculateBeatTime();
}


void draw()
{
  background(220);

  updateBPMByIR();

  textSize(30);

  text(
  "BPM : "+bpm,
  120,
  100
  );

  text(
  "Beat : "+
  nf(beatTime,0,1)+
  " ms",
  100,
  150
  );
}


// =====================
// ArduinoからBPM更新
// =====================

void updateBPMByIR()
{
  while(
  myPort.available()>0
  )
  {
    String data=
    myPort.readStringUntil('\n');

    if(data!=null)
    {
      bpm=
      int(
      trim(data)
      );

      bpm=
      getSmoothBPM(
      bpm
      );

      calculateBeatTime();
    }
  }
}


// =====================
// BPM範囲制限
// =====================

int getSmoothBPM(
int value
)
{
  return constrain(
  value,
  60,
  180
  );
}


// =====================
// BPM→拍時間変換
// =====================

void calculateBeatTime()
{
  beatTime=
  60000.0/bpm;
}
