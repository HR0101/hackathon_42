# hackathon_42 — 赤外線一斉同期によるカエル合奏システム

1 台の **マスタ機** から赤外線で送る同期信号に合わせて，最大 5 台の **スレーブ機** が一斉に，あるいは時間差をつけて演奏する「輪唱（カノン）」合奏システムです．カエルの歌のように，同じ旋律を少しずつずらして重ねることで合奏を実現します．

---

## 1. システム概要

- **マスタ機（指揮者役）**: Arduino UNO R4 WiFi．赤外線で全スレーブへ「再生・停止・BPM・同期パルス」を送信します．
- **スレーブ機（演奏者役）**: 最大 5 台．赤外線を受信し，自機の番号に応じたタイミングで演奏と LED 演出を行います．
- **同期方式**: マスタが 1 拍ごとに送る **SYNC パルス** に全スレーブが追従するため，テンポがずれません．
- **輪唱**: マスタが各スレーブへ時間差で PLAY を送ることで，自動的に輪唱になります．

```
              赤外線 (38kHz, NEC準拠)
   ┌─────────┐  ))) PLAY / STOP / BPM / SYNC  ┌──────────┐
   │ マスタ機 │ ───────────────────────────▶ │ スレーブ1 │ ♪
   │ (指揮者) │ ───────────────────────────▶ │ スレーブ2 │  ♪（8拍遅れ）
   │   D9 TX │ ───────────────────────────▶ │ スレーブ3 │   ♪（16拍遅れ）
   └─────────┘                               │ …最大5台  │
        ▲                                    └──────────┘
        │ シリアル / Web Serial
   ┌─────────────────┐
   │ PC（操作端末）   │  master_controller.html もしくはシリアルモニタ
   └─────────────────┘
```

### 機体構成

| 機体 | 役割 | 楽器 |
|------|------|------|
| 1号機 | 旋律（輪唱 1番） | ピアノ |
| 2号機 | 旋律（輪唱 2番） | トランペット |
| 3号機 | 旋律（輪唱 3番） | 木琴（まろやか） |
| 4号機 | リズム | キックドラム |
| 5号機 | リズム | スネアドラム |

---

## 2. 主な特徴

- **赤外線ブロードキャスト同期**: 1 対多の一斉送信で，配線なしに複数台を同時制御します．
- **拡張ハミング(24,18) SEC-DED**: 1bit 誤り自動訂正 / 2bit 誤り検出付きの自作パケットフォーマットです．
- **反復送信**: 重要コマンド（PLAY/STOP/BPM）は同一フレームを 3 回連送してフレーム欠落を補います．
- **絶対拍アライメント**: SYNC パルスを使った位相補正で，パケットロスによる多拍ズレも自動回復します．
- **オンデバイス音声合成**: スレーブは外部 PC 不要．12-bit DAC + 倍音合成 + ADSR で楽器音を生成します．
- **2 通りの操作方法**: シリアルモニタからのコマンド入力と，ブラウザ上の GUI（Web Serial API）の両方に対応します．

---

## 3. ディレクトリ構成

```
hackathon_42/
├── README.md                  本ファイル
├── GitHubRule.md              チーム開発用 Git/GitHub ガイド
├── Sound.pde                  Processing 音源（メロディ + 音色切替の基本形）
│
├── Yoda_Workspace/            ★ メインの赤外線同期システム（Arduino）
│   ├── Master/                マスタ機スケッチ（IR 送信）
│   ├── Slave/                 スレーブ機スケッチ（IR 受信・演奏）
│   │   └── Song.h             楽曲・音色・機体割り当て（ここを編集して曲を変える）
│   ├── Shared/                IrDef.h / Packet.* の正本（共通定義）
│   ├── master_controller.html ブラウザ操作 UI（Web Serial）
│   └── CLAUDE.md              アーキテクチャ詳細ドキュメント
│
├── 055/                       Processing 音源（音色・ドラム・木琴など）
├── ISHIMARU/                  Processing 音源（リズム・効果音）
├── iwasawa/                   赤外線送受信の初期試作（Arduino）
└── Yoda/
```

> `IrDef.h` と `Packet.h/.cpp` は `Shared/`・`Master/`・`Slave/` の 3 か所に存在します．Arduino IDE はスケッチフォルダ外を参照できないため，`Shared/` を正本として変更時に手動で同期してください．

