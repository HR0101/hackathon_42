# 赤外線一斉同期によるカエル合奏システム

Arduino UNO R4 WiFi を複数台使い、赤外線通信で同期しながら「かえるのうた」を輪唱するシステムです。  
1台の **Master** が指揮者として各 **Slave**（最大5台）へコマンドを送信し、各 Slave は内蔵 DAC でオンデバイス音声合成を行います。

---

## システム構成

```
Master（1台）
  ├── D9: IR LED（38 kHz 搬送波、送信のみ）
  └── D4: 演出 LED

Slave（最大5台）
  ├── D2: IR 受信モジュール OSRB38C9AA（受信のみ）
  ├── D4: 演出 LED（SYNC に同期して点滅）
  └── A0: 12-bit DAC → TA7368 アンプ → 8Ω スピーカー
```

| 機体 | 役割 | 楽器 |
|------|------|------|
| 1号機 | 旋律（輪唱 1番） | ピアノ |
| 2号機 | 旋律（輪唱 2番） | トランペット |
| 3号機 | 旋律（輪唱 3番） | 木琴（まろやか） |
| 4号機 | リズム | キックドラム |
| 5号機 | リズム | スネアドラム |

---

## ディレクトリ構成

```
Yoda_Workspace/
├── Master/        Master スケッチ（IR 送信、全体指揮）
├── Slave/         Slave スケッチ（IR 受信、音声合成、演奏）
│   └── Song.h     楽曲・音色・機体割り当て（ここを編集して曲を変える）
└── Shared/        IrDef.h / Packet.h / Packet.cpp の正規ソース
```

> **注意:** Arduino IDE はスケッチフォルダ外のファイルを参照できないため、  
> `IrDef.h` / `Packet.h` / `Packet.cpp` は `Shared/`・`Master/`・`Slave/` の3か所に存在します。  
> `Shared/` が正規ソースで、変更時は3か所を同期してください。

---

## ビルドとアップロード

### 必要環境
- Arduino IDE 2.x または Arduino CLI
- ボード: **Arduino UNO R4 WiFi** (`arduino:renesas_uno:unor4wifi`)
- ライブラリ: `FspTimer`（RA4M1 標準付属）

### Arduino CLI

```bash
# Master
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Master/Master.ino
arduino-cli upload -p <PORT> --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Master/Master.ino

# Slave（各機体で MY_SLAVE_ID を変えてアップロード）
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Slave/Slave.ino
arduino-cli upload -p <PORT> --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Slave/Slave.ino
```

### Slave のアップロード前に必須

`Slave/Slave.ino` の `MY_SLAVE_ID` を機体番号に合わせて書き換えてください。

```cpp
constexpr uint8_t MY_SLAVE_ID = IR_DEST_SLAVE1;  // 1号機は SLAVE1、5号機は SLAVE5
```

シリアルモニタのボーレート: **115200 bps**

---

## 操作方法

Master をシリアルモニタに接続し、以下のコマンドを送信します。

| コマンド | 動作 |
|----------|------|
| `play` | BPM を送信してから各機へ時間差で PLAY を送信（輪唱開始） |
| `stop` | 全機へ STOP を送信 |
| `bpm <値>` | BPM を指定値（10〜600）に変更して全機へ送信 |
| `bpm+` | BPM を 5 増加 |
| `bpm-` | BPM を 5 減少 |
| `sync` | SYNC を今すぐ手動送信 |
| `status` | 現在の BPM・再生状態を表示 |
| `help` | コマンド一覧を表示 |

初期 BPM: **120**

---

## 通信プロトコル

### パケット形式（24-bit、NEC 互換、38 kHz 搬送波）

旧 XOR 検査バイトを **拡張ハミング(24,18) SEC-DED** に置き換えています。

```
bit 23  22    18  17 16  15         8  7    4  3    0
┌──────┬────────┬──────┬────────────┬────────┬────────┐
│ PTY  │PARITY  │SEQ   │  DATA (8b) │CMD (4b)│DST (4b)│
│ (1b) │ (5b)   │ (2b) │            │        │        │
└──────┴────────┴──────┴────────────┴────────┴────────┘

情報ビット [bit17:0] = DST + CMD + DATA + SEQ（18bit）
パリティ   [bit23:18] = SEC 5bit + 全体パリティ 1bit
```

### コマンド一覧

| 値 | コマンド | data フィールド |
|----|----------|----------------|
| 0x1 | PLAY | 未使用 |
| 0x2 | STOP | 未使用 |
| 0x3 | BPM | `(bpm - 10) / 5`（0〜118） |
| 0x4 | SYNC | 拍カウンタ（0〜255、ラップ） |

`IR_DEST_ALL = 0x0` で全機ブロードキャスト、`IR_DEST_SLAVE1`〜`IR_DEST_SLAVE5` で個別指定。

---

## 通信の信頼性対策

### 対策1：ビット誤り訂正（内符号）

