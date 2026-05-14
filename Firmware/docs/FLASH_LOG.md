# Flash Log Subsystem

Persistent on-chip dive log captured to the external SPI NOR. Two
parallel FCB instances cover the storage; bulk download is over UDS
ISO-TP. This document is the source of truth for the on-flash format,
partition layout, recovery semantics, and runtime knobs.

Source:
- `include/flash_log.h` — public producer / stats / config API
- `src/flash_log/flash_log.c` — FCB instances, writer thread, ingest queue, settings handler
- `src/flash_log/flash_log_entries.h` — TLV record structs
- `src/flash_log/flash_log_backend.c` — Zephyr log_backend adapter for the text FCB
- `src/flash_log/flash_log_listeners.c` — zbus listeners that feed the telemetry FCB
- `src/flash_log/flash_log_reader.{c,h}` — selector resolution + streaming cursor
- `src/divecan/uds/uds_log_download.c` — UDS 0x34/0x36/0x37 + 0xF1xx RoutineControl

## Why two FCBs

A single FCB caps at 255 logical sectors (`uint8_t f_sector_cnt`) and
needs an in-RAM 8-byte descriptor per sector. The STM32L431 has only
64 KiB SRAM — running one FCB at the native 4 KiB sector size would
cover ~1 MiB of flash, which is too small for multi-dive history.
Bumping to 64 KiB block-erase "logical sectors" raises the per-FCB
cap to 16 MiB, but the project budget for stack-instrumented builds
forces a lower 100-sector working point ≈ 6.4 MiB per FCB.

Splitting into telemetry + text doubles the addressable space at
the cost of a second `f_sectors` array, giving ~12.8 MiB total log
capacity at ~1.6 KiB RAM.

## Partition layout

Defined in `boards/quickrecon/divecan_jr/divecan_jr.dts`:

| Label                    | Offset       | Size      | Purpose                          |
|--------------------------|--------------|-----------|----------------------------------|
| `log-telemetry`          | `0x00070000` | 6400 KiB  | FCB-A: structured telemetry      |
| `log-text`               | `0x006b0000` | 6400 KiB  | FCB-B: LOG_x text capture        |
| (unallocated)            | `0x00cf0000` | ≈47 MiB   | reserved for future expansion    |
| `storage`                | `0x03ff8000` | 32 KiB    | Zephyr NVS (settings)            |

Each FCB declares 100 × 64 KiB logical sectors. The SPI NOR driver
decomposes a 64 KiB `flash_area_erase` into either 16× 4 KiB sector
erases or a single 64 KiB block erase based on JEDEC opcode support.

## Stream allocation

| Stream    | What goes in                                                                          |
|-----------|---------------------------------------------------------------------------------------|
| Telemetry | Consensus snapshots, cell raw samples, PID snapshots, solenoid fire events, errors    |
| Text      | LOG_x messages (formatted via `log_output`)                                            |
| Both      | BOOT_MARKER, DIVE_START, DIVE_END (mirrored so either stream can be partitioned alone) |

CAN frame logging is off by default — semantic protocol events become
structured telemetry records and any unhandled frame produces a
LOG_WRN that lands in the text stream. Runtime toggle via UDS DID
`0xF284 LOG_CAN_VERBOSE` re-enables it for protocol-debug sessions.

## On-flash format

FCB handles framing (length encoding + per-entry CRC + end marker).
Each entry's *payload* is a TLV record:

```c
struct fl_entry_hdr {
    uint8_t  type;        /* FlashLogType_t */
    uint8_t  flags;       /* bit 0: preceded by drops */
    uint16_t length;      /* payload bytes after this header */
    uint64_t ts_boot_us;  /* k_uptime_get monotonic µs */
};
```

Followed by `length` bytes of type-specific payload (see
`src/flash_log/flash_log_entries.h`). Endianness throughout: little.

### Entry types

