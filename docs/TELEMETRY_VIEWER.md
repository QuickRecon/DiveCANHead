# Telemetry Log Viewer

Browser viewer for downloaded flash-log telemetry: graph every decoded channel
over time, with solenoid fires, errors, reboots, dive markers and data gaps
overlaid on the same axis.

| Artefact | Path |
|----------|------|
| Viewer page | `DiveCAN_bt/examples/telemetry-viewer.html` |
| Channel model (what is plottable, units, axis groups) | `DiveCAN_bt/src/telemetry/TelemetryModel.js` |
| Byte stream → typed-array channels | `DiveCAN_bt/src/telemetry/TelemetryBuilder.js` |
| Viewport decimation + sample lookup | `DiveCAN_bt/src/telemetry/TelemetrySeries.js` |
| Discrete-event uPlot plugin | `DiveCAN_bt/src/telemetry/EventOverlay.js` |
| UI controller | `DiveCAN_bt/src/telemetry/TelemetryViewer.js` |
| Decode worker | `DiveCAN_bt/src/telemetry/telemetry-worker.js` |
| CSV → stream rebuild | `DiveCAN_bt/src/telemetry/CsvSource.js` |
| Record decoders (shared with the BT client) | `DiveCAN_bt/src/logs/LogParser.js` |
| Tests | `DiveCAN_bt/src/telemetry/TelemetryBuilder.test.js` |
| CLI companion | `Firmware/scripts/telemetry_log.py` |

Wire format reference: [`Firmware/docs/FLASH_LOG.md`](../Firmware/docs/FLASH_LOG.md)
and `Firmware/src/flash_log/flash_log_entries.h`.

## Running it

The page uses raw ES modules and a module worker, so it must be **served over
HTTP** — `file://` will not load it.

```bash
cd DiveCAN_bt
npm run dev            # python3 -m http.server 8000
# then open http://localhost:8000/examples/telemetry-viewer.html
```

`./deploy.sh` publishes it alongside `diagnostics.html` on the CDN host; both
`examples/` and `src/` are rsynced, so no extra step is needed.

Drop a file on the header target, or click to pick one:

* **`.bin`** — the raw stream from `LogDownloader` / `logToRawBin`. Preferred:
  it is the smallest input and the fastest to decode.
* **`.csv`** — the export from `logToCSV`. `payload_hex` is lossless, so the
  CSV is rebuilt into the same TLV stream and decoded by the same path rather
  than through a second parser.

Decoding runs in a Web Worker and the channel arrays are **transferred** (not
cloned) back to the page, so the UI never blocks and there is no second copy of
the data.

### Dependencies

