# Poseidon MkVI — Battery & HUD Emulator / Test-Stand Spec

**Purpose:** Define the bus-facing behaviour of the **Battery (0x43)** and **HUD (0x40)**
processors precisely enough that a test stand can (a) *emulate* both as I2C slaves and
(b) *assert* that the DUT — which plays the Display/Head master role — drives them
correctly.

This file is self-contained: it does not depend on the other RE docs in this repo.
Everything here is derived from firmware reverse engineering of `battery.bin` /
`hud.bin` plus bus captures. Confidence is flagged per item; treat **Working
hypothesis** / **Unverified** items as configurable, not hard assertions.

> **Roles in the test stand**
> - **DUT** = the device under test. It impersonates the **Display (0x41)** and/or
>   **Head (0x42)** — i.e. it is the *master* that sends heartbeats, commands and
>   telemetry, and the consumer of broadcasts/responses.
> - **Emulator** = this code. It presents two slave devices on the bus: **HUD @ 0x40**
>   and **Battery @ 0x43**. It validates every frame the DUT sends and generates the
>   responses/broadcasts the real silicon would.

---

## 1. Physical layer

| Property | Value | Confidence |
|----------|-------|------------|
| Bus | Standard I2C, multi-master | Proven |
| Bit rate | ~100 kHz (battery TWBR=0x2A @ 8 MHz) | Proven |
| Addressing | 7-bit | Proven |
| Pull-ups | External, value uncharacterised (assume 4.7 kΩ) | Assumption |
| Clock stretching | Battery = hardware TWI (may stretch in ISR); HUD = USI bit-bang slave (timing-sensitive, see §6.4) | Strongly inferred |
| General call (0x00) | Both HUD and Battery match general-call address in addition to their own | Strongly inferred |

