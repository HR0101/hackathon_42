
// スピーカー出力
// ③ Processing側で演奏が始まり、シリアル経由でリアルタイム音符データが届いたとき
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("N,")) {
      float freq = input.substring(2).toFloat();
      player.noteOn(freq); // スピーカーモジュール（A0等）へ音を出力（アタック開始）
    } 
    else if (input == "R") {
      player.noteOff();    // 音の余韻フェーズへ（リリース開始）
    }
  }