---

## 4. 技術仕様

### 4.1 パケットフォーマット（24bit, NEC 準拠, 38kHz 搬送波）

旧 XOR 検査バイトを **拡張ハミング(24,18) SEC-DED** に置き換えています．

```
bit 23  22    18  17 16  15         8  7    4  3    0
┌──────┬────────┬──────┬────────────┬────────┬────────┐
│ PTY  │PARITY  │SEQ   │  DATA (8b) │CMD (4b)│DST (4b)│
│ (1b) │ (5b)   │ (2b) │            │        │        │
└──────┴────────┴──────┴────────────┴────────┴────────┘

情報ビット [bit17:0] = DST + CMD + DATA + SEQ（18bit）
パリティ   [bit23:18] = SEC 5bit + 全体パリティ 1bit
```

`Packet::build()` が SEC-DED 符号化，`Packet::parse()` がシンドローム復号と誤り訂正を担当します．

### 4.2 コマンド一覧（IrCmd）

| 値 | 名前 | DATA フィールド |
|------|------|-----------|
| 0x1 | PLAY | 未使用 |
| 0x2 | STOP | 未使用 |
| 0x3 | BPM  | `(bpm - 10) / 5`（0〜118） |
| 0x4 | SYNC | 拍カウンタ（0〜255，巡回） |

宛先 `IR_DEST_ALL = 0x0` は全スレーブへのブロードキャスト，`IR_DEST_SLAVE1`〜`IR_DEST_SLAVE5`（0x1〜0x5）は個別指定です．

### 4.3 通信の信頼性対策

| 対策 | 実装場所 | 内容 |
|------|----------|------|
| **ビット誤り訂正（SEC-DED）** | `Packet.cpp` | 1bit 自動訂正 / 2bit 検出廃棄 |
| **反復送信** | `Master.ino` | PLAY/STOP/BPM を 3 回連送（間隔 12ms） |
| **重複除去** | `Slave.ino` | SEQ(2bit) で反復フレームを識別・無視 |
| **パケットロス検出** | `Slave.ino` | beatCount の連続性を監視してログ出力 |
| **位相補正** | `Player.cpp` | 絶対拍アライメント（hard/damp 2 段階） |

**位相補正の詳細（`Player::sync()`）:**

```
PLAY 後の最初の SYNC → アンカー確立
  _anchorMaster = beatCount  ← Master の絶対拍
  _anchorSlave  = round(slave 内部拍)

以降の SYNC ごとに:
  期待拍  = _anchorSlave + (uint8_t)(beatCount - _anchorMaster)
  誤差 ms = (実際の拍 − 期待拍) × beatMs

  |誤差| > 1/2拍分ms → hard 補正（SYNC 欠落による多拍ズレを即スナップ）
  |誤差| > 20ms      → damp 補正（通常ドリフトを 0.5 倍で緩やかに戻す）
  それ以下            → 許容範囲内，補正なし
```

### 4.4 ピン配置

| ピン | 役割 | 対象 |
|-----|------|------|
| D9  | IR 送信（38kHz 搬送波出力） | マスタ機 |
| D2  | IR 受信（FALLING エッジ割り込み） | スレーブ機 |
| D4  | 演出 LED（SYNC に同期して点滅） | Master / Slave |
| A0  | 音声出力（12-bit DAC → TA7368 アンプ → スピーカー） | スレーブ機 |

### 4.5 動作の仕組み

- **マスタ送信**: `Tx` クラスが RA4M1 の GPT タイマー（`FspTimer`）を 76kHz でトグルし，D9 に 38kHz 搬送波を生成します．`sendFrame()` はブロッキング処理で，最長フレームで約 68ms かかります．
- **スレーブ受信**: `Rx` はシングルトンで，D2 の FALLING エッジ割り込みからエッジ間隔を計測し，`IDLE → LEADER_DETECTED → RECEIVING → IDLE` の状態機械で NEC フレームをデコードします．
- **音声合成**: `Player` が `FspTimer` の 16kHz 割り込み内で倍音合成 + ADSR を 1 サンプルずつ計算し，A0 の 12-bit DAC へ出力します．

---

## 5. 使い方

### 5.1 ビルドとアップロード

