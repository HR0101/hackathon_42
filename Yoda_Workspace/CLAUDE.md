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

### Stub modules (not yet implemented)

`Scheduler`, `LedCtrl` (Master) and `Player`, `LedCtrl` (Slave) have header stubs. Their `begin()` / `update()` calls are commented out in the `.ino` files; uncomment when implementing.

### Pin assignments

| Pin | Role |
|-----|------|
| D9  | IR TX (38 kHz carrier out, Master) |
| D2  | IR RX (FALLING edge interrupt, Slave) |
| D4  | Effect LED |

## Git Workflow

Each feature is developed on its own branch and merged to `main` via Pull Request. Branch naming follows contributor IDs (e.g. `055`).

Start of session:
```bash
git checkout main && git pull origin main && git checkout <branch> && git merge main
```

Commit prefix conventions: `feat:`, `fix:`, `docs:`, `style:`, `refactor:`, `test:`, `chore:`.