[uPlot 1.6.32](https://github.com/leeoniya/uPlot) from jsDelivr, pinned with an
SRI hash, matching the convention in `diagnostics.html`. It was chosen over
Chart.js/Plotly because it renders this dataset at interactive framerates. No
build step, no bundler, no npm install for the viewer itself.

## Channels

Every numeric field of every telemetry record type is selectable. Record types
that carry a cell index are split into one series per observed cell, so cell 0
and cell 1 keep their own sample times instead of being interleaved.

| Group | Channels | Unit |
|-------|----------|------|
| Consensus (`0x10`) | Consensus PPO2, Setpoint, Confidence, Cell 0/1/2 PPO2 (voted), Cell 0/1/2 output, Cell 0/1/2 status, Cell 0/1/2 included | bar, mV, code |
| PID (`0x11`) | Solenoid duty, PID integral, Saturation count, Setpoint | 0-1, counts, bar |
| Atmospheric pressure (`0x14`) | Ambient pressure, **Depth** | mbar, m |
| Power (`0x15`) | VBUS, VCC, battery and CAN voltage; low-battery threshold; device current and derived power; sample ages; Poseidon battery percentage/freshness; low-battery state | V, mA, W, s, %, code |
| DiveO2 raw (`0x20`, per cell) | PPO2, Temperature, Error code, Phase, Intensity, Ambient light, Ambient pressure, Humidity, **Depth** | bar, °C, counts, mbar, %RH, m |
| O2S raw (`0x21`, per cell) | PPO2, Status | bar, code |
| Analog raw (`0x22`, per cell) | PPO2, Raw ADC, Cell output | bar, counts, mV |

Cell status codes are `0=OK 1=DEGRADED 2=FAIL 3=NEED_CAL` (`CellStatus_t`).

### Units — and two firmware field names that lie

The scale factors are the exported constants in `LogParser.js`. Two of them do
not match the field name in `fl_payload_cell_diveo2_t`; the constants, not the
names, are correct:

| Field | Name implies | Actually is | Evidence |
|-------|--------------|-------------|----------|
| `ppo2`, `consensus_ppo2`, `setpoint` | — | uint8 **centibar** (69 → 0.69 bar) | `ppo2_centibar_to_wire()` |
| `milli_array` / `millivolts` | — | uint16 in **0.01 mV** (4520 → 45.20 mV) | `Millivolts_t` |
| `temperature_dc` | deci-°C | **milli-°C** | `/10` gives 1728–3268 °C on real data; `/1000` gives 17.3–32.7 °C |
| `pressure_uhpa` | micro-hPa | **milli-hPa** (= milli-mbar) | `src/calibration.c`: `pressure_uhpa / 1000U` → `pressure_mbar` |
| `humidity_mrh` | — | milli-%RH | consistent with the other two ancillary fields |

If those field names are ever corrected in the firmware, update
`DIVEO2_TEMP_LSB_PER_DEGC` / `DIVEO2_PRESSURE_LSB_PER_MBAR` here only if the
*encoding* changes — the names alone are cosmetic.

### Derived depth

The dedicated `ATMOS_PRESSURE` record is the preferred ambient-pressure signal
and makes depth available on analog and O2S heads too. DiveO2
`pressure_uhpa` remains a second sensor-local pressure source. Both use the same
derived channel:

```
depth_m = (pressure_mbar - surface_mbar) / 100
```

The surface reference defaults to the **2nd percentile** of observed pressure
across the whole log — a percentile rather than the minimum so one dropout or a
pre-dive excursion cannot set the datum. Override it in the sidebar
(*Depth reference → Surface → Apply*) for a dive at altitude or a sensor with a
known offset; every depth series re-datums in place.

## Time axis

`ts_boot_us` restarts at zero on every reboot, and a wrapped ring starts
part-way through an epoch, so raw timestamps are not a usable axis.

The builder segments records at each `BOOT_MARKER` into **epochs** and lays them
end to end on a synthetic global axis separated by `EPOCH_GAP_S` (30 s). That
gives one monotonic axis across a multi-boot log without pretending the reboot
was instantaneous. The gap is hatched, so no trend is ever read across it.

* **Elapsed** (default) — `h:mm:ss` from the start of the global axis.
* **Absolute** — available when a dive marker supplies a `unix_timestamp`; that
  marker anchors the whole axis to wall-clock time.

The cursor readout always names the epoch and the boot-relative offset
(`boot 184 +1:12:33`), so a global time can be mapped back to a real uptime.

## Discrete-event overlay

| Layer | Where | Meaning |
|-------|-------|---------|
| Drop markers | Top strip (+ full-height hatch when few are in view) | The ingest queue overflowed and `count` records were lost just before this instant. **A gap, never interpolated.** |
| Reboot gaps | Full-height hatch | Synthetic time between epochs. |
| Boot / dive markers | Full-height rule + label | `BOOT <id>`, `DIVE START`, `DIVE END`. A boot whose marker carries a previous-boot crash record is called out in the summary panel. |
| Solenoid fires | Bottom band | Spans from open to close, paired from the start/end records. Blue = O2 inject, amber = setpoint-change flush. |
| Error events | Rug above the solenoid band | One column per pixel, coloured by the dominant `OP_ERR_*` code, opacity by density. |

Drop markers switch between the density strip and the full-height hatch at 40
visible markers: a bad stretch emits hundreds, and painting them all full-height
obscures the traces they exist to qualify.

Hovering reports every visible series' value **at full resolution** (a binary
search over the original samples, not the decimated draw data) plus any events
within a few pixels — with error codes rendered as `OP_ERR_*` names and their
descriptions, sourced from the same `OP_ERRORS` table as the error histogram.