The emulator should be tolerant of the DUT clock-stretching, and should itself be
able to stretch (or be configured not to, to test the DUT's NoStretch handling).

---

## 2. Addresses

| 7-bit | SLA+W (write byte) | Device | Emulated here? |
|-------|--------------------|--------|----------------|
| 0x40 | **0x80** | HUD | **Yes (slave)** |
| 0x41 | 0x82 | Display | No — this is (one of) the DUT's identities |
| 0x42 | 0x84 | Head | No — this is (one of) the DUT's identities |
| 0x43 | **0x86** | Battery | **Yes (slave)** |

All transactions are **I2C writes**. There are **no I2C read (SLA+R) transactions** on
this bus — a device that needs to "reply" does so by becoming master and *writing* a
response frame back to the requester's address. The emulator must therefore also be
able to act as **master** to deliver Battery responses and the HUD/Battery broadcasts
(§5.2, §6.3).

> If the DUT ever issues a SLA+R to 0x40 or 0x43, that is a **DUT bug** — assert-fail.

---

## 3. Frame format

```
[START] [SLA+W] [CMD] [LEN] [DATA_0 .. DATA_{n-1}] [CRC8] [STOP]
```

| Field | Size | Meaning |
|-------|------|---------|
| SLA+W | 1 (HW) | `addr << 1`, write bit = 0. Included in CRC. |
| CMD   | 1 | Command / message type. |
| LEN   | 1 | **Number of bytes remaining after LEN, i.e. `data_count + 1` (the +1 is the CRC byte).** See note. |
| DATA  | 0–6 | Payload. |
| CRC8  | 1 | See §4. |

### ⚠ LEN semantics — resolved

Two interpretations appear in the older notes. **The bus-capture-verified, and the one
the emulator MUST use, is:**

> **LEN = count of all bytes after the LEN field = (number of DATA bytes) + 1 (CRC).**

Worked examples (all capture-verified):
- Heartbeat, 1 data byte (`0x00`) → `LEN = 0x02`.
- Battery voltage `0x57`, 2 data bytes → `LEN = 0x03`.
- Battery percentage `0x26`, 1 data byte → `LEN = 0x02`.

So **`data_count = LEN − 1`**, and the DATA region is bytes `[3 .. 3+LEN-2]`, with the
CRC at byte index `3 + (LEN-1) = LEN + 2`.

(An earlier table in `I2C_PROTOCOL_SPEC.md` §3 stated "LEN = data bytes only". That is
**superseded** — do not use it. The command tables in this file quote LEN the
capture-verified way.)

**Bounds:** Battery dispatcher validates `data_count ≤ 6` (i.e. `LEN ≤ 7`). Max frame on
wire = 9 bytes (SLA+W, CMD, LEN, up to 5 data, CRC) in normal traffic; reject anything
with `LEN > 7` as malformed.

---

## 4. CRC-8

**Proven across all four processors.**

| Param | Value |
|-------|-------|
| Polynomial | `0x07` (x⁸+x²+x+1) |
| Init | `0x00` |
| Reflect in / out | No / No (MSB-first) |
| Output XOR | None |
| Scope | `[SLA+W, CMD, LEN, DATA_0 .. DATA_{n-1}]` (CRC byte itself excluded) |

A valid frame satisfies `CRC8(SLA+W, CMD, LEN, DATA..., received_CRC) == 0x00`
(running the CRC over the whole frame *including* the CRC yields 0).

```c
// Bitwise reference (HUD/ATtiny84 uses this; Battery uses an equivalent 256-byte LUT).
uint8_t crc8_update(uint8_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    return crc;
}
uint8_t crc8_frame(uint8_t sla_w, uint8_t cmd, uint8_t len, const uint8_t *data, uint8_t n) {
    uint8_t c = 0x00;
    c = crc8_update(c, sla_w);
    c = crc8_update(c, cmd);
    c = crc8_update(c, len);
    for (uint8_t i = 0; i < n; i++) c = crc8_update(c, data[i]);
    return c;
}
```

### 4.1 Golden test vectors (use these to self-check the emulator's CRC + framing)

Each line is the **complete on-wire frame** including SLA+W and CRC. Re-running the CRC
over the whole line must yield `0x00`.

| Description | Wire bytes (hex) | CRC |
|-------------|------------------|-----|
| Heartbeat → Battery (normal) | `86 00 02 00 6F` | 6F |
| Heartbeat → Battery (shutdown) | `86 00 02 01 68` | 68 |
| Shutdown → HUD | `80 00 02 01 1C` | 1C |
| Shutdown → Head | `84 00 02 01 44` | 44 |
| Battery runtime/alarm init | `86 2D 02 00 BD` | BD |
| Battery speaker control 0 (audible, state-dependent) | `86 0E 02 00 43` | 43 |
| Battery speaker control 1 (high tone, state-dependent) | `86 0E 02 01 44` | 44 |
| Battery speaker control 2 (patterned alarm after init) | `86 0E 02 02 4D` | 4D |
| Battery speaker control 3 (stop/reset active alarm state) | `86 0E 02 03 4A` | 4A |
| Heartbeat → HUD | `80 00 02 00 1B` | 1B |
| HUD vibrator ON | `80 0B 02 00 F7` | F7 |
| HUD vibrator OFF | `80 0B 02 01 F0` | F0 |
| HUD red LED ON | `80 0C 02 00 E1` | E1 |
| HUD red LED OFF | `80 0C 02 01 E6` | E6 |
| Battery buddy LED OFF | `86 0D 02 01 F9` | F9 |
| HUD alarm threshold (1000) | `80 A1 03 03 E8 6A` | 6A |
| HUD sensor state 0xA3 | `80 A3 05 00 64 00 64 9E` | 9E |
| Battery O₂ pressure 0x1E | `86 1E 03 0B B8 74` | 74 |
| Battery DS2782 reg read 0x5A (reg 0x0E) | `86 5A 02 0E E6` | E6 |
| HUD mouthpiece broadcast 0x1D → Battery | `86 1D 02 01 5B` | 5B |
| Battery voltage broadcast 0x57 (4065 mV) | `82 57 03 0F E1 1A` | 1A |
| Battery percentage broadcast 0x26 (90%) | `82 26 02 5A 88` | 88 |

---

## 5. HUD device model (slave @ 0x40 / SLA+W 0x80)

The HUD is **slave-only for everything except the mouthpiece broadcast** (§5.2). It runs
an autonomous alarm watchdog (§5.3). Emulating it means: accept the command set below,
and model the watchdog so the test stand can prove the DUT keeps it fed.

### 5.1 Commands the HUD RECEIVES (DUT → HUD). Emulator validates these.

| CMD | LEN | data_count | Payload | Meaning | Confidence |
|-----|-----|-----------|---------|---------|------------|
| 0x00 | 0x02 | 1 | `[0x00]` | Heartbeat / watchdog kick (resets the 2-min timer) | Proven |
| 0x0B | 0x02 | 1 | `[0x00]`=ON, `[0x01]`=OFF | Vibrator control | Proven |
| 0x0C | 0x02 | 1 | `[0x00]`=ON, `[0x01]`=OFF | Red LED control (may be brightness) | Proven |
| 0xA0 | 0x05 | 4 | 4 bytes | Alarm state data (format TBD) | Working hyp. |
| 0xA1 | 0x03 | 2 | `[hi, lo]` | 16-bit PO₂ alarm threshold | Working hyp. |
| 0xA3 | 0x05 | 4 | `[s1_hi, s1_lo, s2_hi, s2_lo]` | Dual PO₂ sensor state for autonomous alarm | Strongly inferred |
| 0xFF | ? | ? | — | Seen in boot traffic; no evidence that it is required to initialise the HUD | Unverified semantics |

Behaviour the emulator should model:
- **Any** validly-framed, CRC-good frame addressed to 0x40 (or general call) **resets
  the heartbeat watchdog** — not just CMD 0x00. (The watchdog is fed by reception, see
  §5.3.) The cleanest assertion is still "DUT must send CMD 0x00 periodically", but model
  the reset on any good RX so you don't false-alarm.
- CMD 0x0B / 0x0C: latch a simulated vibrator/LED state the test can read back /
  assert against (used in pre-dive actuator tests).
- Unknown CMD with good CRC: real HUD ignores it. Emulator should **log + ignore**, not
  fault (but you may flag it as "DUT sent unmodelled CMD 0x?? to HUD" for review).

#### 5.1.1 HUD startup requirements

The HUD does **not** use the Battery's external CMD 0x2D arming process. Its reset path
performs the required initialization locally before entering the receive loop:

1. Configure the actuator GPIOs and their inactive output levels.
2. Initialize Timer1, the 125 ms scheduler, state/event storage and timer slots.
3. Initialize the USI/I2C slave and receive queue.
4. Select the reset-cause state-machine event and enter the main loop.

The host should allow the HUD to finish reset, then send a normal heartbeat:
`80 00 02 00 1B`. Continue sending it at the normal approximately 2-second cadence.
There is no prerequisite CMD 0x2D transaction and no proven need to send CMD 0xFF
before CMD 0x0B/0x0C actuator commands. A conservative bench implementation may wait
20–100 ms after power is stable before the first transaction.

Unlike the Battery, the HUD receive parser feeds its 120-second watchdog after every
CRC-good addressed frame. CMD 0x00 remains the recommended explicit heartbeat because
it makes the host's intent and bus diagnostics unambiguous.

### 5.2 The HUD as MASTER — mouthpiece broadcast (emulator generates this)

The HUD is the only thing that emits CMD 0x1D. On a simulated mouthpiece position
change, the emulator (as master) writes to **all three** other addresses:

| CMD | LEN | data_count | Payload | Destinations |
|-----|-----|-----------|---------|--------------|
| 0x1D | 0x02 | 1 | `[0x00]`=Open-Circuit, `[0x01]`=Closed-Circuit | Display 0x82, Head 0x84, Battery 0x86 |

The test stand should expose a control to inject a mouthpiece change and verify the DUT
(in its Display/Head role) ingests CMD 0x1D and reacts. If the DUT also emulates the
Battery side, note the Battery *also* receives 0x1D (§6.1).

### 5.3 HUD autonomous alarm watchdog — **the key DUT assertion**

- Tick = **125 ms** (Timer1 CompA). Countdown = **960 ticks = 120 s**.
- If no frame is received for **120 seconds**, the HUD **autonomously fires
  vibrator + red-LED alarm** (timer IDs 0x02 and 0x12). **Proven.**
- A received frame reloads the countdown.

**Emulator behaviour:** maintain a 120 s countdown reset on every good RX. If it
expires, raise an emulator event `HUD_WATCHDOG_ALARM` and (optionally) set the
vibrator/LED model active.

**Assertion against the DUT:** in normal operation the DUT MUST send a heartbeat (or any
valid frame) to 0x40 well within 120 s. The real Display does so every ~2 s. Recommend
the test stand **fail** if the inter-frame gap to the HUD exceeds a configurable
threshold (default e.g. 7 s, hard-fail at 120 s). The 120 s figure is deliberately
generous to ride out bus glitches — do not let it lapse mid-dive.

### 5.4 HUD constraints to be aware of when emulating

- ATtiny84-class part: bit-banged USI slave, **8 KB flash**, CRC done bit-by-bit. It is
  more timing-sensitive than the Battery's hardware TWI; if you want fidelity, model a
  modest per-byte processing latency and the possibility of NACKing if hammered.
- GPIOR0 flags (internal): bit0=data ready, bit1=transfer active, bit2=data valid,
  bit4=aux. Not bus-visible; informational only.

---

## 6. Battery device model (slave @ 0x43 / SLA+W 0x86)

Hardware TWI, interrupt-driven, 128-byte RX ring buffer. Acts as **slave** (receives
commands) and **master** (writes responses + periodic broadcasts). The Battery is robust:
**missing dive data never raises an error** (§6.5) — only loss of heartbeat changes
behaviour (§6.2).

### 6.1 Commands the Battery RECEIVES (DUT → Battery). Emulator validates these.

LEN column is on-wire (`data_count + 1`). "→ reply" = the Battery answers by becoming
master and writing the listed CMD back to the requester (normally Display 0x82).

| CMD | LEN | data_count | Payload | Meaning | → Reply | Conf. |
|-----|-----|-----------|---------|---------|---------|-------|
| 0x00 | 0x02 | 1 | `[0x00]`=normal, `[0x01]`=shutdown | Heartbeat (see §6.2) | — | Proven |
| 0x0D | 0x02 | 1 | `[0x00]`=ON,`[0x01]`=OFF | Buddy-light LED | — | Proven |
| 0x0E | 0x02 | 1 | `[0x00..0x03]` state-machine input; after CMD 0x2D initialization, 0x00 is audible, 0x01 is a high tone, 0x02 is the patterned alarm and 0x03 stops/resets the active alarm state | Speaker/beeper control (see §6.2.1) | — | Event mapping and gating proven; acoustic modes bench-derived |
| 0x1D | 0x02 | 1 | `[0x00]`=OC,`[0x01]`=CC | Mouthpiece position (from HUD) | — | Proven |
| 0x1E | 0x03 | 2 | `[hi, lo]` | O₂ cylinder pressure (BE) | — | Strongly inf. |
| 0x1F | 0x03 | 2 | `[hi, lo]` | Diluent cylinder pressure (BE) | — | Strongly inf. |
| 0x26 | 0x02 | 1 | `[pct]` | (also broadcast — see §6.3) | — | Proven |
| 0x27 | 0x02 | 1 | `[param]` | Firmware version request | 0x81 | Working hyp. |
| 0x29 | 0x03 | 2 | telemetry | Telemetry accumulation | — | Working hyp. |
| 0x2C | ? | ? | — | Sets internal flag and queues event 0x17; can conditionally schedule delayed speaker event 0x1B (see §6.2.1) | — | Proven handler/effect; payload and external purpose unknown |
| **0x2D** | **0x02** | **1** | Dummy byte; use `[0x00]` | **One-shot runtime/alarm initialization; queues event 0x07 and makes CMD 0x0E alarm transitions reachable** | status/identity writes to Display 0x41 | **Proven** |
| 0x32 | 0x02 | 1 | `[idx]` | Generic EEPROM read, word at `idx*2`, `idx<0x80` | reply word | Proven |
| 0x3A | ≥0x03 | ≥2 | `[selector][value..]` | EEPROM write (sel 0x01→1B@0x27, sel 0x02→2B@0x2A) | — | Proven |
| 0x3B | 0x02 | 1 | `[selector]` | EEPROM read (sel 0x00→0x26, 0x01→0x27, 0x02→0x2A) | 0x3B `[sel,lo,hi]` | Proven |
| 0x40,0x42,0x44,0x46,0x48,0x4A,0x4C,0x50,0x52,0x96 | var | var | deco frames | Deco data from Head | — | Strongly inf. |
| 0x4D | 0x01 | 0 | — | Read EEPROM 0x2A | 0x4E (2B) | Strongly inf. |
| 0x53 | 0x01 | 0 | — | Read runtime counter | 0x54 (4B) | Strongly inf. |
| 0x57 | — | — | (Battery *generates* this; see §6.3) | Voltage broadcast | — | Proven |
| 0x58 | 0x01 | 0 | — | Read EEPROM 0x34 | 0x59 (2B) | Strongly inf. |
| 0x5A | 0x02 | 1 | `[ds2782_reg]` | **Generic DS2782 register read** | reply 2B | **Proven** |
| 0x5C | 0x03 | 2 | `[reg, val]` | DS2782 register/EEPROM write (auto-commits NV for EEPROM pages) | — | Proven |
| 0x5D | 0x01 | 0 | — | Read 2-byte value | 0x5E (2B) | Working hyp. |
| 0x65 | 0x01 | 0 | — | Learn-cycle init (sets EEPROM 0x2C/0x2E/0x4C = 0xFFFF) | — | Strongly inf. |
| 0x66 | 0x03 | 2 | `[hi, lo]` | O₂ sensor raw millivolts | — | Strongly inf. |
| 0x76 | 0x01 | 0 | — | Read EEPROM 0x3A | 0x77 (4B) | Strongly inf. |
| 0x78 | 0x01 | 0 | — | Read EEPROM 0x38 | 0x79 (2B) | Strongly inf. |
| 0x7E | 0x02 | 1 | `[idx 0x00..0x27]` | Deco compartment write (tissue tension) | — | Strongly inf. |
| 0x80 | 0x04 | 3 | `[idx, hi, lo]` | EEPROM 40-entry table write, `idx<0x28` | — | Proven |
| 0x85 | 0x01 | 0 | — | Task trigger 0x0F | — | Working hyp. |
| 0x8F | 0x05 | 4 | 4 bytes | EEPROM multi-write (runtime snapshot → 0x3A/0x5A) | — | Strongly inf. |
| 0x90 | 0x01 | 0 | — | Clear runtime counter | 0x91 | Strongly inf. |
| 0x97 | 0x01 | 0 | — | Read 1-byte status | 0x98 (1B) | Strongly inf. |
| 0x9D | 0x01 | 0 | — | Clear 2-byte value | 0x9E | Working hyp. |
| **0x2F** | **0x01** | **0** | — | **HARD KILL — service tool only (see §6.7)** | — | Proven |

### 6.2 Heartbeat behaviour (model precisely — it gates battery state)

- **Required:** CMD 0x00 with `data=0x00` must arrive periodically. Real cadence ≈ 2 s.
- A CRC-good CMD 0x00 with `data=0x00` queues internal event **0x15**. Its state-machine
  actions arm/reset timer event **0x13** with a countdown of **960 ticks**.
- The timer tick is **125 ms**, so the Battery heartbeat timeout is
  **960 × 125 ms = 120 seconds**. This is proven from the Timer1 configuration and
  event/action tables.
- **If heartbeats stop:** timer event **0x13** expires and drives the Battery's
  timeout/shutdown state transition. It does **not** start the speaker and does not use
  the immediate TX-flush path described below.
- **Only** CMD 0x00 with `data=0x00` reloads the Battery heartbeat timer. Other valid
  Battery commands are processed but do not substitute for a heartbeat. This differs
  from the HUD, whose watchdog is fed by any good addressed RX.
- **CMD 0x00 with `data=0x01`** = deliberate shutdown signal. It immediately queues
  event **0x14** and `FUN_code_1965` flushes the TX queue. It **does NOT release the PB1
  power-hold** — the battery stays powered until the cell is physically pulled.
  Emulator should model this as "shutdown requested, rail still up".

### 6.2.1 Speaker/beeper control and non-heartbeat tone diagnosis

`FUN_code_0bd5` proves only the mapping from CMD 0x0E payloads to state-machine
events. It does **not** prove four direct audio modes: each event is evaluated against
the Battery's current state, and hardware actions occur only when the corresponding
transition predicates match.

| Data | Internal event | Observed result in tested state | Confidence |
|------|----------------|---------------------------------|------------|
| 0x00 | 0x1D | Audible mode after alarm initialization; used by captured pre-dive speaker test | Bus/current capture + firmware mapping |
| 0x01 | 0x1C | Single higher-pitched tone after alarm initialization | Bench observation + firmware mapping |
| 0x02 | 0x1F | Patterned alarm after alarm initialization | Bench observation + state-transition/action tables |
| 0x03 | 0x1E | Stops/resets an active alarm state; normally silent | Bench observation + state-transition tables |

Payloads above 0x03 fall through to the same internal event as 0x03 in the real
firmware, but the emulator should continue to flag them as invalid protocol values.

There is one additional, indirect audio path: CMD 0x2C sets `DAT_mem_0243`, queues
event 0x17, and—when its state predicate matches—arms event 0x1B for 48 timer ticks
(about 6 seconds). Event 0x1B can then enter the same speaker-pattern machinery.
The external purpose and normal sender of CMD 0x2C remain unresolved, so the emulator
should log it distinctly rather than treating it as an unknown harmless command.

Internal events 0x1B and 0x20 otherwise sequence the same state machine. The heartbeat
timeout path sets the selector that takes event 0x1B's non-audio branch; deliberate
shutdown, DS2782 status events, startup and TWI recovery also do not select an audio
branch. Because the Battery enables general-call matching, a CRC-good general call
carrying CMD 0x0E or CMD 0x2C can also reach these handlers.

For a persistent or delayed high-pitched bench tone, capture traffic to Battery
address 0x43 and general call from power-up. In particular, look for
`86 0E 02 01 44` and for CMD 0x2C in the preceding seconds. Do not infer the audible
result from the payload alone without also knowing the Battery's current state.

### 6.2.2 Battery startup and alarm arming

The Battery reset state is not alarm-ready. The relevant state tokens start as
`[state0=0x00, state2=0x06, state7=0x10]`, while the normal CMD 0x0E start transitions
require `[0x01, 0x05, 0x10]`.

A normal heartbeat queues event 0x15 and changes state0 to 0x01, but it does **not**
change state2 from 0x06 to 0x05. Consequently, heartbeats alone keep the Battery alive
but do not guarantee that a subsequent alarm command will start the speaker.

CMD 0x2D is the missing one-shot initialization:

1. Its handler checks a one-shot guard. The first accepted CMD 0x2D after reset sets
   that guard; later CMD 0x2D frames are no-ops until the Battery resets.
2. It initializes the DS2782/EEPROM-derived Battery runtime state.
3. It queues state-machine event 0x07. That event changes state2 to 0x05 and leaves or
   changes the other alarm predicates to `state0=0x01, state7=0x10`.
4. It queues one or more master-mode status/identity writes to Display address 0x41.

The RX parser rejects a zero-data CMD 0x2D frame, even though its handler does not use
the payload. Send one dummy byte:

```
86 2D 02 00 BD
```

Recommended host startup:

```
wait for Battery power/reset to settle (20–100 ms)
send 86 00 02 00 6F       # normal heartbeat; starts heartbeat supervision
send 86 2D 02 00 BD       # one-shot runtime/alarm initialization
allow the Battery to perform DS2782/EEPROM work and master-mode replies
continue 86 00 02 00 6F approximately every 2 seconds
only then send CMD 0x0E alarm controls
```

The host should be prepared to act as I2C slave 0x41 and ACK the Battery's response
writes. If it does not consume them, leave the bus idle long enough for the Battery's
bounded retry attempts to finish before sending further commands.

Once an alarm-start event succeeds, state7 changes from idle token 0x10 to active token
0x11. Repeating the same start command while already active does not necessarily
restart its sequencer. Send CMD 0x0E data 0x03 to return it to the idle alarm state
before starting another mode during deterministic bench tests.

### 6.3 Periodic broadcasts the Battery GENERATES (emulator emits as master)

The test stand should drive these out on a timer so the DUT's RX path can be exercised.

| CMD | LEN | data_count | Payload | Cadence | Meaning | Conf. |
|-----|-----|-----------|---------|---------|---------|-------|
| 0x06 | 0x03 | 2 | `[lo, hi]` (LSB first!) | on-change | DS2782 instantaneous current, 16-bit **signed**, **LSB byte first**. Negative = discharging. | Proven |
| 0x26 | 0x02 | 1 | `[pct]` | ~4 s | Battery %, **direct** (0x52=82%, 0x5A=90%) → Display 0x82 | Proven |
| 0x57 | 0x03 | 2 | `[hi, lo]` | ~20 s | Battery voltage, **direct millivolts** (0x0FE1=4065 mV) → HUD/Display/Head | Proven |

> **Byte-order trap:** CMD 0x06 current is **little-endian** (LSB first) while pressures
> (0x1E/0x1F) and voltage (0x57) are **big-endian** (hi first). Don't unify them.

Other Battery→master replies (answers to the read commands in §6.1): 0x3B, 0x4E, 0x54,
0x59, 0x5E, 0x77, 0x79, 0x7B, 0x81, 0x91, 0x98, 0x9E to Display (0x82); 0x4F, 0x51 to
Head (0x84). The emulator only needs to generate the replies for the read commands the
DUT actually exercises — give those realistic, configurable payloads.

### 6.4 DS2782 fuel-gauge model (optional, for current/voltage realism)

CMD 0x5A reads any DS2782 register; 0x5C writes. Useful registers:
- `0x0E/0x0F` CURRENT (signed, 1.5625 µV / Rsense per LSB)
- `0x08/0x09` average current
- `0x0A/0x0B` temperature (`(int16 >> 5) * 0.125 °C`)
- `0x0C/0x0D` voltage
- EEPROM pages `0x20–0x2F`, `0x60–0x7F` (writes auto-commit to NV)

If the test stand wants to assert current draw of DUT-controlled loads, model the
DS2782 CURRENT register and have CMD 0x06 fire when it changes. (Capture baselines:
~−268..−553 counts idle; +519 HUD LED, +1619 buddy LED, +2067 vibrator, +2998..+3760
solenoids, +7919 speaker — sign negative = discharge.)

### 6.5 Battery resilience (don't over-assert)

The Battery stores dive data (0x1E, 0x1F, 0x29, 0x66, 0x7E, deco frames) into RAM and
**raises no error if any of it is missing or never sent.** The emulator must NOT fault
the DUT for omitting these. Only heartbeat loss (§6.2) and CRC/framing errors are real
faults.

### 6.6 Normal low-power shutdown sequence

Normal shutdown uses CMD 0x00 with data 0x01. It is a soft, recoverable low-power
request, distinct from the service-only CMD 0x2F hard cutoff in §6.7.

Before sending the shutdown request, explicitly turn off controllable loads so the
result does not depend on the current actuator/alarm state:

```
80 0B 02 01 F0    # HUD vibrator OFF
80 0C 02 01 E6    # HUD red LED OFF
86 0D 02 01 F9    # Battery buddy LED OFF
86 0E 02 03 4A    # Battery speaker stop/reset
```

Then send the shutdown heartbeat to each powered peer. The Head frame is included for
system firmware even though the emulator described by this document primarily models
the HUD and Battery:

```
80 00 02 01 1C    # HUD shutdown
84 00 02 01 44    # Head shutdown
86 00 02 01 68    # Battery shutdown; send last
```

Wait for each addressed write to be ACKed and completed before sending the next one.
After the Battery frame completes, stop periodic heartbeats and nonessential bus
traffic, then place the controlling firmware in its own lowest-power state or remove
the switched peripheral rails.

Observed firmware effects:

- **HUD:** data 0x01 queues internal event 0x13 and clears its queued-transmission
  state. The interrupt-driven main loop executes `SLEEP` whenever it is idle. There is
  no separate HUD hard-off command.
- **Battery:** data 0x01 queues internal event 0x14 and immediately clears its TX
  queue. Its main loop sleeps while idle, but TWI and PB1 power-hold remain enabled.
  This reduces activity but does not electrically disconnect the pack.
- **Head:** data 0x01 queues internal event 0x29, clears TX and saves final state.

Do **not** implement shutdown by merely stopping normal heartbeats. Without the
explicit shutdown frame, heartbeat loss is a fault: after 120 seconds the HUD can
activate its autonomous LED/vibrator alarm, increasing rather than reducing current.

The emulator should accept the four actuator-off writes followed by the shutdown
frames, suppress normal post-shutdown heartbeat assertions for those devices, and
record a clean `LOW_POWER_SHUTDOWN` result. Any later reset/power-up starts a new
session and requires the startup sequence in §5.1.1 and §6.2.2.

### 6.7 ⚠ CMD 0x2F — must never be issued by the DUT

CMD 0x2F (`LEN=0x01`, no data) is a **hard kill**. On the Battery it saves state, sets
`TWCR=0x80`, **drops the PB1 power-hold pin and halts permanently** — recovery needs a
physical re-seat. On the Head it saves state and halts. Byte-pattern search across all
four field binaries proves **no field firmware ever emits 0x2F** — it is a
factory/service-tool entry point only.

**Assertion:** if the DUT ever sends CMD 0x2F to 0x40 or 0x43, the emulator must
**hard-fail the test** (and obviously not "die"). This is the single most dangerous
command on the bus.

---

## 7. Assertions the emulator should enforce against the DUT

Group these so the test stand can report precisely which contract the DUT broke.

### 7.1 Framing / integrity (every frame)
1. **CRC valid** — recompute over `[SLA+W,CMD,LEN,DATA...]`; mismatch → `FAIL: bad CRC`.
   (Real silicon silently discards; the test stand should *flag* it, since a correct DUT
   never emits a bad CRC.)
2. **LEN sane** — `1 ≤ LEN ≤ 7`; DATA length on the wire matches `LEN−1`; STOP lands
   where LEN predicts. Otherwise `FAIL: malformed LEN`.
3. **Known CMD for that address** — CMD is in the receiving device's table (§5.1/§6.1).
   Unknown but well-framed → `WARN: unmodelled CMD` (configurable to fail).
4. **No SLA+R** to 0x40/0x43 → `FAIL: unexpected read transaction`.

### 7.2 Per-command payload checks
- CMD 0x00 data ∈ {0x00, 0x01}.
- CMD 0x0B/0x0C/0x0D data ∈ {0x00, 0x01}.
- CMD 0x0E data ∈ {0x00..0x03}.
- CMD 0x2C is recognized and logged as a delayed-state/speaker trigger; its payload
  format is unresolved, so do not impose a payload assertion yet.
- CMD 0x2D must have `LEN=0x02` and one dummy data byte; use data 0x00.
- CMD 0x1D data ∈ {0x00, 0x01}.
- CMD 0x7E / 0x80 index ≤ 0x27.
- CMD 0x3A selector ∈ {0x01, 0x02} to have effect (0x00/other = no-op, not an error).
- CMD 0x2F to either device → **hard fail** (§6.7).

### 7.3 Liveness / timing
- **HUD heartbeat:** inter-frame gap to 0x40 < configurable warn (default 7 s),
  hard-fail at 120 s (§5.3).
- **Battery heartbeat:** CMD 0x00(0x00) cadence within configurable window (~2 s
  nominal; warn if it drifts, fail on `BATTERY_HEARTBEAT_LOST`).
- **Heartbeat order (optional fidelity check):** real Display emits HUD→Battery→Head
  with ~1.6 ms spacing, groups ~2 s apart. Only assert if the DUT claims to reproduce
  Display timing.

### 7.4 Reaction to emulator-generated traffic
- After the emulator injects a HUD CMD 0x1D (mouthpiece), assert the DUT acknowledges /
  reacts (test-specific).
- After Battery broadcasts 0x26 / 0x57 / 0x06, assert the DUT ingests them without
  NACK-storming or mis-parsing (e.g. reads back correct mV / %).

---

## 8. Suggested emulator config surface

```yaml
bus:
  bitrate_hz: 100000
  clock_stretch: true          # emulator may stretch; set false to test DUT NoStretch
hud:                            # slave @ 0x40
  enabled: true
  watchdog_s: 120              # PROVEN
  heartbeat_warn_s: 7
battery:                        # slave @ 0x43
  enabled: true
  require_runtime_init_2d: true
  heartbeat_lost_s: 120        # PROVEN: 960 × 125 ms, expiry event 0x13
  broadcasts:
    voltage_mv: 4012           # CMD 0x57, big-endian
    voltage_period_s: 20
    percent: 90                # CMD 0x26
    percent_period_s: 4
    current_counts: -300       # CMD 0x06, signed, little-endian, on-change
assertions:
  unknown_cmd: warn            # warn | fail | ignore
  bad_crc: fail
  cmd_2f: fail                 # keep as fail — hard kill
```

---

## 9. Confidence & open items

**Proven (safe to hard-assert):** CRC-8 (poly/init/scope), frame format, LEN semantics
(data+CRC), addresses, CMD 0x00/0x0B/0x0C/0x0D/0x0E/0x1D/0x06/0x26/0x57, CMD 0x5A generic
DS2782 read, CMD 0x2F hard-kill (do not send), HUD 120 s watchdog, Battery 120 s
heartbeat timeout (event 0x13), deliberate-shutdown event 0x14, and CMD 0x0E's
payload-to-event mapping. Battery CMD 0x2D's one-shot guard, runtime initialization
call and event 0x07 alarm-state transition are also proven.

**Strongly inferred:** most Battery read/EEPROM commands and reply CMDs; pressure/sensor
big-endian layout; deco frames.

**Working hypothesis / configure rather than assert:** HUD 0xA0/0xA1/0xA3 payload
formats; reply CMD bytes for some DS2782 EEPROM reads; HUD CMD 0xFF semantics; full
CMD 0x0E audible behaviour outside the initialized idle/active alarm states.

**Open questions for the bench (resolve before locking the corresponding assertions):**
1. CMD 0x0E behaviour in Battery states other than the initialized idle/active alarm
   states. Current initialized-state result: 0x00 audible, 0x01 high tone, 0x02
   patterned alarm and 0x03 stop/reset.
2. HUD CMD 0xA3 4-byte decode (which sensor, scaling) for meaningful alarm injection.
3. The meaning of HUD CMD 0xFF. It is not required for local HUD startup, but its
   observed boot-traffic role remains unresolved.
4. Reply-CMD byte for DS2782 EEPROM reads via 0x5A (needs capture confirmation).
