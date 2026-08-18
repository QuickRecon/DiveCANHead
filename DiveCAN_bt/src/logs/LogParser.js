/**
 * Flash-log stream parser.
 *
 * A downloaded stream is a 16-byte DCLG header followed by concatenated TLV
 * records. Each TLV = 12-byte header [type u8, flags u8, length u16 LE,
 * ts_boot_us u64 LE] + `length` payload bytes. BATCH (0xFD) records are
 * containers that are flattened into their sub-records. Parsing stops at
 * END_OF_STREAM (0xFF) or a truncated tail.
 *
 * Mirrors dut.parse_log_stream and the module-level decode_* helpers.
 */

import {
  LOG_DOWNLOAD_MAGIC,
  LOG_DCLG_HEADER_LEN,
  FL_ENTRY_HDR_LEN,
  FL_TYPE_BATCH,
  FL_TYPE_END_OF_STREAM,
  FL_TYPE_BOOT_MARKER,
  FL_TYPE_DIVE_START,
  FL_TYPE_DIVE_END,
  FL_TYPE_CAN_RX,
  FL_TYPE_CAN_TX,
  FL_TYPE_LOG_TEXT,
  FL_TYPE_CONSENSUS,
  FL_TYPE_PID_SNAPSHOT,
  FL_TYPE_SOLENOID_FIRE,
  FL_TYPE_SOLENOID_CURRENT,
  FL_TYPE_CELL_RAW_DIVEO2,
  FL_TYPE_CELL_RAW_O2S,
  FL_TYPE_CELL_RAW_ANALOG,
  FL_TYPE_ERROR_EVENT,
  FL_TYPE_DROP_MARKER,
  FL_CRASH_MAGIC,
  FL_TYPE_NAMES
} from '../uds/constants.js';
import { ByteUtils } from '../utils/ByteUtils.js';

/* ---- Payload lengths (bytes) — see firmware src/flash_log/flash_log_entries.h ---- */
const LEN_BOOT_MARKER = 40;      // boot_id + fw[16] + reset_cause + 4× prev_crash
const LEN_BOOT_MARKER_MIN = 24;  // pre-crash-fields firmware still decodes
const LEN_DIVE_MARKER = 6;
const LEN_CAN_FRAME = 13;
const LEN_LOG_TEXT_MIN = 3;
const LEN_CONSENSUS = 14;
const LEN_PID = 11;
const LEN_SOLENOID_FIRE = 9;
const LEN_SOLENOID_CURRENT = 14;
const LEN_CELL_DIVEO2 = 30;
const LEN_CELL_O2S = 3;
const LEN_CELL_ANALOG = 8;   // packed: u8 + u8 + i32 + u16
const LEN_ERROR_EVENT = 8;
const LEN_DROP_MARKER = 5;

/* ---- Unit scale factors ----
 *
 * The firmware stores these fields in fixed-point integer units. The names of
 * two DiveO2 fields are misnomers carried over from an early revision — the
 * scale constants below are the authoritative conversions, cross-checked
 * against firmware `src/calibration.c` (pressure) and against measured sensor
 * ranges (temperature, humidity). See docs/TELEMETRY_VIEWER.md.
 */

/** PPO2 wire values are uint8 centibar: 69 -> 0.69 bar. */
export const PPO2_CBAR_PER_BAR = 100;
/** Millivolts_t is uint16 in 0.01 mV units: 4520 -> 45.20 mV. */
export const MILLIVOLT_LSB_PER_MV = 100;
/**
 * DiveO2 `temperature_dc` is **milli-degrees Celsius**, not deci-degrees as the
 * field name suggests (raw 22921 -> 22.921 degC; /10 would give 2292 degC).
 */
export const DIVEO2_TEMP_LSB_PER_DEGC = 1000;
/**
 * DiveO2 `pressure_uhpa` is **milli-hPa** (== milli-mbar), not micro-hPa.
 * Confirmed by firmware `src/calibration.c`: `pressure_uhpa / 1000U` -> mbar.
 */