| Code   | Name              | Stream  | Payload key fields                                       |
|--------|-------------------|---------|----------------------------------------------------------|
| `0x01` | BOOT_MARKER       | both    | boot_id, fw_ver_str[16], reset_cause, prev_crash_* (16 B)|
| `0x02` | DIVE_START        | both    | dive_number u16 + unix_timestamp u32                     |
| `0x03` | DIVE_END          | both    | dive_number u16 + unix_timestamp u32                     |
| `0x04` | CAN_RX            | telem   | id u32 + dlc u8 + data[8] (gated off by default)         |
| `0x05` | CAN_TX            | telem   | id u32 + dlc u8 + data[8] (gated off by default)         |
| `0x10` | CONSENSUS         | telem   | 3× ppo2, 3× mV, packed status+include, confidence, setpoint |
| `0x11` | PID_SNAPSHOT      | telem   | integral f32, saturation_count u16, duty f32, setpoint u8|
| `0x12` | SOLENOID_FIRE     | telem   | kind u8 (0=start, 1=end), requested_on_us, off_us        |
| `0x20` | CELL_RAW_DIVEO2   | telem   | idx, ppo2, temp_dC, err_code, phase, intensity, ambient, pressure_uhpa, humidity_mRH |
| `0x21` | CELL_RAW_O2S      | telem   | idx, ppo2, status                                        |
| `0x22` | CELL_RAW_ANALOG   | telem   | idx, ppo2, raw_adc i32, millivolts u16                   |
| `0x30` | ERROR_EVENT       | telem   | code u32 + detail u32                                    |
| `0x40` | LOG_TEXT          | text    | level u8 + module_id u16 + text bytes (length-bound)     |
| `0xFE` | DROP_MARKER       | either  | count u32 + last_dropped_type u8 (synthetic, per-FCB)    |
| `0xFF` | END_OF_STREAM     | —       | — (download-only synthetic, never on flash)              |

### Capture cadence

Telemetry is **event-driven** — exactly one entry per upstream zbus
publish or per discrete occurrence. There is no fixed-rate sampling
in the flash log subsystem itself.

| Source                  | Effective rate today                          |
|-------------------------|-----------------------------------------------|
| Consensus               | One per `chan_consensus` publish (≈10 Hz)     |
| Cell raw                | One per `chan_cell_N` publish (driver-determined; DiveO2/O2S ≈1 Hz, analog higher) |
| PID                     | One per PID iteration (≈0.2 Hz)               |
| Solenoid fire start/end | Two per fire cycle (≈0.4 Hz peak)             |
| Errors                  | One per `chan_error` publish                  |
| Dive markers            | One per `DIVING_ID` CAN frame                 |
| Boot marker             | One per boot                                  |
| LOG_TEXT                | Subset of LOG_x output above runtime severity threshold |

## Ingest pipeline

```
                                          ┌─────────────────────────────┐
producers (ISR / threads / log backend) → │ k_msgq (CONFIG_FLASH_LOG_   │
                                          │  QUEUE_DEPTH × MAX_ENTRY_   │
                                          │  BYTES, K_NO_WAIT enqueue)  │
                                          └────────────┬────────────────┘
                                                       │
                                          ┌────────────▼────────────────┐
                                          │ flash_log_writer thread     │
                                          │ (priority 9, heartbeat slot │
                                          │  HEARTBEAT_FLASH_LOG)       │
                                          └─┬──────────────────────────┬┘
                                            │                          │
                                ┌───────────▼────────────┐   ┌─────────▼──────────┐
                                │ telemetry FCB          │   │ text FCB           │
                                │ 100 × 64 KiB sectors   │   │ 100 × 64 KiB       │
                                │ f_scratch_cnt = 1      │   │ f_scratch_cnt = 1  │
                                └────────────────────────┘   └────────────────────┘
```

The writer thread is the only path that touches the FCB instances —
producers only ever enqueue. This breaks the recursion path between
LOG_x (which feeds the text FCB) and SPI NOR errors (which would
otherwise log back through the same FCB): the SPI driver runs on the
writer thread, but its LOG_x calls are deferred to the Zephyr log
processing thread, which goes through the queue, not directly to
flash.

### Drop policy

All producers use `K_NO_WAIT`. On enqueue failure they atomically
increment a per-FCB drop counter and store the dropped type. The
writer thread emits a synthesised `DROP_MARKER` entry into the
affected FCB on the next iteration so downstream tools can detect
gaps.

Producers never call `LOG_x` (would recurse). All log warnings about
flash-log subsystem state come from outside the producer hot path.

### Wrap behaviour

Per-FCB ring buffer: on `fcb_append → -ENOSPC` the writer calls
`fcb_rotate` (erases the oldest sector) and retries the append once.
The most recent dive is always retained at the cost of the oldest
boots rolling off first.

## Marker mirroring

BOOT_MARKER, DIVE_START, and DIVE_END are appended to both FCBs by
the writer thread. When a producer enqueues a marker with
`dest = FL_DEST_TELEMETRY` (the default), the writer writes it to
telemetry first, then writes a second copy to text — directly, not
via the queue, so the mirror cannot be dropped midway. This makes
either stream independently partitionable by boot or dive ID.

## Boot counter

