# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

赤外線一斉同期によるカエル合奏システム — an infrared-synchronized frog ensemble system.

One **Master** Arduino UNO R4 WiFi transmits IR signals to up to 5 **Slave** units, which play music in a round (輪唱) synchronized via beat pulses.

## Build and Upload

This is an Arduino project. Use the **Arduino IDE** or **Arduino CLI**:

```bash
# Compile (Arduino CLI)
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Master/Master.ino
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Slave/Slave.ino

# Upload
arduino-cli upload -p <PORT> --fqbn arduino:renesas_uno:unor4wifi Master/Master.ino
arduino-cli upload -p <PORT> --fqbn arduino:renesas_uno:unor4wifi Slave/Slave.ino
```

**Before uploading Slave:** edit `MY_SLAVE_ID` in `Slave/Slave.ino` to match the unit number (`IR_DEST_SLAVE1` through `IR_DEST_SLAVE5`).

Serial monitor baud rate: **115200**.

## Architecture

### Directory Structure

```
Yoda_Workspace/
├── Master/     — Master sketch (IR transmitter, controls all slaves)
├── Slave/      — Slave sketch (IR receiver, plays music)
│   └── Song.h  — all song / timbre / per-unit round config (edit this)
└── Shared/     — Canonical source for IrDef.h and Packet.h/cpp
```

`IrDef.h` and `Packet.h/cpp` exist in **all three directories**. The Arduino IDE cannot reference files outside the sketch folder, so `Shared/` holds the source of truth and the copies in `Master/` and `Slave/` must be kept in sync manually when these files change.

### Packet Format (24-bit, NEC-compatible, 38 kHz carrier)

```
bit 23        16  15         8  7    4  3    0
┌─────────────┬─────────────┬────────┬────────┐
│  CHECK (8b) │  DATA  (8b) │CMD (4b)│DST (4b)│
└─────────────┴─────────────┴────────┴────────┘
upperByte = (dest & 0x0F) | ((cmd & 0x0F) << 4)
CHECK     = upperByte XOR DATA
```

`Packet::build()` / `Packet::parse()` handle construction and XOR validation.

### Commands (IrCmd enum)

| Value | Name | data field |
|-------|------|-----------|
| 0x1 | PLAY | unused |
| 0x2 | STOP | unused |
| 0x3 | BPM  | BPM value (40–240) |
| 0x4 | SYNC | beat counter (0–255, wraps) |

`IR_DEST_ALL = 0x0` broadcasts to all slaves. Individual slaves use `IR_DEST_SLAVE1`–`IR_DEST_SLAVE5`.

### Master flow

`Tx` class uses RA4M1's GPT timer (via `FspTimer`) toggled at 76 kHz to produce a 38 kHz carrier on **D9**. `sendFrame()` is blocking (~68 ms worst case). `updateSyncTiming()` in `loop()` accumulates `g_lastSyncMs += beatMs` (not `= millis()`) to prevent drift.

Serial commands: `play`, `stop`, `bpm <val>`, `bpm+`, `bpm-`, `sync`, `status`, `help`.

### Slave flow

`Rx` is a singleton (required by `attachInterrupt`). It registers a FALLING-edge ISR on **D2** that measures intervals between edges to decode NEC frames. State machine: `IDLE → LEADER_DETECTED → RECEIVING → IDLE`. `decode()` atomically reads the completed 24-bit packet using `noInterrupts()`/`interrupts()`.

### Slave audio engine (`Player` + `Song.h`)

The Slave plays music entirely on-device (no PC/external source):

- **Additive synthesis**: at startup `Player` builds a one-cycle wavetable from each instrument's harmonic array, then drives a phase accumulator to sound any pitch (no waveform-loop discontinuity).
- **ADSR**: computed per-sample inside an `FspTimer` sampling ISR at `AUDIO_SAMPLE_RATE` (16 kHz).
- **Output**: A0 (12-bit DAC) → coupling cap → TA7368 amp → 8 Ω speaker.
- **Round (輪唱)**: after `play()`, each unit waits its per-voice offset, plays the melody, and loops every `SONG_LEN_BEATS` (32). `sync()` corrects phase drift (subtracting estimated IR latency to avoid false correction).

**All song/timbre/round data lives in `Slave/Song.h`** — the single file to edit:
1. `SONG[]` — melody (note, start beat, duration).
2. `INSTRUMENTS[]` — per-instrument harmonics + ADSR (Piano / Trumpet / Mokkin×2, ported from `055/`).
3. `VOICES[]` — maps unit number → round offset (beats) + instrument. `Slave.ino`'s `MY_SLAVE_ID` (1–5) selects the row via `player.setVoice()`.

### Stub modules (not yet implemented)

`Scheduler` (Master) and `LedCtrl` (Master) have header stubs. Their `begin()` / `update()` calls are commented out in `Master.ino`; uncomment when implementing. (`Player` and both `LedCtrl` are now implemented.)

### Pin assignments

| Pin | Role |
|-----|------|
| D9  | IR TX (38 kHz carrier out, Master) |
| D2  | IR RX (FALLING edge interrupt, Slave) |
| D4  | Effect LED |
| A0  | Audio out (12-bit DAC → TA7368 amp → speaker, Slave) |

## Git Workflow

Each feature is developed on its own branch and merged to `main` via Pull Request. Branch naming follows contributor IDs (e.g. `055`).

Start of session:
```bash
git checkout main && git pull origin main && git checkout <branch> && git merge main
```

Commit prefix conventions: `feat:`, `fix:`, `docs:`, `style:`, `refactor:`, `test:`, `chore:`.