Arduino IDE または Arduino CLI を使用します．対象ボードは **Arduino UNO R4 WiFi** です．

```bash
# コンパイル（Arduino CLI）
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Master/Master.ino
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Slave/Slave.ino

# 書き込み
arduino-cli upload -p <PORT> --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Master/Master.ino
arduino-cli upload -p <PORT> --fqbn arduino:renesas_uno:unor4wifi Yoda_Workspace/Slave/Slave.ino
```

> **スレーブ書き込み前の注意**: `Slave/Slave.ino` の `MY_SLAVE_ID` を機体番号（`IR_DEST_SLAVE1`〜`IR_DEST_SLAVE5`）に書き換えてください．5 台それぞれに異なる値を設定します．

シリアルモニタのボーレートは **115200** です．

### 5.2 シリアルコマンド（マスタ機）

シリアルモニタから以下のコマンドを送信します（大文字・小文字どちらも可）．

| コマンド | 動作 |
|----------|------|
| `play`        | 全スレーブへ BPM 送信後，PLAY を時間差送信（輪唱開始） |
| `stop`        | 全スレーブへ STOP を送信 |
| `bpm <値>`    | BPM を指定値（10〜600）に変更して全スレーブへ送信 |
| `bpm+` / `bpm-` | BPM を ±5 |
| `sync`        | SYNC を手動で 1 回送信 |
| `status`      | 現在の BPM・再生状態を表示 |
| `help` / `?`  | コマンド一覧を表示 |

### 5.3 Web コントローラ

`Yoda_Workspace/master_controller.html` を **Chrome** または **Edge（バージョン 89 以上）** で開くと，GUI からマスタ機を操作できます．「接続」ボタンでシリアルポート（115200 baud）に接続し，PLAY/STOP・BPM 調整・SYNC・ログ表示が行えます．

> Web Serial API はセキュアコンテキストでのみ動作します．`file://` で動かない場合はローカルサーバー経由で開いてください．

### 5.4 楽曲・音色の変更

`Slave/Song.h` だけを編集すれば曲・音色を変更できます．

| 配列 | 内容 |
|------|------|
| `SONG[]` | 旋律（音名・開始拍・音符長）．現在は「かえるのうた」32拍1ループ |
| `INSTRUMENTS[]` | 楽器ごとの倍音構成・ノイズ比・ADSR |
| `KICK_PATTERN[]` / `SNARE_PATTERN[]` | ドラムが叩く拍の列 |
| `VOICES[]` | 機体番号 → 役割・楽器の対応表 |

> 輪唱の入りタイミングは `Song.h` ではなく `Master.ino` の `ROUND_START_BEAT[]` で管理します．

### 5.5 Processing 音源

`Sound.pde` や `055/`・`ISHIMARU/` 内の `.pde` は **Processing** で動作し，**Minim**（`ddf.minim.*`）または **Sound**（`processing.sound.*`）ライブラリを使用します．Processing IDE でスケッチを開き，実行後にキー入力で再生・音色切替を行います（例: `Sound.pde` は `1`/`2`/`3` で音色，`p` で再生）．

---

## 6. ハードウェア

- マイコン: **Arduino UNO R4 WiFi**（Renesas RA4M1 / R7FA4M1AB3CFP）
- 赤外線 LED（送信，D9）
- 赤外線受信モジュール: OSRB38C9AA（受信，D2）
- アンプ: TA7368（音声出力，A0）
- 演出 LED（D4）

---

## 7. 開発フロー

機能ごとにブランチを切り，Pull Request 経由で `main` にマージします．ブランチ名は担当者 ID（例: `055`, `Workspace_yoda`）に従います．

```bash
# 作業開始時
git checkout main && git pull origin main && git checkout <自分のブランチ> && git merge main
```

コミットメッセージの接頭辞は `feat:` / `fix:` / `docs:` / `style:` / `refactor:` / `test:` / `chore:` を使用します．詳しいコマンドや運用ルールは [`GitHubRule.md`](./GitHubRule.md) を参照してください．

---

## 8. ドキュメント

- [`GitHubRule.md`](./GitHubRule.md) — Git / GitHub の基本コマンドとチーム開発ルール
- [`Yoda_Workspace/CLAUDE.md`](./Yoda_Workspace/CLAUDE.md) — 赤外線同期システムのアーキテクチャ詳細