## Interaction

| Gesture | Effect |
|---------|--------|
| Drag | Zoom to selection |
| Shift-drag / middle-drag | Pan |
| Wheel | Zoom about the cursor |
| Double-click / *Reset zoom* | Full extent |

## How it stays fast

The viewer owns the x range; uPlot never does its own zoom. On every range
change each selected channel is reduced onto a **shared uniform grid** sized
from the plot's pixel width, keeping each bucket's min and max:

* Sharing the grid is what lets channels with different sample times (consensus,
  PID, per-cell raw) occupy one uPlot x array.
* Because bucket width comes from the pixel width, the time quantisation is
  always sub-pixel — the decimated line is visually identical to the full series
  at every zoom level, and spikes survive because min and max are both kept.
* Empty runs wider than ~4× the channel's median sample interval stay `null`, so
  dropouts render as breaks. Narrower holes (which only appear when zoomed past
  the sample rate) are bridged so the line stays continuous.

Draw cost is therefore bounded by plot width, not by log length.

## Axes

Series are grouped onto one scale per unit (PPO2/bar, duty, depth, pressure,
temperature, humidity, cell mV, supply V/mA/W, battery %, sample age, raw counts,
status codes), alternating left and right so the gutters stay balanced. Depth is
inverted. Any series can be reassigned to a different axis from the *Series*
panel. Past four axis groups the text labels are dropped and the tick colour
carries the association instead.

## CLI companion

`Firmware/scripts/telemetry_log.py` is a deliberately **independent**
implementation of the same decode — running `validate` cross-checks the
JavaScript against it and against the `summary` column the download tool wrote.
Standard library only.

```bash
# Counts, boot epochs, per-channel statistics, errors, gaps, ordering check
/usr/bin/python3 Firmware/scripts/telemetry_log.py summary log.bin

# Diff a .bin against its sibling .csv (payload_hex, timestamps, decoded fields)
/usr/bin/python3 Firmware/scripts/telemetry_log.py validate log.bin log.csv

# Rebuild a .bin from a CSV-only log so the viewer's fast path applies
/usr/bin/python3 Firmware/scripts/telemetry_log.py tobin log.csv -o log.bin
```

`validate` compares field-by-field rather than as whole strings, so a CSV
exported before a decoder gained a field (for example `BOOT_MARKER.prevCrash`)
still passes, with the addition reported informationally.

## Maintenance

When a record type is added to `FlashLogType_t` or a payload struct changes in
`flash_log_entries.h`:

1. Add the type code and name to `DiveCAN_bt/src/uds/constants.js`.
2. Add a `decode*` function in `DiveCAN_bt/src/logs/LogParser.js`, wire it into
   `decodeRecord`, and add its payload length constant. **The length must be the
   packed size** — `fl_payload_cell_analog_t` is 8 bytes, not 10, because
   `__packed` removes the padding a natural-alignment reading would assume.
3. Declare the plottable fields in `TelemetryModel.js` (`payloadLen`, byte
   offsets, reader, scale, axis group, description). Set `perCell: true` if the
   payload starts with a cell index.
4. Add a payload builder to `DiveCAN_bt/tests/fixtures/log-streams.js` and cases
   to `TelemetryBuilder.test.js`.
5. Mirror the decoder in `Firmware/scripts/telemetry_log.py` so `validate` keeps
   cross-checking two independent implementations.

Error-code names come from `OP_ERRORS` in
`DiveCAN_bt/src/errors/ErrorHistogram.js`, which must stay in exact `OpError_t`
order — the array index *is* the code. `Firmware/scripts/telemetry_log.py` keeps
its own copy in `OP_ERROR_NAMES`; update both when `include/errors.h` gains a
code.