export const DIVEO2_PRESSURE_LSB_PER_MBAR = 1000;
/** DiveO2 `humidity_mrh` is milli-%RH: 45210 -> 45.21 %RH. */
export const DIVEO2_HUMIDITY_LSB_PER_PCT = 1000;
/** Sea-water column: 1 metre ~= 100 mbar. Used for the derived depth channel. */
export const MBAR_PER_METRE = 100;

/* ---- CONSENSUS status_packed bit layout ----
 * Mirrors the packing in firmware flash_log.c::flash_log_enqueue_consensus.
 *   bits 0-1 status[0], bit 2 include[0]
 *   bits 3-4 status[1], bit 5 include[1]
 *   bits 6-7 status[2], bit 8 include[2]
 */
const CONSENSUS_STATUS_MASK = 0x03;
const CONSENSUS_STATUS_SHIFTS = [0, 3, 6];
const CONSENSUS_INCLUDE_SHIFTS = [2, 5, 8];

function toBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  return new Uint8Array(input);
}

function readU64LE(bytes, offset) {
  let result = 0n;
  for (let i = 7; i >= 0; i--) {
    result = (result << 8n) | BigInt(bytes[offset + i] || 0);
  }
  return result;
}

/**
 * Parse the 16-byte DCLG download header if present.
 * @param {Uint8Array|Array} input
 * @returns {{magic:number, version:number, flags:number, stream:number,
 *   totalBytes:number, entryCount:number}|null}
 */
export function parseDclgHeader(input) {
  const bytes = toBytes(input);
  if (bytes.length < LOG_DCLG_HEADER_LEN) return null;
  const magic = ByteUtils.leToUint32(bytes.slice(0, 4));
  if (magic !== LOG_DOWNLOAD_MAGIC) return null;
  return {
    magic,
    version: bytes[4],
    flags: bytes[5],
    stream: bytes[6],
    totalBytes: ByteUtils.leToUint32(bytes.slice(8, 12)),
    entryCount: ByteUtils.leToUint32(bytes.slice(12, 16))
  };
}

/**
 * Parse a downloaded flash-log byte stream into a flat list of record dicts
 * {type, typeName, flags, tsUs, payload}. BATCH containers are flattened.
 * @param {Uint8Array|Array} input
 * @returns {Array<{type:number, typeName:string, flags:number, tsUs:bigint, payload:Uint8Array}>}
 */
export function parseLogStream(input) {
  let body = toBytes(input);
  if (parseDclgHeader(body)) {
    body = body.slice(LOG_DCLG_HEADER_LEN);
  }

  const records = [];

  const walk = (buf) => {
    let i = 0;
    const n = buf.length;
    while (i + FL_ENTRY_HDR_LEN <= n) {
      const rtype = buf[i];
      const flags = buf[i + 1];
      const length = ByteUtils.leToUint16(buf.slice(i + 2, i + 4));
      const tsUs = readU64LE(buf, i + 4);
      const payload = buf.slice(i + FL_ENTRY_HDR_LEN, i + FL_ENTRY_HDR_LEN + length);
      if (payload.length < length) break; // truncated tail
      if (rtype === FL_TYPE_END_OF_STREAM) break;
      if (rtype === FL_TYPE_BATCH) {
        walk(payload);
      } else {
        records.push({
          type: rtype,
          typeName: FL_TYPE_NAMES[rtype] || `0x${rtype.toString(16)}`,
          flags,
          tsUs,
          payload
        });
      }
      i += FL_ENTRY_HDR_LEN + length;
    }
  };

  walk(body);
  return records;
}

/**
 * Count the (flattened) records inside a fully-present slice of TLV bytes.
 * @private
 */
