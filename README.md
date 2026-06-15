# hackathon_42 — 赤外線一斉同期によるカエル合奏システム

1 台の **マスタ機** から赤外線で送る同期信号に合わせて，最大 5 台の **スレーブ機** が一斉に，あるいは時間差をつけて演奏する「輪唱（カノン）」合奏システムです．カエルの歌のように，同じ旋律を少しずつずらして重ねることで合奏を実現します．

---

## 1. システム概要

- **マスタ機（指揮者役）**: Arduino UNO R4 WiFi．赤外線で全スレーブへ「再生・停止・BPM・同期パルス」を送信します．
- **スレーブ機（演奏者役）**: 最大 5 台．赤外線を受信し，自機の番号に応じたタイミングで演奏と LED 演出を行います．
- **同期方式**: マスタが 1 拍ごとに送る **SYNC パルス** に全スレーブが追従するため，テンポがずれません．
- **輪唱**: スレーブ N 号機が `(N-1) × 4 拍` 遅れて演奏を始めることで，自動的に輪唱になります．

```
              赤外線 (38kHz, NEC準拠)
   ┌─────────┐  ))) PLAY / STOP / BPM / SYNC  ┌──────────┐
   │ マスタ機 │ ───────────────────────────▶ │ スレーブ1 │ ♪
   │ (指揮者) │ ───────────────────────────▶ │ スレーブ2 │  ♪（4拍遅れ）
   │   D9 TX │ ───────────────────────────▶ │ スレーブ3 │   ♪（8拍遅れ）
   └─────────┘                               │ …最大5台  │
        ▲                                    └──────────┘
        │ シリアル / Web Serial
   ┌─────────────────┐
   │ PC（操作端末）   │  master_controller.html もしくはシリアルモニタ
   └─────────────────┘
```

---

## 2. 主な特徴