`Packet::parse()` が拡張ハミング SEC-DED でシンドローム復号します。

| 状況 | 判定 | 処理 |
|------|------|------|
| シンドローム=0、パリティ一致 | `OK` | そのまま受理 |
| 全体パリティ不一致（奇数誤り） | `CORRECTED` | 誤りビットを特定・反転して受理 |
| シンドローム≠0、パリティ一致（偶数誤り） | `UNCORRECTABLE` | 廃棄 |

### 対策2：反復送信（外符号）

PLAY / STOP / BPM は同一フレームを **3回連送**（フレーム間 12ms）します。  
SYNC は毎拍の自己回復型なので1回のみ送信します。

### 対策3：重複除去

各フレームに 2-bit の SEQ（論理コマンド連番）を埋め込みます。  
Slave 側の `isDuplicate()` が `(cmd, data, seq)` を前回実行と比較し、反復フレームを無視します。

### 対策4：SYNC パケットロス検出

`onSync()` が `beatCount` の連続性を監視し、欠落をシリアルログに記録します（`[LOSS]` タグ）。

### 対策5：位相補正（絶対拍アライメント）

`Player::sync()` が Master の `beatCount` を絶対基準として誤差を計算し、3段階で補正します。

```
PLAY 後の最初の SYNC → アンカー確立
  _anchorMaster = beatCount
  _anchorSlave  = round(slave 内部拍)

以降の SYNC ごとに:
  期待拍  = _anchorSlave + (uint8_t)(beatCount - _anchorMaster)
  誤差 ms = (実際の拍 − 期待拍) × beatMs

  |誤差| > 1/2拍分ms → hard 補正（SYNC 欠落による多拍ズレを即スナップ）
  |誤差| > 20ms      → damp 補正（通常ドリフトを 0.5 倍で緩やかに戻す）
  それ以下            → 許容範囲内、補正なし
```

---

## 音声合成エンジン（Slave）

Slave は外部音源を使わず、オンデバイスでリアルタイム音声合成します。

### 倍音合成 + ノイズ

起動時に楽器ごとの倍音テーブルから 1 周期分のウェーブテーブル（256サンプル）を生成し、  
位相アキュムレータで任意の高さの音を発振します。打楽器は xorshift ホワイトノイズを混合します。

### ADSR 包絡線

`FspTimer` の割り込み（16 kHz）内で 1 サンプルずつ計算します。ドラムは sustain=0 のワンショットです。

### 出力経路

```
A0（12-bit DAC） → 結合コンデンサ → TA7368 アンプ → 8Ω スピーカー
```

---

## 輪唱のしくみ

### Master 側

`ROUND_START_BEAT[]` で各機の入り拍を定義します（`Master.ino` で編集）。

```cpp
// 1号機: 拍0、2号機: 拍8（2小節後）、3号機: 拍16（4小節後）、4・5号機: 拍0（リズムは最初から）
constexpr uint16_t ROUND_START_BEAT[5] = { 0, 8, 16, 0, 0 };
```

`play` コマンドを受けると、Master は `g_beat` が各機の開始拍に達するたびに個別に PLAY を送信します。

### Slave 側

PLAY を受け取った瞬間に `SONG[]` の先頭から演奏を開始します（自己遅延なし）。  
`SONG_LEN_BEATS = 32` 拍（8小節）で 1 ループし、巡回します。

### 楽曲データの編集

`Slave/Song.h` だけを編集すれば曲・音色を変更できます。

| 配列 | 内容 |
|------|------|
| `SONG[]` | 旋律（音名・開始拍・音符長） |
| `INSTRUMENTS[]` | 楽器ごとの倍音構成・ノイズ比・ADSR |
| `KICK_PATTERN[]` / `SNARE_PATTERN[]` | ドラムが叩く拍の列 |
| `VOICES[]` | 機体番号 → 役割・楽器の対応表 |

> 輪唱の入りタイミングは `Song.h` ではなく `Master.ino` の `ROUND_START_BEAT[]` で管理します。

---

## ピン配置

| ピン | 役割 | 対象 |
|------|------|------|
| D9 | IR 送信（38 kHz 搬送波） | Master |
| D2 | IR 受信（FALLING 割り込み） | Slave |
| D4 | 演出 LED（SYNC に同期点滅） | Master / Slave |
| A0 | 音声出力（12-bit DAC） | Slave |

---

## Git ワークフロー

各機能は独立したブランチで開発し、`main` へ Pull Request でマージします。  
ブランチ名は担当者 ID に従います（例: `055`, `Workspace_yoda`）。

### セッション開始手順

```bash
git checkout main && git pull origin main
git checkout <branch> && git merge main
```

### コミットプレフィックス

| プレフィックス | 用途 |
|---|---|
| `feat:` | 新機能 |
| `fix:` | バグ修正 |
| `docs:` | ドキュメント |
| `refactor:` | リファクタリング |
| `chore:` | ビルド・設定変更 |
