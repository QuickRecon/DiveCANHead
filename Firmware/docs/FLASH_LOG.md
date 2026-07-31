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
cover ~1 MiB of flash, which is too small for multi-dive history. The
descriptor RAM, not the 64 MB chip, is the binding constraint, so the
lever for capacity is sector *size*: at 256 KiB logical sectors the
SPI NOR driver decomposes each `flash_area_erase` into 4× 64 KiB block
erases on rotate, and a handful of descriptors map most of the chip.

The two FCBs are sized independently — telemetry is the hot stream,
text is low-rate:
- telemetry: 192 × 256 KiB = **48 MiB** (1536 B descriptors)
- text:       32 × 256 KiB = **8 MiB**  ( 256 B descriptors)

~1.75 KiB of descriptor RAM across both. At the measured
~168 KiB/min telemetry rate, 48 MiB ≈ **4.9 h** of continuous
high-rate logging before the ring wraps (was ~39 min at the previous
100 × 64 KiB / 6.4 MiB working point).

## Partition layout

Defined in `boards/quickrecon/divecan_jr/divecan_jr.dts`:

| Label                    | Offset       | Size      | Purpose                          |
|--------------------------|--------------|-----------|----------------------------------|
| `log-telemetry`          | `0x00080000` | 48 MiB    | FCB-A: structured telemetry      |
| `log-text`               | `0x03080000` | 8 MiB     | FCB-B: LOG_x text capture        |
| (unallocated)            | `0x03880000` | ≈7.5 MiB  | reserved for future expansion    |
| `storage`                | `0x03ff8000` | 32 KiB    | Zephyr NVS (settings)            |

Each FCB declares 256 KiB logical sectors (telemetry 192, text 32).
The SPI NOR driver decomposes a 256 KiB `flash_area_erase` into 4×
64 KiB block erases (or 64× 4 KiB sector erases) based on JEDEC opcode
support. Partition offsets and sizes must stay 256 KiB-aligned and
match the per-FCB sector counts in `flash_log.c`.

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

