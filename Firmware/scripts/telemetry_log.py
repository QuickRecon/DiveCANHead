#!/usr/bin/env python3
"""Flash-log telemetry CLI: inspect, validate, and repack downloaded dive logs.

Companion to the browser viewer at ``DiveCAN_bt/examples/telemetry-viewer.html``.
This is a deliberately independent implementation of the same TLV decode, so
running ``validate`` cross-checks the JavaScript decoders against both this
decoder and the ``summary`` column that the download tool wrote into the CSV.

Wire format reference:
  * Firmware/src/flash_log/flash_log_entries.h  -- packed payload structs
  * Firmware/docs/FLASH_LOG.md                  -- record table, BATCH framing
  * Firmware/include/flash_log.h                -- FlashLogType_t, SOL_FIRE_EVT

Subcommands
-----------
summary   Record counts, boot epochs, per-channel statistics, anomaly report.
validate  Re-decode a .bin and diff against the sibling .csv's summary column.
tobin     Rebuild a .bin (DCLG stream) from a .csv so the viewer's fast path
          works on a log that only survives in CSV form.

Only the standard library is required; numpy is used when available to speed up
the statistics pass.
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator

# ---- Wire constants (mirror Firmware/include/flash_log.h) -------------------

DCLG_MAGIC = b"DLCG"          # "DCLG" as stored little-endian
DCLG_HEADER_LEN = 16
ENTRY_HDR_LEN = 12

FL_BOOT_MARKER = 0x01
FL_DIVE_START = 0x02
FL_DIVE_END = 0x03
FL_CAN_RX = 0x04
FL_CAN_TX = 0x05
FL_CONSENSUS = 0x10
FL_PID_SNAPSHOT = 0x11
FL_SOLENOID_FIRE = 0x12
FL_SOLENOID_CURRENT = 0x13
FL_CELL_RAW_DIVEO2 = 0x20
FL_CELL_RAW_O2S = 0x21
FL_CELL_RAW_ANALOG = 0x22
FL_ERROR_EVENT = 0x30
FL_LOG_TEXT = 0x40
FL_BATCH = 0xFD
FL_DROP_MARKER = 0xFE
FL_END_OF_STREAM = 0xFF

TYPE_NAMES = {
    FL_BOOT_MARKER: "Boot Marker",
    FL_DIVE_START: "Dive Start",
    FL_DIVE_END: "Dive End",
    FL_CAN_RX: "CAN RX",
    FL_CAN_TX: "CAN TX",
    FL_CONSENSUS: "Consensus",
    FL_PID_SNAPSHOT: "PID Snapshot",
    FL_SOLENOID_FIRE: "Solenoid Fire",
    FL_SOLENOID_CURRENT: "Solenoid Current",
    FL_CELL_RAW_DIVEO2: "Cell Raw (DiveO2)",
    FL_CELL_RAW_O2S: "Cell Raw (O2S)",
    FL_CELL_RAW_ANALOG: "Cell Raw (Analog)",
    FL_ERROR_EVENT: "Error Event",
    FL_LOG_TEXT: "Log Text",
    FL_BATCH: "Batch",
    FL_DROP_MARKER: "Drop Marker",
    FL_END_OF_STREAM: "End Of Stream",
}

SOL_FIRE_KIND_NAMES = {
    0: "Inject Start",
    1: "Inject End",
    2: "Flush Start",
    3: "Flush End",
}

CELL_STATUS_NAMES = {0: "OK", 1: "DEGRADED", 2: "FAIL", 3: "NEED_CAL"}

CRASH_MAGIC = 0xDEADC0DE

# ``OpError_t`` in Firmware/include/errors.h. Index IS the code; append only.
OP_ERROR_NAMES = [
    "NONE", "I2C_BUS", "UART", "CAN_TX", "CAN_OVERFLOW", "INT_ADC", "EXT_ADC",
    "FLASH", "CELL_OVERRANGE", "CELL_FAILURE", "INVALID_CELL", "MATH",
    "CAL_METHOD", "CAL_MISMATCH", "VBUS_UNDERVOLT", "VCC_UNDERVOLT",
    "SOLENOID_DISABLED", "ISOTP_TIMEOUT", "ISOTP_SEQ", "ISOTP_OVERFLOW",
    "ISOTP_STATE", "UDS_NRC", "UDS_TOO_FULL", "UDS_INVALID", "CONFIG",
    "TIMEOUT", "OUT_OF_DATE", "QUEUE", "NULL_PTR", "LOGGING", "LOG_TRUNCATED",
    "UNREACHABLE", "UNKNOWN", "POST_FAIL", "GPIO", "DEVICE_NOT_READY",
    "SOLENOID_OVERCURRENT", "SOLENOID_UNDERCURRENT",
]

# ---- Unit scales -----------------------------------------------------------
#
# DiveO2 #DRAW physical-unit scales from the vendor datasheet.

PPO2_CBAR_PER_BAR = 100.0
MILLIVOLT_LSB_PER_MV = 100.0
DIVEO2_TEMP_LSB_PER_DEGC = 1000.0
DIVEO2_PHASE_LSB_PER_DEG = 1000.0
DIVEO2_UV_PER_MV = 1000.0
DIVEO2_UBAR_PER_MBAR = 1000.0
DIVEO2_HUMIDITY_LSB_PER_PCT = 1000.0
MBAR_PER_METRE = 100.0
US_PER_S = 1_000_000.0

#: Gap inserted between boot epochs on the synthetic global axis (seconds).
#: Must match ``EPOCH_GAP_S`` in DiveCAN_bt/src/telemetry/TelemetryBuilder.js.
EPOCH_GAP_S = 30.0

#: Percentile of ambient pressure treated as the surface datum.
SURFACE_PERCENTILE = 0.02


# ---- Record walking --------------------------------------------------------

@dataclass
class Record:
    """One flattened TLV record."""

    type: int
    flags: int
    ts_us: int
    payload: bytes


_HDR = struct.Struct("<BBHQ")


def iter_records(data: bytes) -> Iterator[Record]:
    """Yield every TLV record in a downloaded stream, flattening BATCH containers.

    Stops at END_OF_STREAM or a truncated tail, matching the firmware reader
    and ``LogParser.parseLogStream`` in the browser client.
    """
    start = DCLG_HEADER_LEN if data[:4] == DCLG_MAGIC else 0
    yield from _walk(data, start, len(data))


def _walk(data: bytes, start: int, end: int) -> Iterator[Record]:
    i = start
    while i + ENTRY_HDR_LEN <= end:
        rtype, flags, length, ts_us = _HDR.unpack_from(data, i)
        body = i + ENTRY_HDR_LEN
        stop = body + length
        if stop > end:
            return  # truncated tail
        if rtype == FL_END_OF_STREAM:
            return
        if rtype == FL_BATCH:
            yield from _walk(data, body, stop)
        else:
            yield Record(rtype, flags, ts_us, data[body:stop])
        i = stop


# ---- Payload decoders ------------------------------------------------------

_S_DIVE = struct.Struct("<HI")
_S_CONSENSUS = struct.Struct("<BBBBHHHHBB")
_S_PID = struct.Struct("<fHfB")
_S_SOL_FIRE = struct.Struct("<BII")
_S_SOL_CURRENT = struct.Struct("<BBiii")
_S_DIVEO2 = struct.Struct("<BBiIiiiii")
_S_O2S = struct.Struct("<BBB")
_S_ANALOG = struct.Struct("<BBiH")
_S_ERROR = struct.Struct("<II")
_S_DROP = struct.Struct("<IB")
_S_BOOT = struct.Struct("<I16sIIIII")

CONSENSUS_STATUS_SHIFTS = (0, 3, 6)
CONSENSUS_INCLUDE_SHIFTS = (2, 5, 8)


def decode_boot_marker(p: bytes) -> dict | None:
    if len(p) < 24:
        return None
    if len(p) >= _S_BOOT.size:
        boot_id, ver, cause, magic, reason, pc, lr = _S_BOOT.unpack_from(p)
        crash = None
        if magic == CRASH_MAGIC:
            crash = {"magic": magic, "reason": reason, "pc": pc, "lr": lr}
    else:
        boot_id, ver, cause = struct.unpack_from("<I16sI", p)
        crash = None
    return {
        "bootId": boot_id,
        "fwVersion": ver.split(b"\x00")[0].decode("ascii", "replace"),
        "resetCause": cause,
        "prevCrash": crash,
    }


def decode_dive_marker(p: bytes) -> dict | None:
    if len(p) < _S_DIVE.size:
        return None
    number, unix_ts = _S_DIVE.unpack_from(p)
    return {"diveNumber": number, "unixTimestamp": unix_ts}


def decode_consensus(p: bytes) -> dict | None:
    if len(p) < _S_CONSENSUS.size:
        return None
    (cons, a0, a1, a2, m0, m1, m2, packed, conf, sp) = _S_CONSENSUS.unpack_from(p)
    return {
        "consensusPpo2": cons,
        "ppo2": [a0, a1, a2],
        "millivolts": [m0, m1, m2],
        "statusPacked": packed,
        "confidence": conf,
        "setpoint": sp,
    }


def consensus_cells(packed: int) -> list[dict]:
    """Unpack the CONSENSUS status/include bitfield into per-cell entries."""
    return [
        {
            "status": (packed >> s) & 0x03,
            "include": bool((packed >> i) & 0x01),
        }
        for s, i in zip(CONSENSUS_STATUS_SHIFTS, CONSENSUS_INCLUDE_SHIFTS)
    ]


def decode_pid(p: bytes) -> dict | None:
    if len(p) < _S_PID.size:
        return None
    integral, sat, duty, sp = _S_PID.unpack_from(p)
    return {
        "integral": integral,
        "saturationCount": sat,
        "duty": duty,
        "setpoint": sp,
    }


def decode_solenoid_fire(p: bytes) -> dict | None:
    if len(p) < _S_SOL_FIRE.size:
        return None
    kind, on_us, off_us = _S_SOL_FIRE.unpack_from(p)
    return {"kind": kind, "requestedOnUs": on_us, "offUs": off_us}


def decode_solenoid_current(p: bytes) -> dict | None:
    if len(p) < _S_SOL_CURRENT.size:
        return None
    role, cls, base, fire, delta = _S_SOL_CURRENT.unpack_from(p)
    return {
        "role": role,
        "classification": cls,
        "baselineUa": base,
        "fireUa": fire,
        "deltaUa": delta,
    }


def decode_cell_diveo2(p: bytes) -> dict | None:
    if len(p) < _S_DIVEO2.size:
        return None
    (idx, ppo2, temp_mc, err, phase_mdeg, signal_intensity_uv,
     ambient_light_uv, ambient_pressure_ubar,
     housing_humidity_mpercent_rh) = _S_DIVEO2.unpack_from(p)
    return {
        "cellIndex": idx,
        "ppo2": ppo2,
        "temperatureMc": temp_mc,
        "errCode": err,
        "phaseMdeg": phase_mdeg,
        "signalIntensityUv": signal_intensity_uv,
        "ambientLightUv": ambient_light_uv,
        "ambientPressureUbar": ambient_pressure_ubar,
        "housingHumidityMpercentRh": housing_humidity_mpercent_rh,
        "ppo2Bar": ppo2 / PPO2_CBAR_PER_BAR,
        "temperatureC": temp_mc / DIVEO2_TEMP_LSB_PER_DEGC,
        "phaseDeg": phase_mdeg / DIVEO2_PHASE_LSB_PER_DEG,
        "signalIntensityMv": signal_intensity_uv / DIVEO2_UV_PER_MV,
        "ambientLightMv": ambient_light_uv / DIVEO2_UV_PER_MV,
        "ambientPressureMbar": ambient_pressure_ubar / DIVEO2_UBAR_PER_MBAR,
        "housingHumidityPctRh":
            housing_humidity_mpercent_rh / DIVEO2_HUMIDITY_LSB_PER_PCT,
    }


def decode_cell_o2s(p: bytes) -> dict | None:
    if len(p) < _S_O2S.size:
        return None
    idx, ppo2, status = _S_O2S.unpack_from(p)
    return {
        "cellIndex": idx,
        "ppo2": ppo2,
        "status": status,
        "ppo2Bar": ppo2 / PPO2_CBAR_PER_BAR,
    }


def decode_cell_analog(p: bytes) -> dict | None:
    if len(p) < _S_ANALOG.size:
        return None
    idx, ppo2, adc, mv = _S_ANALOG.unpack_from(p)
    return {
        "cellIndex": idx,
        "ppo2": ppo2,
        "rawAdc": adc,
        "millivolts": mv,
        "ppo2Bar": ppo2 / PPO2_CBAR_PER_BAR,
        "mv": mv / MILLIVOLT_LSB_PER_MV,
    }


def decode_error(p: bytes) -> dict | None:
    if len(p) < _S_ERROR.size:
        return None
    code, detail = _S_ERROR.unpack_from(p)
    return {"code": code, "detail": detail}


def decode_drop(p: bytes) -> dict | None:
    if len(p) < _S_DROP.size:
        return None
    count, last = _S_DROP.unpack_from(p)
    return {"count": count, "lastDroppedType": last}


def decode_can_frame(p: bytes) -> dict | None:
    if len(p) < 13:
        return None
    return {"id": struct.unpack_from("<I", p)[0], "dlc": p[4], "data": list(p[5:13])}


def decode_log_text(p: bytes) -> dict | None:
    if len(p) < 3:
        return None
    return {
        "level": p[0],
        "moduleId": struct.unpack_from("<H", p, 1)[0],
        "text": p[3:].decode("ascii", "replace"),
    }


DECODERS: dict[int, Callable[[bytes], dict | None]] = {
    FL_BOOT_MARKER: decode_boot_marker,
    FL_DIVE_START: decode_dive_marker,
    FL_DIVE_END: decode_dive_marker,
    FL_CAN_RX: decode_can_frame,
    FL_CAN_TX: decode_can_frame,
    FL_CONSENSUS: decode_consensus,
    FL_PID_SNAPSHOT: decode_pid,
    FL_SOLENOID_FIRE: decode_solenoid_fire,
    FL_SOLENOID_CURRENT: decode_solenoid_current,
    FL_CELL_RAW_DIVEO2: decode_cell_diveo2,
    FL_CELL_RAW_O2S: decode_cell_o2s,
    FL_CELL_RAW_ANALOG: decode_cell_analog,
    FL_ERROR_EVENT: decode_error,
    FL_DROP_MARKER: decode_drop,
    FL_LOG_TEXT: decode_log_text,
}


def decode_record(rec: Record) -> dict | None:
    """Decode a record's payload according to its type, or None if unhandled."""
    fn = DECODERS.get(rec.type)
    return fn(rec.payload) if fn else None


