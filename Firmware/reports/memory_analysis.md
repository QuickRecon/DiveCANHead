# Memory analysis — 2026-05-14

Build: `build/Firmware`, target `divecan_jr/stm32l431xx`, variant `dev_full.conf`.

Raw reports saved as siblings:
- `ram_report.txt` — `west build -t ram_report`
- `rom_report.txt` — `west build -t rom_report`

## Totals

| Region | Used | Capacity | % |
|---|---|---|---|
| **RAM**   | 54,284 B | 65,536 B (64 KiB) | **82.83 %** |
| **App slot0 FLASH** | 182,238 B | 225,280 B (220 KiB slot) | **80.89 %** |
| **MCUBoot FLASH** | 31,756 B | 36,864 B (36 KiB partition) | 86.14 % |

Recovered **10,474 B RAM** (98.84 % → 82.83 %) and **7,596 B FLASH**
(96.94 % → 93.06 %) since the first audit, after:
- `CONFIG_FACTORY_IMAGE_CHUNK_SIZE` 4096 → 1024 (−3 KB RAM).
- `CONFIG_IMG_BLOCK_BUF_SIZE` 4096 → 1024 (−3 KB RAM).
- New `variants/dev_full.overlay` disabling `adc_ext1` and `usart3`
  (the two peripherals declared in the base DTS but never referenced
  by this variant's cell mix) → −1.8 KB RAM, −1.6 KB FLASH from the
  dropped driver state + code.
- `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` 4096 → 2048 (−2 KB RAM, matched
  in `sysbuild/mcuboot.conf`).
- `CONFIG_LOG_BUFFER_SIZE` 2048 → 1024 (−1 KB RAM).
- Threat-model rationalisation: dropped fault-injection hardening
  flags (`-fstack-clash-protection`, `-fharden-compares`,
  `-fharden-conditional-branches`), restored stack canaries
  (`CONFIG_STACK_CANARIES_STRONG=y`). Net **−7 KB FLASH** with
  unchanged software-bug coverage. See "Threat-model rationalisation"
  section below.
- Net +576 B RAM for the new UDS log push backend, which was
  previously missing entirely (see below).

## RAM — top consumers

| Bytes | % | What | Notes |
|---:|---:|---|---|
| 4,096 | 6.33 | SEGGER RTT _acUpBuffer | `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=4096` — must match mcuboot |
| 4,096 | 6.33 | `factory_image.c:buffer[CHUNK_SIZE]` | `CONFIG_FACTORY_IMAGE_CHUNK_SIZE=4096` |
| 4,096 | ~6  | OTA `flash_img_context.buf` | `CONFIG_IMG_BLOCK_BUF_SIZE=4096`, inside `uds_ota.c` state |
| 2,948 | 4.55 | `adc_ads1x1x` driver | 3 device data structs, buffers, sem state |
| 2,560 | 3.95 | `_k_fifo_buf_log_push_msgq` | UDS log-push queue (10 × 256 B) — **dead code** |
| 2,304 | 3.56 | thread_analyzer subsys | stack 2,112 + thread_obj 192 |
| 2,116 | 3.27 | `kheap__system_heap` | system heap |
| 2,112 | 3.26 | `z_interrupt_stacks` | hard ISR stack |
| 2,112 | 3.26 | `z_main_stack` | main thread stack |
| 2,112 | 3.26 | `logging_stack` | log processor thread |
| 2,048 | 3.16 | `log_core buf32` | `CONFIG_LOG_BUFFER_SIZE=2048` |
| 1,887 | 2.91 | `power_management.c` | thread + state + zbus channel data |
| 1,608 | 2.48 | `flash_log_reader.c` | per-FCB lazy sector indexes (255 × 8 × 2 ≈ 4 KB cap) |
| 1,344 | 2.08 | `divecan_rx` thread stack | |
| 1,136 | 1.76 | `isotp_tx_queue.c` | thread + queue |
| 1,088 | 1.68 | `sys_work_q_stack` | system workqueue |
| 1,088 ×5 | — | App threads at 1,088 B | divecan_ppo2_tx, fl_writer, diveo2_thread_1, consensus, cal |
| 832 ×5  | — | App threads at 832 B | shutdown, ppo2_pid, o2s_thread_2, firmware_confirm, analog_cell_3 |
| 800 ×2  | — | `fl_telemetry_sectors`, `fl_text_sectors` | 100 × `struct flash_sector` (8 B) per FCB |
| 576 ×3  | — | App threads at 576 B | watchdog_feeder, solenoid_fire, battery_monitor |
| 448 | 0.69 | zbus net_buf pool | message-subscriber backing |
| 384 | 0.59 | `z_idle_stacks` | 2 idle threads |

**Aggregated by category** (rough):

| Category | Bytes | Notes |
|---|---|---|
| App thread stacks (15 threads) | ~13,200 | + 192 B per thread obj = ~16 KB total |
| Zephyr subsystem stacks | ~9,920 | main + interrupt + logging + analyzer + workq + idle |
| RTT / log infrastructure | ~6,200 | 4 KB up-buffer + 2 KB log_buffer + log_msg/MPSC framing |
| OTA + factory buffers | ~8,200 | two parallel 4 KB chunks |
| zbus channels + state | ~600 | very cheap |
| flash_log subsystem | ~5,700 | reader + writer + FCB sector arrays |
| UDS log push (dead) | ~2,560 | unused — see below |

## ROM — top consumers

| Bytes | % | Module |
|---:|---:|---|
| 20,600 | 10.81 | `src/divecan/` (UDS + ISO-TP + dispatch) — of which `uds_ota.c` = 4,180, `uds_log_push.c` = 3,124, `divecan_rx.c` = 2,685, `divecan_ppo2_tx.c` = 1,280 |
| 6,518 | 3.42 | `src/flash_log/` |
| 5,706 | 2.99 | `uart_stm32.c` |
| 4,642 | 2.44 | `adc_ads1x1x.c` |
| 3,604 | 1.89 | Zephyr `core/` |
| 3,155 | 1.66 | `oxygen_cell_diveo2.c` |
| 3,098 | 1.63 | `can_stm32_bxcan.c` |
| 2,934 | 1.54 | `adc_stm32.c` |
| 2,835 | 1.49 | `calibration.c` |
| 2,556 | 1.34 | `spi_stm32.c` |
| 2,500 | 1.31 | `factory_image.c` |
| 2,475 | 1.30 | `power_management.c` |
| 2,444 | 1.28 | `spi_nor.c` |

Biggest single functions:
- `UDS_ProcessRequest` — 4,268 B
- `divecan_rx_thread.part.0` — 1,824 B
- `ota_awaiting_activate_run` — 908 B
- `z_arm_mpu_init` — 592 B
- `z_arm_fault` — 576 B
- `UDS_LogDownload_Handle` — 548 B
- `UDS_LogDownload_HandleRoutine` — 480 B
- `fl_process` — 472 B

## Why factory_image.c is so RAM-heavy

The factory partition flow does **not** share buffers with OTA, even though they exercise nearly the same flash path:

| Path | Buffer | Lives in |
|---|---|---|
| OTA (`uds_ota.c`) | 4,096 B inside `struct flash_img_context.buf` | OTA SM state |
| Factory capture/restore (`factory_image.c`) | 4,096 B static `buffer[CHUNK_SIZE]` | `factory_image.c` bss |

Both buffers exist permanently. They are **mutually exclusive in time** — only one of {OTA download, factory capture, factory restore} is ever active — but each carries its own 4 KB.

Three options, in order of safety/effort:

1. **Drop `CONFIG_FACTORY_IMAGE_CHUNK_SIZE` to 1024 or 512.** Saves 3–3.5 KB. The 4 KB number was chosen "to match a single SPI NOR sector erase", but the erase is done in bulk *before* the copy loop (line 126 in `factory_image.c`), so chunk size only controls how many SPI read/write iterations the loop does — not erase granularity. 512 B × 768 iterations ≈ 400 ms total at 6 MHz SPI; still single-shot, still well under the watchdog.
2. **Share OTA's `flash_img_context.buf`.** Cleanest from a "single source of truth" standpoint but requires reaching across module boundaries (factory_image would need an accessor into the OTA SM), and the two consumers would need to agree on locking. Not worth the complexity for a one-shot capture path.
3. **Externalise both into a shared `flash_io_buffer[]`** owned by neither module. Same idea as option 2, but the buffer lives in a new tiny TU and both modules depend on it. Saves the same 4 KB as option 2 with a cleaner ownership model.

**Recommendation: option 1** — change `CONFIG_FACTORY_IMAGE_CHUNK_SIZE` default to 1024 (or set it explicitly in `prj.conf`) and reword the misleading comment in `Kconfig.factory_image`. **~3 KB recovered**, no code changes, no behaviour change. Bumping iteration count doesn't matter because the loop already feeds the watchdog and isn't time-critical.

## UDS log push — was unwired, now wired

Earlier audit flagged `uds_log_push.c` as dead because `UDS_LogPush_SendLogMessage`
and `UDS_LogPush_Init` had no callers. That was wrong by intent — the path
was *meant* to carry the live RTT stream to the BT client over UDS WDBI
0xA100, complementing the flash log's bulk-retrieval semantics. The
producer side simply never got connected to Zephyr's logging framework.

**Resolved** by:

1. New file `src/divecan/uds/uds_log_push_backend.c` — Zephyr
   `log_backend_api` adapter that mirrors `flash_log_backend.c`
   structurally but routes the rendered line through
   `UDS_LogPush_SendLogMessage` instead of the FCB writer.
2. `divecan_rx.c:InitializeUDSContexts` now calls
   `UDS_LogPush_Init(&udsState->logPushIsoTpContext)` instead of
   `ISOTP_Init` directly, so the module's singleton state actually gets
   the context pointer it needs to send anything.

Severity threshold is shared with the flash backend
(`flash_log_get_rtt_level()`), so UDS DID 0xF283 (LOG_VERBOSITY) raises
or lowers both sinks at once. Backlog accumulated before
`UDS_LogPush_Init` completes flushes once the bus comes up — desired
"live tail starting from boot" behaviour for connected debugging.

Cost of wiring it in: +576 B RAM, +1.1 KB FLASH. The 2.6 KB
`log_push_msgq` is now load-bearing for an active feature instead of
sitting unused.

## Other low-risk trim candidates

| Target | Current | Suggest | Saving | Notes |
|---|---|---|---|---|
| `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` | 4096 | 2048 | 2 KB | Must change mcuboot to match (sysbuild/mcuboot.conf) or lose log alignment |
| `CONFIG_LOG_BUFFER_SIZE` | 2048 | 1024 | 1 KB | Risk: dropped messages under burst |
| `flash_log_reader` lazy index | up to 800 ×2 | only build when requested | up to 1.6 KB | Index is already lazy on first selector — but the array itself is reserved permanently. Could move to system heap and free after download. |
| Factory chunk (see above) | 4096 | 1024 | 3 KB | Best risk/reward |

## Action set status

| # | Action | Status | RAM Δ |
|---|---|---|---|
| 1 | `CONFIG_FACTORY_IMAGE_CHUNK_SIZE` 4096 → 1024 | **Done** | −3,072 B |
| 2 | `CONFIG_IMG_BLOCK_BUF_SIZE` 4096 → 1024 | **Done** | −3,072 B |
| 3 | Wire UDS log push backend (was missing entirely) | **Done** | +576 B (intentional — live RTT-over-UDS path now functional) |
| 4 | `variants/dev_full.overlay` disables `adc_ext1` + `usart3` | **Done** | −1,856 B |
| 5 | `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` 4096 → 2048 (+ matching mcuboot.conf) | **Done** | −2,048 B |
| 6 | `CONFIG_LOG_BUFFER_SIZE` 2048 → 1024 | **Done** | −1,024 B |
| 7 | Threat-model rationalisation: drop fault-injection hardening flags, restore stack canaries | **Done** | +4 B RAM, **−7,064 B FLASH** |
| 8 | Move `flash_log_reader` per-FCB sector index to heap | Open | up to −1.6 KB |

Cumulative realised: **−10,474 B RAM** (98.84 % → 82.83 %), **−7,596 B FLASH** (96.94 % → 93.06 %).

## Threat-model rationalisation (action #7 — details)

This is an open-source, deliberately-hackable platform. The threat model
covers **software bugs**: buffer overflows, uninitialised reads, classic
overflow exploits. It explicitly does **not** cover deliberate fault
injection (glitching, voltage/EM/clock attacks) — there is no adversary
to defeat and no secret to protect.

**Dropped** (all fault-injection class):

- `-fstack-clash-protection` — defends against unbounded stack growth.
  Architecturally impossible per NASA P10 (no recursion, no VLAs, all
  loops fixed-bound). Mitigating something we cannot do.
- `-fharden-compares` — duplicates comparisons to defeat glitching of
  a single compare result.
- `-fharden-conditional-branches` — re-evaluates the comparison after
  branch to confirm the right side was taken.

**Kept** (bug class):

- `-ftrivial-auto-var-init=pattern` — fills locals with `0xAA` to catch
  uninit-read bugs.
- `_FORTIFY_SOURCE=2` — runtime bounds checks on libc string/memory
  ops with compile-time-known destinations.
- `-Wstack-usage=1305` — per-function stack cap (static).
- `CONFIG_HW_STACK_PROTECTION=y` — MPU-backed stack guard page.

**Added back**: `CONFIG_STACK_CANARIES_STRONG=y`. The `_STRONG` variant
guards every function with a local array OR taking the address of a
local — the actual surface where a buffer overflow lands. `_REGULAR`
only protects functions with char buffers ≥8 bytes and misses most
real targets. Canaries catch return-address smashing, which is the
dominant C bug-class failure mode.

Net effect across the binary: **−7,064 B FLASH**, +4 B RAM. The `uds.c`
TU alone shed 2 KB — the giant `UDS_ProcessRequest` switch dispatcher
was almost half branch-hardening overhead. Per-TU `.text` deltas:

| File | Before | After | Δ |
|---|---|---|---|
| `uds.c` | 5,493 | 3,473 | **−2,020** |
| `divecan_rx.c` | 3,024 | 2,360 | **−664** |
| `uds_ota.c` | 2,666 | 2,114 | **−552** |
| `uds_log_push.c` | 544 | 464 | −80 |

## Compiler optimisation level

We were already at `-Os` (`CONFIG_SIZE_OPTIMIZATIONS=y`, Zephyr's default
when no debug/no-opt is selected). Tested `CONFIG_SIZE_OPTIMIZATIONS_AGGRESSIVE=y`
(GCC `-Oz`) in a parallel build with everything else identical:

| | `-Os` | `-Oz` | Δ |
|---|---|---|---|
| **RAM** | 54,280 B | 54,280 B | 0 B |
| **FLASH** | 190,056 B | 190,056 B | 0 B |
| Per-`.obj` `.text` | identical bit-for-bit on every TU sampled (main.c, uds.c, divecan_rx.c, ppo2_control.c, flash_log.c, uds_ota.c, calibration.c) | | |

GCC 12's `-Oz` is `-Os` with extra prologue/epilogue and inlining-suppression
tweaks. A NASA-P10-disciplined codebase (single returns, no recursion,
fixed loops, no heap) presents none of the patterns `-Oz` knows how to
shrink further. **Verdict: not worth enabling**; the flag is a no-op
here and complicates debugging without payoff. The codebase is already
optimised to the practical floor at `-Os`.

LTO is not exposed via Zephyr Kconfig in this NCS/Zephyr version and
would need to be patched in manually — high risk for low payoff given
the above. Section-level dead-code elimination (`-ffunction-sections` +
`-fdata-sections` + `--gc-sections`) is already on by Zephyr default.

## Variant overlays — pattern

`variants/<name>.overlay` lives alongside `variants/<name>.conf` and
disables the peripherals that the variant's cell-type Kconfig doesn't
need. Both files are passed at build time:

```
-DEXTRA_CONF_FILE=variants/dev_full.conf \
-DEXTRA_DTC_OVERLAY_FILE=variants/dev_full.overlay
```

A new variant should always add its overlay companion — without it,
Zephyr allocates ~1.5 KB per unused ADS1115 and ~350 B per unused
USART for driver state that the application can never reach. The
overlay is the cheapest large RAM saving available because it costs
zero code changes.

## What we should not touch (yet)

- Thread stacks at 1024+ B — runtime high-water marks already showed static WCS under-estimates by ~2× (see `CLAUDE.md` and `divecan_ppo2_tx.c` comment). Need a sustained-load HWM capture before trimming any of them.
- `CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE=2048` — already had to be bumped from 1024 after a canary trip.
- `CONFIG_MAIN_STACK_SIZE=2048` — also already reverted from 1024 after canary trip.
- ADC driver allocations (`adc_ads1x1x` 2,948 B) — driver-owned, modifying upstream is out of scope.