function countRecordsIn(buf) {
  let i = 0;
  let count = 0;
  const n = buf.length;
  while (i + FL_ENTRY_HDR_LEN <= n) {
    const rtype = buf[i];
    const length = ByteUtils.leToUint16(buf.slice(i + 2, i + 4));
    const end = i + FL_ENTRY_HDR_LEN + length;
    if (end > n) break;
    if (rtype === FL_TYPE_END_OF_STREAM) break;
    if (rtype === FL_TYPE_BATCH) {
      count += countRecordsIn(buf.slice(i + FL_ENTRY_HDR_LEN, end));
    } else {
      count += 1;
    }
    i = end;
  }
  return count;
}

/**
 * Create a resumable record counter for a growing download buffer, so a live
 * progress indicator can count records without re-parsing from scratch (O(total)
 * overall, not O(total^2)). Feed it the full accumulated buffer each call; it
 * advances an internal cursor past complete top-level entries and returns the
 * running record count.
 * @returns {(buffer: Uint8Array|Array) => number}
 */
export function makeRecordCounter() {
  let offset = -1; // -1 until the DCLG header (if any) is resolved
  let count = 0;
  let done = false;

  return function next(buffer) {
    if (done) return count;
    const bytes = toBytes(buffer);
    if (offset < 0) {
      if (bytes.length < LOG_DCLG_HEADER_LEN) return count;
      offset = parseDclgHeader(bytes) ? LOG_DCLG_HEADER_LEN : 0;
    }
    let i = offset;
    const n = bytes.length;
    while (i + FL_ENTRY_HDR_LEN <= n) {
      const rtype = bytes[i];
      const length = ByteUtils.leToUint16(bytes.slice(i + 2, i + 4));
      const end = i + FL_ENTRY_HDR_LEN + length;
      if (end > n) break; // entry not fully received yet — resume next call
      if (rtype === FL_TYPE_END_OF_STREAM) { done = true; break; }
      if (rtype === FL_TYPE_BATCH) {
        count += countRecordsIn(bytes.slice(i + FL_ENTRY_HDR_LEN, end));
      } else {
        count += 1;
      }
      i = end;
    }
    offset = i;
    return count;
  };
}

/* ---- Little-endian scalar readers over a Uint8Array ---- */

function readI32LE(p, o) {
  return (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) | 0;
}

function readU32LE(p, o) {
  return ((p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) >>> 0);
}

function readU16LE(p, o) {
  return p[o] | (p[o + 1] << 8);
}

/** Shared scratch for the f32 reinterpret — avoids allocating per record. */
const F32_SCRATCH = new DataView(new ArrayBuffer(4));

function readF32LE(p, o) {
  F32_SCRATCH.setUint8(0, p[o]);
  F32_SCRATCH.setUint8(1, p[o + 1]);
  F32_SCRATCH.setUint8(2, p[o + 2]);
  F32_SCRATCH.setUint8(3, p[o + 3]);
  return F32_SCRATCH.getFloat32(0, true);
}

/**
 * Decode a BOOT_MARKER payload.
 *
 * The full 40-byte payload carries the previous boot's crash snapshot; a
 * 24-byte prefix (the pre-crash-field layout) still decodes the identity
 * fields, so short payloads degrade gracefully rather than returning null.
 *
 * @param {Uint8Array|Array} payload
 * @returns {{bootId:number, fwVersion:string, resetCause:number,
 *   prevCrash:?{magic:number, reason:number, pc:number, lr:number}}|null}
 */
export function decodeBootMarker(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_BOOT_MARKER_MIN) return null;
  const verBytes = p.slice(4, 20);
  const nul = verBytes.indexOf(0);
  const fwVersion = new TextDecoder('ascii').decode(nul >= 0 ? verBytes.slice(0, nul) : verBytes);
  let prevCrash = null;
  if (p.length >= LEN_BOOT_MARKER) {
    const magic = readU32LE(p, 24);
    if (magic === FL_CRASH_MAGIC) {
      prevCrash = {
        magic,
        reason: readU32LE(p, 28),
        pc: readU32LE(p, 32),
        lr: readU32LE(p, 36)
      };
    }
  }
  return {
    bootId: readU32LE(p, 0),
    fwVersion,
    resetCause: readU32LE(p, 20),
    prevCrash
  };
}