- **赤外線ブロードキャスト同期**: 1 対多の一斉送信で，配線なしに複数台を同時制御します．
- **NEC 準拠 24bit プロトコル**: XOR チェックバイトによる誤り検出付きの自作パケットフォーマットです．
- **ドリフト補正**: SYNC 送信間隔を累積加算で管理し，処理時間の積み重なりによるテンポずれを防ぎます．
- **2 通りの操作方法**: シリアルモニタからのコマンド入力と，ブラウザ上の GUI（Web Serial API）の両方に対応します．
- **音響合成**: Processing + Minim ライブラリで，ピアノ・トランペット・木琴などの音色やキック・スネアを倍音合成で再現します．

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
│   ├── Shared/                IrDef.h / Packet.* の正本（共通定義）
│   ├── master_controller.html ブラウザ操作 UI（Web Serial）
│   └── CLAUDE.md              アーキテクチャ詳細ドキュメント
│
├── 055/                       Processing 音源（音色・ドラム・木琴など）
│   ├── Melody/                旋律 + キック + スネアの合成
│   ├── Melody_snare/ Merody_kick/
│   └── resound_piano/ resound_tranpet/ resound_mokkin_*/
│
├── ISHIMARU/                  Processing 音源（リズム・効果音）
│   ├── kick_rizumu3/          BPM 連動キックドラム
│   └── suneru/
│
├── iwasawa/                   赤外線送受信の初期試作（Arduino）
│   ├── mainLED.ino            38kHz キャリア送信の最小例
│   ├── sekigaisen.ino         受信して LED 点灯する最小例
│   └── zyusinpgm.ino
│
└── Yoda/
```

---

## 4. 技術仕様

### 4.1 パケットフォーマット（24bit, NEC 準拠, 38kHz 搬送波）

```
bit 23        16  15         8  7    4  3    0
┌─────────────┬─────────────┬────────┬────────┐
│  CHECK (8b) │  DATA  (8b) │CMD (4b)│DST (4b)│
└─────────────┴─────────────┴────────┴────────┘
upperByte = (dest & 0x0F) | ((cmd & 0x0F) << 4)
CHECK     = upperByte XOR DATA
```

`Packet::build()` がパケット生成と XOR チェックバイトの付与を，`Packet::parse()` が分解と整合性検査を担当します．

### 4.2 コマンド一覧（IrCmd）

| 値 | 名前 | DATA フィールド |
|------|------|-----------|
| 0x1 | PLAY | 未使用 |
| 0x2 | STOP | 未使用 |
| 0x3 | BPM  | BPM 値（40〜240） |
| 0x4 | SYNC | 拍カウンタ（0〜255，巡回） |

宛先 `IR_DEST_ALL = 0x0` は全スレーブへのブロードキャスト，`IR_DEST_SLAVE1`〜`IR_DEST_SLAVE5`（0x1〜0x5）は個別指定です．

### 4.3 ピン配置

| ピン | 役割 |
|-----|------|
| D9  | IR 送信（38kHz 搬送波出力，マスタ機） |
| D2  | IR 受信（FALLING エッジ割り込み，スレーブ機） |
| D4  | 演出用 LED |
| D7  | 同期点滅 LED（LED 対応版スレーブ） |

### 4.4 動作の仕組み

- **マスタ送信**: `Tx` クラスが RA4M1 の GPT タイマー（`FspTimer`）を 76kHz でトグルし，D9 に 38kHz 搬送波を生成します．`sendFrame()` はブロッキング処理で，最長フレームで約 68ms かかります．
- **スレーブ受信**: `Rx` はシングルトンで，D2 の FALLING エッジ割り込みからエッジ間隔を計測し，`IDLE → LEADER_DETECTED → RECEIVING → IDLE` の状態機械で NEC フレームをデコードします．
- **同期**: マスタは `60000 / BPM` ms ごとに SYNC を送信し，スレーブはそれに合わせて演奏位相と LED 点滅を補正します．

---

## 5. 使い方

### 5.1 Arduino（マスタ機・スレーブ機）

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

> `IrDef.h` と `Packet.h/.cpp` は `Shared/`・`Master/`・`Slave/` の 3 か所に存在します．Arduino IDE はスケッチフォルダ外を参照できないため，`Shared/` を正本として変更時に手動で同期してください．

シリアルモニタのボーレートは **115200** です．

### 5.2 シリアルコマンド（マスタ機）

シリアルモニタから以下のコマンドを送信します（大文字・小文字どちらも可）．

| コマンド | 動作 |
|----------|------|
| `play`        | 全スレーブへ BPM 送信後，PLAY を送信 |
| `stop`        | 全スレーブへ STOP を送信 |
| `bpm <値>`    | BPM を指定値（40〜240）に変更 |
| `bpm+` / `bpm-` | BPM を ±5 |
| `sync`        | SYNC を手動で 1 回送信 |
| `status`      | 現在の BPM・再生状態を表示 |
| `help` / `?`  | コマンド一覧を表示 |

### 5.3 Web コントローラ

`Yoda_Workspace/master_controller.html` を **Chrome** または **Edge（バージョン 89 以上）** で開くと，GUI からマスタ機を操作できます．「接続」ボタンでシリアルポート（115200 baud）に接続し，PLAY/STOP・BPM 調整・SYNC・ログ表示が行えます．

> Web Serial API はセキュアコンテキストでのみ動作します．`file://` で動かない場合はローカルサーバー経由で開いてください．

### 5.4 Processing 音源

`Sound.pde` や `055/`・`ISHIMARU/` 内の `.pde` は **Processing** で動作し，**Minim**（`ddf.minim.*`）または **Sound**（`processing.sound.*`）ライブラリを使用します．Processing IDE でスケッチを開き，実行後にキー入力で再生・音色切替を行います（例: `Sound.pde` は `1`/`2`/`3` で音色，`p` で再生）．

---

## 6. ハードウェア

- マイコン: **Arduino UNO R4 WiFi**（Renesas RA4M1 / R7FA4M1AB3CFP）
- 赤外線 LED: OSI5LA5113A など（送信，D9）
- 赤外線受信モジュール: OSRB38C9AA など（受信，D2）
- 演出用 LED（D4 / D7）

---

## 7. 開発フロー

機能ごとにブランチを切り，Pull Request 経由で `main` にマージします．ブランチ名は担当者 ID（例: `055`）に従います．

```bash
# 作業開始時
git checkout main && git pull origin main && git checkout <自分のブランチ> && git merge main
```

コミットメッセージの接頭辞は `feat:` / `fix:` / `docs:` / `style:` / `refactor:` / `test:` / `chore:` を使用します．詳しいコマンドや運用ルールは [`GitHubRule.md`](./GitHubRule.md) を参照してください．

---

## 8. ドキュメント

- [`GitHubRule.md`](./GitHubRule.md) — Git / GitHub の基本コマンドとチーム開発ルール
- [`Yoda_Workspace/CLAUDE.md`](./Yoda_Workspace/CLAUDE.md) — 赤外線同期システムのアーキテクチャ詳細