Monotonic `uint32_t` stored as the Zephyr setting `log/boot_id` in
the existing NVS partition. Incremented on every successful
`flash_log_init()` and emitted in the BOOT_MARKER payload. If the
NVS read fails the boot_id is set to `0x80000000 | low_bits(uptime)`
so the marker is still distinguishable but flagged as uncertain.

## Power-loss recovery

FCB drops half-written entries on the next mount: each entry carries
an inline CRC + end-marker, and `fcb_init` skips records whose
framing doesn't match. The currently-in-progress entry at brownout
is lost; anything still in the ingest queue is also lost. No
battery-backed RAM tier — this is documented and accepted.

If `fcb_init` fails outright (corrupt sector at mount), the init
path tries one full-partition erase + re-init. If that also fails,
the writer thread suspends itself, the producers still increment
drop counters (so the failure is observable), and `OP_ERR_FLASH` is
published to `chan_error`. The head never reboots on flash-log
failure — log loss is not a safety event.

## Retrieval

Bulk download is over UDS. The protocol — RoutineControl selectors,
0x34/0x36/0x37 download triple, claim shim against OTA, on-the-wire
stream framing — is documented in `UDS.md` under
[Flash Log Download Protocol](../UDS.md#flash-log-download-protocol-0xf1xx--0x340x360x37).

The reader uses a lazy per-FCB sector index (100 × 8 B = 800 B per
FCB). Built by one `fcb_walk` that inspects only marker entries.
Invalidated and rebuilt on entry to a UDS programming session.

## Runtime knobs

| Setting key       | UDS DID  | Default       | Purpose                                  |
|-------------------|----------|---------------|------------------------------------------|
| `log/boot_id`     | —        | starts at 1   | Monotonic boot counter; managed internally |
| `log/rtt_level`   | 0xF283   | 2 (LOG_WRN)   | Min severity captured to text FCB        |
| `log/can_verbose` | 0xF284   | 0 (off)       | Bit 0=RX, bit 1=TX capture into telem FCB |

All Kconfig options live under `src/Kconfig.flash_log`:

| Kconfig                              | Default | Purpose                                |
|--------------------------------------|---------|----------------------------------------|
| `CONFIG_FLASH_LOG`                   | y       | Master enable                          |
| `CONFIG_FLASH_LOG_QUEUE_DEPTH`       | 8       | Ingest k_msgq slot count               |
| `CONFIG_FLASH_LOG_MAX_ENTRY_BYTES`   | 96      | Slot size in RAM (12 B header + payload) |
| `CONFIG_FLASH_LOG_WRITER_STACK`      | 512     | Writer thread stack                    |
| `CONFIG_FLASH_LOG_WRITER_PRIORITY`   | 9       | Below safety-critical threads          |
| `CONFIG_FLASH_LOG_DEFAULT_RTT_LEVEL` | 2       | Initial value of `log/rtt_level`       |
| `CONFIG_FLASH_LOG_CAN_VERBOSE_DEFAULT` | 0x00  | Initial value of `log/can_verbose`     |

## Capacity sizing

At today's producer rates (CAN disabled by default):

| Source                  | Bytes/sec |
|-------------------------|-----------|
| Consensus @ 10 Hz       | ~280      |
| Cell raw 1 Hz × 3 cells | ~93       |
| PID @ 0.2 Hz            | ~5        |
| Solenoid fire ≈0.4 Hz   | ~9        |
| Errors / markers        | <5        |
| **Total during dive**   | ~390      |

`6.4 MiB / 1.4 MB/h ≈ 4.5 dive-hours` of continuous high-rate
logging on the telemetry FCB before the oldest boot starts to roll
off. Realistic mixed-day load (dive + surface idle) extends this to
days.

The text FCB at WRN+ default sees on the order of KB/hour — plenty
of room for incident postmortems. Bumping `log/rtt_level` to INF or
DBG for developmental dives shortens that to hours.

## Coordination with OTA / factory restore

The flash log shares the SPI NOR with MCUBoot slot1, the scratch
sector, and the factory backup. The writer is paused around two
operations that monopolise the bus:

- `boot_request_upgrade(BOOT_UPGRADE_TEST)` in `uds_ota.c` and the
  force-revert path in `uds.c`
- `factory_image_restore_to_slot1()` in the restore-factory WDBI
  handler

`flash_log_pause()` sets an atomic flag the writer checks before each
`fcb_append`; the ingest queue keeps buffering. Resume releases the
flag and the writer drains the backlog. Reboot-imminent paths don't
strictly need resume but pair the calls for clarity.