/** Decode a DIVE_START/END payload -> {diveNumber, unixTimestamp}. */
export function decodeDiveMarker(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_DIVE_MARKER) return null;
  return {
    diveNumber: readU16LE(p, 0),
    unixTimestamp: readU32LE(p, 2)
  };
}

/** Decode a CAN_RX/CAN_TX payload -> {id, dlc, data}. */
export function decodeCanFrame(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_CAN_FRAME) return null;
  return {
    id: readU32LE(p, 0),
    dlc: p[4],
    data: p.slice(5, 13)
  };
}

/** Decode a LOG_TEXT payload -> {level, moduleId, text}. */
export function decodeLogText(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_LOG_TEXT_MIN) return null;
  return {
    level: p[0],
    moduleId: readU16LE(p, 1),
    text: new TextDecoder('ascii').decode(p.slice(3))
  };
}

/**
 * Unpack the CONSENSUS status/include bitfield into per-cell entries.
 * @param {number} packed - statusPacked value
 * @returns {Array<{status:number, include:boolean}>} one entry per cell
 */
export function unpackConsensusStatus(packed) {
  return CONSENSUS_STATUS_SHIFTS.map((shift, i) => ({
    status: (packed >> shift) & CONSENSUS_STATUS_MASK,
    include: ((packed >> CONSENSUS_INCLUDE_SHIFTS[i]) & 1) === 1
  }));
}

/** Per-cell status codes from a packed CONSENSUS bitfield. */
export function consensusStatusArray(packed) {
  return CONSENSUS_STATUS_SHIFTS.map((shift) => (packed >> shift) & CONSENSUS_STATUS_MASK);
}

/** Per-cell include flags (0/1) from a packed CONSENSUS bitfield. */
export function consensusIncludeArray(packed) {
  return CONSENSUS_INCLUDE_SHIFTS.map((shift) => (packed >> shift) & 1);
}

/**
 * Decode a CONSENSUS payload -> control snapshot.
 *
 * `consensusPpo2`, `ppo2[]` and `setpoint` are uint8 centibar; `millivolts[]`
 * are uint16 in 0.01 mV units. Fields mirror the firmware struct one-for-one
 * and in declaration order, so the flattened CSV summary column stays
 * byte-faithful; call `unpackConsensusStatus()` for the per-cell view.
 */
export function decodeConsensus(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_CONSENSUS) return null;
  return {
    consensusPpo2: p[0],
    ppo2: [p[1], p[2], p[3]],
    millivolts: [readU16LE(p, 4), readU16LE(p, 6), readU16LE(p, 8)],
    statusPacked: readU16LE(p, 10),
    confidence: p[12],
    setpoint: p[13]
  };
}

/** Decode a PID_SNAPSHOT payload -> {integral, saturationCount, duty, setpoint}. */
export function decodePidSnapshot(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_PID) return null;
  return {
    integral: readF32LE(p, 0),
    saturationCount: readU16LE(p, 4),
    duty: readF32LE(p, 6),
    setpoint: p[10]
  };
}

/** Decode a SOLENOID_FIRE payload -> {kind, requestedOnUs, offUs}. */
export function decodeSolenoidFire(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_SOLENOID_FIRE) return null;
  return {
    kind: p[0],
    requestedOnUs: readU32LE(p, 1),
    offUs: readU32LE(p, 5)
  };
}

/** Decode a SOLENOID_CURRENT payload -> closed-loop fire-current verdict. */
export function decodeSolenoidCurrent(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_SOLENOID_CURRENT) return null;
  return {
    role: p[0],
    classification: p[1],
    baselineUa: readI32LE(p, 2),
    fireUa: readI32LE(p, 6),
    deltaUa: readI32LE(p, 10)
  };
}

/**
 * Decode a CELL_RAW_DIVEO2 payload.
 *
 * Raw integer fields are kept under their firmware names; SI-converted
 * companions are added alongside (see the unit constants at the top of this
 * module for why `temperatureDc`/`pressureUhpa` are misnomers).
 */