def error_name(code: int) -> str:
    """Render an OpError_t code as its enum name."""
    if 0 <= code < len(OP_ERROR_NAMES):
        return OP_ERROR_NAMES[code]
    return f"UNKNOWN({code})"


def format_elapsed(seconds: float) -> str:
    """Format seconds as h:mm:ss."""
    total = int(abs(seconds))
    sign = "-" if seconds < 0 else ""
    return f"{sign}{total // 3600}:{(total % 3600) // 60:02d}:{total % 60:02d}"


# ---- Epoch segmentation ----------------------------------------------------

@dataclass
class Epoch:
    """A run of records sharing one boot's ``ts_boot_us`` origin."""

    index: int
    min_us: int = 0
    max_us: int = 0
    boot: dict | None = None
    count: int = 0
    types: Counter = field(default_factory=Counter)
    start_s: float = 0.0
    offset_s: float = 0.0

    @property
    def span_s(self) -> float:
        return (self.max_us - self.min_us) / US_PER_S


def segment_epochs(records: list[Record]) -> list[Epoch]:
    """Split a record list at BOOT_MARKERs and lay the epochs end to end.

    ``ts_boot_us`` restarts at zero on every reboot, and a wrapped ring starts
    part-way through an epoch, so a single monotonic axis only exists after
    each epoch is given its own offset.
    """
    epochs: list[Epoch] = []
    current: Epoch | None = None
    for rec in records:
        if rec.type == FL_BOOT_MARKER or current is None:
            if current is not None and current.count > 0:
                current = None
            if current is None:
                current = Epoch(index=len(epochs), min_us=rec.ts_us, max_us=rec.ts_us)
                epochs.append(current)
        if rec.type == FL_BOOT_MARKER:
            current.boot = decode_boot_marker(rec.payload)
        current.min_us = min(current.min_us, rec.ts_us)
        current.max_us = max(current.max_us, rec.ts_us)
        current.count += 1
        current.types[rec.type] += 1

    cursor = 0.0
    for e in epochs:
        e.start_s = cursor
        e.offset_s = cursor - e.min_us / US_PER_S
        cursor += e.span_s + EPOCH_GAP_S
    return epochs