FCB handles framing (length encoding + end marker). The per-entry **CRC
is disabled** (`FCB_FLAGS_CRC_DISABLED`, set on both FCB instances at
their definition in `flash_log.c`; requires `CONFIG_FCB_ALLOW_FIXED_
ENDMARKER`) — entries are validated by a fixed end-marker byte, not a
CRC. This is a boot-time performance requirement, not a preference; see
[Boot mount cost & the active-sector walk](#boot-mount-cost--the-active-sector-walk).

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
| `0xFD` | BATCH             | telem   | container: packed `[fl_entry_hdr + sub-payload]×N` (one flush of telemetry sub-records) |
| `0xFE` | DROP_MARKER       | either  | count u32 + last_dropped_type u8 (synthetic, per-FCB)    |
| `0xFF` | END_OF_STREAM     | —       | — (download-only synthetic, never on flash)              |

Telemetry records are not written one-per-FCB-entry; one `BATCH` entry
holds all the telemetry sub-records from a single 2 s flush (see
[Boot mount cost](#boot-mount-cost--the-active-sector-walk)). A `BATCH`
payload is a packed sequence of `[fl_entry_hdr + sub-payload]`, byte-for-
byte the same layout a standalone entry would have on flash, so a reader
just recurses into it. Markers (`BOOT_MARKER`/`DIVE_START`/`DIVE_END`)
and `LOG_TEXT` are still written as individual entries.

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

A recovered noinit crash is saved separately in the five-entry
`bootdiag/crashes` NVS ring before the FCB mounts. After the text backend is
ready, `main.c` emits the full recovered snapshot as `LOG_ERR`, so the same
event also appears as an ordinary `LOG_TEXT` record. The dedicated ring is
available through UDS DID `0xF255`; it does not depend on the much larger FCB
ring retaining the relevant boot.

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
                                │ 192 × 256 KiB (48 MiB) │   │ 32 × 256 KiB (8MiB)│
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

## Dedicated boot diagnostics

The NVS `bootdiag` subtree is intentionally independent from the FCB log:

| Setting key | Retention | UDS DID | Contents |
|-------------|-----------|---------|----------|
| `bootdiag/crashes` | newest 5 crashes | `0xF255` | reboot sequence, reason, PC, LR, CFSR, thread |
| `bootdiag/reboots` | newest 5 startups | `0xF256` | reboot sequence and Zephyr `hwinfo` reset-cause flags |

`boot_history_init()` runs before `flash_log_init()`. It reads and clears the
hardware reset flags immediately so causes do not accumulate across later
resets. The captured value is passed into the FCB `BOOT_MARKER`; the boot
marker no longer re-reads the cleared hardware register.

## Power-loss recovery

FCB drops half-written entries on the next mount: each entry carries
an end-marker (CRC disabled — see On-flash format), and `fcb_init`
stops the active-sector walk at the first record whose framing/marker
doesn't match. The currently-in-progress entry at brownout is lost;
anything still in the ingest queue is also lost. No battery-backed RAM
tier — this is documented and accepted.

If `fcb_init` fails outright (corrupt sector at mount), the init path
tries one full-partition erase + re-init, wrapped in
`heartbeat_set_long_op` so the multi-second erase of the 48 MiB
telemetry partition can't trip the IWDG (the spi_nor driver k_sleeps
on WIP, so the low-prio feeder still runs and feeds during the erase).
`fcb_init` bails fast (`-ENOMSG`, before any entry walk) when sector 0's
magic doesn't match `FL_FCB_MAGIC`, so a format/geometry change is a
quick reject → erase → clean re-init; **bump `FL_FCB_MAGIC` on any
on-flash format change** (it is also the lever to force a one-shot
recovery erase). If the re-init also fails, the writer thread suspends
itself, producers still increment drop counters (so the failure is
observable), and `OP_ERR_FLASH` is published to `chan_error`. The head
never reboots on flash-log failure — log loss is not a safety event.

## Boot mount cost & the active-sector walk

`fcb_init` walks the **active sector** on every boot to find the append
point. Two properties of this walk drove the on-flash format choices
above; getting them wrong makes the head **hang at boot** (not reset —
the SPI reads k_sleep, so the watchdog stays fed and the board sits at
~15 mA grinding SPI, never reaching normal operation):

1. **CRC disabled / fixed end-marker.** With per-entry CRC, the walk
   must *read every byte of every entry* to recompute the CRC — i.e.
   read the whole (up to 256 KiB) active sector over the 6 MHz SPI NOR
   on every boot (6 MHz is the ceiling at the 12 MHz low-power SYSCLK).
   On a filled sector that grinds for tens of seconds to minutes. The
   fixed end-marker lets the walk read only the ~3-byte header+marker
   per entry, so cost scales with entry **count**, not entry **data**.

2. **Telemetry batched into one entry per flush.** Even reading 3 bytes
   per entry, the walk scales with entry *count*. At one FCB entry per
   telemetry record (≈10–15/s), a 256 KiB sector holds thousands of
   entries and the walk still grinds on this slow/flaky flash. Writing
   one `BATCH` entry per 2 s flush cuts that to ~one entry per flush
   (≈30/min), bounding the walk regardless of dive length. (Markers stay
   individual so the lazy reader index — which only inspects marker
   entries — still finds dive/boot markers without a deep walk.)

Verified on the rig: a 10-minute fill that previously hung 100 s+
boots in ~2–6 s with both changes. The 192-sector / 256 KiB geometry is
forced by FCB's `uint8_t f_sector_cnt` (≤255) over a 48 MiB partition —
so the per-boot walk is inherent and these two levers (not a faster bus,
which is clock-capped) are what keep it bounded.

## Retrieval

Bulk download is over UDS. The protocol — RoutineControl selectors,
0x34/0x36/0x37 download triple, claim shim against OTA, on-the-wire
stream framing — is documented in `UDS.md` under
[Flash Log Download Protocol](../UDS.md#flash-log-download-protocol-0xf1xx--0x340x360x37).

The reader uses a lazy per-FCB sector index (one `FlashLogIndexEntry_t`
per logical sector — 192 for telemetry, 32 for text). Built by one
`fcb_walk` that inspects only marker entries.
Invalidated and rebuilt on entry to a UDS programming session.

**The index build is asynchronous.** A cold index would take many seconds to
walk on a populated ring, so the index-backed selectors (`0xF101`–`0xF104`)
resolve on a dedicated lower-priority worker thread
(`fl_resolve_worker_tid` in `uds_log_download.c`) rather than blocking the
DiveCAN RX thread. While the worker builds the index the selector answers
**NRC 0x21 (busyRepeatRequest)** and the client re-polls the identical
selector until it resolves. The worker pins the shared maintenance arena
against eviction for the walk's duration (`maint_arena_log_index_set_building`)
so a concurrent OTA/factory/autotune op can't clobber the half-built index —
that op is briefly denied (`-EBUSY`) and retries instead.

**"Download all" bypasses the index entirely.** Selector `0xF106`
(`flash_log_reader_resolve_all`) resolves a cleared range (begin/end NULL) that
the streaming cursor walks oldest→newest with no marker index, no arena, and no
walk — so it is O(1), never blocks, and never defers with `0x21`. It is the
walk-free path for retrieving the whole log.

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
| `CONFIG_FLASH_LOG_SECTOR_SIZE`       | 262144  | Logical FCB sector size                |
| `CONFIG_FLASH_LOG_TELEMETRY_SECTOR_COUNT` | 192 | Telemetry FCB sector descriptors     |
| `CONFIG_FLASH_LOG_TEXT_SECTOR_COUNT` | 32      | Text FCB sector descriptors            |
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