export function decodeCellDiveO2(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_CELL_DIVEO2) return null;
  const temperatureDc = readI32LE(p, 2);
  const pressureUhpa = readU32LE(p, 22);
  const humidityMrh = readI32LE(p, 26);
  return {
    cellIndex: p[0],
    ppo2: p[1],
    temperatureDc,
    errCode: readU32LE(p, 6),
    phase: readI32LE(p, 10),
    intensity: readI32LE(p, 14),
    ambientLight: readI32LE(p, 18),
    pressureUhpa,
    humidityMrh,
    ppo2Bar: p[1] / PPO2_CBAR_PER_BAR,
    temperatureC: temperatureDc / DIVEO2_TEMP_LSB_PER_DEGC,
    pressureMbar: pressureUhpa / DIVEO2_PRESSURE_LSB_PER_MBAR,
    humidityPct: humidityMrh / DIVEO2_HUMIDITY_LSB_PER_PCT
  };
}

/** Decode a CELL_RAW_O2S payload -> {cellIndex, ppo2, status}. */
export function decodeCellO2S(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_CELL_O2S) return null;
  return {
    cellIndex: p[0],
    ppo2: p[1],
    status: p[2],
    ppo2Bar: p[1] / PPO2_CBAR_PER_BAR
  };
}

/** Decode a CELL_RAW_ANALOG payload -> {cellIndex, ppo2, rawAdc, millivolts}. */
export function decodeCellAnalog(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_CELL_ANALOG) return null;
  const millivolts = readU16LE(p, 6);
  return {
    cellIndex: p[0],
    ppo2: p[1],
    rawAdc: readI32LE(p, 2),
    millivolts,
    ppo2Bar: p[1] / PPO2_CBAR_PER_BAR,
    mv: millivolts / MILLIVOLT_LSB_PER_MV
  };
}

/** Decode an ERROR_EVENT payload -> {code, detail}. */
export function decodeErrorEvent(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_ERROR_EVENT) return null;
  return {
    code: readU32LE(p, 0),
    detail: readU32LE(p, 4)
  };
}

/** Decode a DROP_MARKER payload -> {count, lastDroppedType}. */
export function decodeDropMarker(payload) {
  const p = toBytes(payload);
  if (p.length < LEN_DROP_MARKER) return null;
  return {
    count: readU32LE(p, 0),
    lastDroppedType: p[4]
  };
}

/**
 * Decode a record's payload into a typed object based on its type.
 * @param {{type:number, payload:Uint8Array}} record
 * @returns {Object|null}
 */
export function decodeRecord(record) {
  switch (record.type) {
    case FL_TYPE_BOOT_MARKER: return decodeBootMarker(record.payload);
    case FL_TYPE_DIVE_START:
    case FL_TYPE_DIVE_END: return decodeDiveMarker(record.payload);
    case FL_TYPE_CAN_RX:
    case FL_TYPE_CAN_TX: return decodeCanFrame(record.payload);
    case FL_TYPE_LOG_TEXT: return decodeLogText(record.payload);
    case FL_TYPE_CONSENSUS: return decodeConsensus(record.payload);
    case FL_TYPE_PID_SNAPSHOT: return decodePidSnapshot(record.payload);
    case FL_TYPE_SOLENOID_FIRE: return decodeSolenoidFire(record.payload);
    case FL_TYPE_SOLENOID_CURRENT: return decodeSolenoidCurrent(record.payload);
    case FL_TYPE_CELL_RAW_DIVEO2: return decodeCellDiveO2(record.payload);
    case FL_TYPE_CELL_RAW_O2S: return decodeCellO2S(record.payload);
    case FL_TYPE_CELL_RAW_ANALOG: return decodeCellAnalog(record.payload);
    case FL_TYPE_ERROR_EVENT: return decodeErrorEvent(record.payload);
    case FL_TYPE_DROP_MARKER: return decodeDropMarker(record.payload);
    default: return null;
  }
}