def global_times(records: list[Record], epochs: list[Epoch]) -> list[float]:
    """Map every record onto the concatenated global seconds axis."""
    out: list[float] = []
    idx = 0
    seen = False
    for rec in records:
        if rec.type == FL_BOOT_MARKER and seen:
            idx += 1
        seen = True
        out.append(rec.ts_us / US_PER_S + epochs[min(idx, len(epochs) - 1)].offset_s)
    return out


# ---- summary ---------------------------------------------------------------

def _percentile(sorted_values: list[float], frac: float) -> float:
    if not sorted_values:
        return 0.0
    return sorted_values[min(len(sorted_values) - 1, int(len(sorted_values) * frac))]


def _stats(values: list[float]) -> tuple[float, float, float]:
    return min(values), max(values), sum(values) / len(values)


def cmd_summary(args: argparse.Namespace) -> int:
    data = Path(args.file).read_bytes()
    records = list(iter_records(data))
    epochs = segment_epochs(records)
    times = global_times(records, epochs)

    print(f"file          {args.file}")
    print(f"size          {len(data) / 1e6:.1f} MB")
    print(f"records       {len(records)}")
    duration = (epochs[-1].start_s + epochs[-1].span_s) if epochs else 0.0
    print(f"global span   {format_elapsed(duration)}  ({len(epochs)} boot epoch(s))")

    print("\nrecord types")
    counts = Counter(r.type for r in records)
    for rtype, n in counts.most_common():
        print(f"  0x{rtype:02x} {TYPE_NAMES.get(rtype, '?'):<20} {n:>8}")

    print("\nboot epochs")
    for e in epochs:
        boot = e.boot
        ident = "(ring tail — no boot marker)"
        if boot:
            ident = f"boot_id={boot['bootId']} fw={boot['fwVersion']} reset_cause=0x{boot['resetCause']:x}"
            if boot["prevCrash"]:
                c = boot["prevCrash"]
                ident += (f"  PREV CRASH reason={c['reason']} "
                          f"pc=0x{c['pc']:08x} lr=0x{c['lr']:08x}")
        print(f"  #{e.index} global {format_elapsed(e.start_s)}"
              f"..{format_elapsed(e.start_s + e.span_s)}"
              f"  boot-rel {e.min_us / US_PER_S:.0f}..{e.max_us / US_PER_S:.0f}s"
              f"  n={e.count}")
        print(f"       {ident}")

    # ---- markers
    print("\nmarkers")
    for rec, t in zip(records, times):
        if rec.type in (FL_DIVE_START, FL_DIVE_END):
            d = decode_dive_marker(rec.payload)
            kind = "DIVE_START" if rec.type == FL_DIVE_START else "DIVE_END"
            print(f"  {format_elapsed(t):>9}  {kind:<10} dive={d['diveNumber']} "
                  f"unix={d['unixTimestamp']}")

    # ---- per-channel statistics
    print("\nchannel statistics")
    diveo2: dict[int, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    consensus: dict[str, list[float]] = defaultdict(list)
    pid: dict[str, list[float]] = defaultdict(list)

    for rec in records:
        if rec.type == FL_CELL_RAW_DIVEO2:
            d = decode_cell_diveo2(rec.payload)
            bucket = diveo2[d["cellIndex"]]
            bucket["ppo2 (bar)"].append(d["ppo2Bar"])
            bucket["temperature (degC)"].append(d["temperatureC"])
            bucket["phase (deg)"].append(d["phaseDeg"])
            bucket["signal intensity (mV)"].append(d["signalIntensityMv"])
            bucket["ambient light (mV)"].append(d["ambientLightMv"])
            bucket["backside pressure (mbar)"].append(
                d["ambientPressureMbar"])
            bucket["housing humidity (%RH)"].append(
                d["housingHumidityPctRh"])
            bucket["errCode"].append(float(d["errCode"]))
        elif rec.type == FL_CONSENSUS:
            d = decode_consensus(rec.payload)
            consensus["consensusPpo2 (bar)"].append(d["consensusPpo2"] / PPO2_CBAR_PER_BAR)
            consensus["setpoint (bar)"].append(d["setpoint"] / PPO2_CBAR_PER_BAR)
            consensus["confidence"].append(float(d["confidence"]))
        elif rec.type == FL_PID_SNAPSHOT:
            d = decode_pid(rec.payload)
            pid["duty"].append(d["duty"])
            pid["integral"].append(d["integral"])
            pid["saturationCount"].append(float(d["saturationCount"]))

    def dump(title: str, table: dict[str, list[float]]) -> None:
        if not table:
            return
        print(f"  {title}")
        for name, vals in table.items():
            lo, hi, mean = _stats(vals)
            print(f"    {name:<24} n={len(vals):>7} "
                  f"min={lo:>12.3f} max={hi:>12.3f} mean={mean:>12.3f}")

    dump("consensus", consensus)
    dump("pid", pid)
    for cell in sorted(diveo2):
        dump(f"diveo2 cell {cell}", diveo2[cell])

    # ---- derived depth
    all_press = sorted(
        v for cell in diveo2.values()
        for v in cell["backside pressure (mbar)"])
    if all_press:
        surface = _percentile(all_press, SURFACE_PERCENTILE)
        max_depth = (all_press[-1] - surface) / MBAR_PER_METRE
        print(f"\nderived depth  surface_ref={surface:.1f} mbar "
              f"(p{SURFACE_PERCENTILE * 100:g})  max_depth={max_depth:.2f} m")

    # ---- discrete events
    print("\nsolenoid fires")
    kinds = Counter()
    fires = [(t, decode_solenoid_fire(r.payload))
             for r, t in zip(records, times) if r.type == FL_SOLENOID_FIRE]
    for _, d in fires:
        kinds[d["kind"]] += 1
    for kind, n in sorted(kinds.items()):
        print(f"  {SOL_FIRE_KIND_NAMES.get(kind, kind):<14} {n:>6}")
    spans = _pair_solenoid_spans(fires)
    if spans:
        durs = sorted(t1 - t0 for t0, t1, _ in spans)
        reqs = sorted(d["requestedOnUs"] / US_PER_S for _, d in fires
                      if d["kind"] in (0, 2))
        print(f"  paired spans   {len(spans)}")
        print(f"  actual on (s)  min={durs[0]:.3f} median={durs[len(durs) // 2]:.3f} "
              f"max={durs[-1]:.3f}")
        print(f"  requested (s)  min={reqs[0]:.3f} median={reqs[len(reqs) // 2]:.3f} "
              f"max={reqs[-1]:.3f}")

    print("\nerror events")
    codes = Counter()
    details = Counter()
    for rec in records:
        if rec.type == FL_ERROR_EVENT:
            d = decode_error(rec.payload)
            codes[d["code"]] += 1
            details[(d["code"], d["detail"])] += 1
    total_err = sum(codes.values())
    for code, n in codes.most_common():
        pct = 100.0 * n / total_err if total_err else 0.0
        print(f"  {code:>3} {error_name(code):<22} {n:>8}  ({pct:5.1f}%)")
    if details:
        print("  most common (code, detail) pairs:")
        for (code, detail), n in details.most_common(8):
            print(f"    {error_name(code):<22} detail={detail:<12} "
                  f"(0x{detail:08x})  {n:>8}")

    print("\ndrop markers (data gaps)")
    dropped_by_type = Counter()
    total_dropped = 0
    n_markers = 0
    for rec in records:
        if rec.type == FL_DROP_MARKER:
            d = decode_drop(rec.payload)
            dropped_by_type[d["lastDroppedType"]] += d["count"]
            total_dropped += d["count"]
            n_markers += 1
    print(f"  markers {n_markers}, total records dropped {total_dropped}")
    for rtype, n in dropped_by_type.most_common():
        print(f"    last_dropped_type 0x{rtype:02x} "
              f"{TYPE_NAMES.get(rtype, '?'):<20} {n:>7}")

    # ---- ordering check
    print("\nordering")
    per_type_inversions = Counter()
    last_seen: dict[tuple[int, int], float] = {}
    for rec, t in zip(records, times):
        key = (rec.type, rec.payload[0] if rec.type in
               (FL_CELL_RAW_DIVEO2, FL_CELL_RAW_O2S, FL_CELL_RAW_ANALOG) else -1)
        if key in last_seen and t < last_seen[key]:
            per_type_inversions[key] += 1
        last_seen[key] = t
    if per_type_inversions:
        for (rtype, cell), n in per_type_inversions.most_common():
            label = TYPE_NAMES.get(rtype, "?") + (f" cell {cell}" if cell >= 0 else "")
            print(f"  {label:<26} {n} out-of-order sample(s)")
    else:
        print("  every per-type series is monotonic in time")
    return 0


def _pair_solenoid_spans(fires: list[tuple[float, dict]]) -> list[tuple[float, float, int]]:
    """Pair open/close SOLENOID_FIRE records into (t_open, t_close, kind) spans."""
    spans: list[list] = []
    pending: dict[int, int] = {}
    for t, d in fires:
        kind = d["kind"]
        family = kind >> 1
        if kind in (0, 2):
            spans.append([t, None, kind, d["requestedOnUs"] / US_PER_S])
            pending[family] = len(spans) - 1
        elif family in pending:
            spans[pending.pop(family)][1] = t
    for s in spans:
        if s[1] is None:
            s[1] = s[0] + s[3]
    return [(s[0], s[1], s[2]) for s in spans]


# ---- validate --------------------------------------------------------------

def _format_field(value) -> str:
    """Reproduce LogExport.js's summary-column value formatting."""
    if isinstance(value, list):
        return "[" + ",".join(str(v) for v in value) + "]"
    return str(value)


def _summarise(decoded: dict) -> str:
    """Reproduce LogExport.js's `k=v` summary string for a decoded record."""
    return " ".join(f"{k}={_format_field(v)}"
                    for k, v in decoded.items() if v is not None)


def _parse_summary(text: str) -> dict[str, str]:
    """Split a `k=v k=v` summary column back into a field map.

    Values never contain a space in any current record type (arrays are
    rendered comma-separated inside brackets), so splitting on whitespace is
    unambiguous.
    """
    fields: dict[str, str] = {}
    for token in text.split(" "):
        if "=" in token:
            key, _, value = token.partition("=")
            fields[key] = value
    return fields


def cmd_validate(args: argparse.Namespace) -> int:
    """Re-decode the .bin and diff it against the CSV the download tool wrote.

    The CSV carries ``payload_hex`` for every record and a ``summary`` column
    populated for whichever types the JS client could decode at export time.
    Both are independent ground truth: payload_hex validates the TLV walk and
    record ordering, summary validates the field-level decode.
    """
    data = Path(args.bin).read_bytes()
    records = list(iter_records(data))

    checked_hex = 0
    checked_summary = 0
    mismatch_hex: list[str] = []
    mismatch_summary: list[str] = []
    csv_rows = 0
    summary_by_type = Counter()
    added_fields: Counter = Counter()

    csv.field_size_limit(1 << 24)
    with open(args.csv, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            i = int(row["index"])
            csv_rows += 1
            if i >= len(records):
                mismatch_hex.append(f"row {i}: no matching record in .bin")
                continue
            rec = records[i]

            rtype = int(row["type"], 16)
            if rtype != rec.type:
                mismatch_hex.append(
                    f"row {i}: type csv=0x{rtype:02x} bin=0x{rec.type:02x}")
                continue
            if int(row["ts_us"]) != rec.ts_us:
                mismatch_hex.append(
                    f"row {i}: ts csv={row['ts_us']} bin={rec.ts_us}")
                continue
            if row["payload_hex"] != rec.payload.hex():
                mismatch_hex.append(f"row {i}: payload_hex differs")
                continue
            checked_hex += 1

            want = row["summary"].strip()
            if not want:
                continue
            decoded = decode_record(rec)
            if decoded is None:
                mismatch_summary.append(
                    f"row {i}: csv has a summary but this decoder returned None")
                continue
            # Compare field-by-field rather than as whole strings: the CSV may
            # predate decoder fields added later (e.g. BOOT_MARKER.prevCrash).
            # Every field the CSV *does* carry must match exactly; anything
            # only this decoder produces is reported as an addition, not a
            # failure.
            want_fields = _parse_summary(want)
            got_fields = _parse_summary(_summarise(decoded))
            summary_by_type[rec.type] += 1
            diffs = [f"{k}: csv={v!r} py={got_fields.get(k)!r}"
                     for k, v in want_fields.items() if got_fields.get(k) != v]
            if diffs:
                mismatch_summary.append(
                    f"row {i} (0x{rec.type:02x}): " + "; ".join(diffs))
            else:
                checked_summary += 1
            for k in got_fields.keys() - want_fields.keys():
                added_fields[(rec.type, k)] += 1

    print(f"bin records            {len(records)}")
    print(f"csv rows               {csv_rows}")
    print(f"payload_hex + ts + type verified   {checked_hex}")
    print(f"summary column verified            {checked_summary}")
    if summary_by_type:
        print("summary coverage by record type:")
        for rtype, n in summary_by_type.most_common():
            print(f"  0x{rtype:02x} {TYPE_NAMES.get(rtype, '?'):<20} {n}")
    if added_fields:
        print("fields this decoder adds beyond the CSV (informational):")
        for (rtype, key), n in added_fields.most_common():
            print(f"  0x{rtype:02x} {TYPE_NAMES.get(rtype, '?'):<20} {key} ({n})")

    failed = False
    for label, problems in (("structural", mismatch_hex),
                            ("summary", mismatch_summary)):
        if problems:
            failed = True
            print(f"\n{len(problems)} {label} mismatch(es); first 10:")
            for p in problems[:10]:
                print(f"  {p}")

    if len(records) != csv_rows:
        failed = True
        print(f"\nrecord count mismatch: bin={len(records)} csv={csv_rows}")

    print("\nRESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


# ---- tobin -----------------------------------------------------------------

def cmd_tobin(args: argparse.Namespace) -> int:
    """Rebuild a DCLG .bin stream from an exported CSV.

    The CSV is the flattened record list, so the rebuilt stream contains no
    BATCH containers — semantically identical for every reader, since readers
    flatten batches anyway.
    """
    out = bytearray()
    out += DCLG_MAGIC
    out += bytes([1, 0, 0, 0])          # version, flags, stream, pad
    header_counts = len(out)
    out += struct.pack("<II", 0, 0)     # totalBytes, entryCount — patched below

    n = 0
    csv.field_size_limit(1 << 24)
    with open(args.csv, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            payload = bytes.fromhex(row["payload_hex"])
            out += _HDR.pack(int(row["type"], 16), int(row["flags"]),
                             len(payload), int(row["ts_us"]))
            out += payload
            n += 1

    struct.pack_into("<II", out, header_counts,
                     len(out) - DCLG_HEADER_LEN, n)
    Path(args.output).write_bytes(out)
    print(f"wrote {args.output}: {n} records, {len(out) / 1e6:.1f} MB")
    return 0


# ---- entry point -----------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="telemetry_log.py", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_sum = sub.add_parser("summary", help="record counts, epochs, channel stats")
    p_sum.add_argument("file", help="downloaded telemetry .bin")
    p_sum.set_defaults(func=cmd_summary)

    p_val = sub.add_parser("validate", help="cross-check a .bin against its .csv")
    p_val.add_argument("bin", help="downloaded telemetry .bin")
    p_val.add_argument("csv", help="CSV exported by the download tool")
    p_val.set_defaults(func=cmd_validate)

    p_bin = sub.add_parser("tobin", help="rebuild a .bin stream from a CSV export")
    p_bin.add_argument("csv", help="CSV exported by the download tool")
    p_bin.add_argument("-o", "--output", required=True, help="destination .bin")
    p_bin.set_defaults(func=cmd_tobin)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
