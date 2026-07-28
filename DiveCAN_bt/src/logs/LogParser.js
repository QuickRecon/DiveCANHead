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
  FL_TYPE_NAMES
} from '../uds/constants.js';
import { ByteUtils } from '../utils/ByteUtils.js';

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

/** Decode a BOOT_MARKER payload -> {bootId, fwVersion, resetCause}. */
export function decodeBootMarker(payload) {
  const p = toBytes(payload);
  if (p.length < 24) return null;
  const verBytes = p.slice(4, 20);
  const nul = verBytes.indexOf(0);
  const fwVersion = new TextDecoder('ascii').decode(nul >= 0 ? verBytes.slice(0, nul) : verBytes);
  return {
    bootId: ByteUtils.leToUint32(p.slice(0, 4)),
    fwVersion,
    resetCause: ByteUtils.leToUint32(p.slice(20, 24))
  };
}

/** Decode a DIVE_START/END payload -> {diveNumber, unixTimestamp}. */
export function decodeDiveMarker(payload) {
  const p = toBytes(payload);
  if (p.length < 6) return null;
  return {
    diveNumber: ByteUtils.leToUint16(p.slice(0, 2)),
    unixTimestamp: ByteUtils.leToUint32(p.slice(2, 6))
  };
}

/** Decode a CAN_RX/CAN_TX payload -> {id, dlc, data}. */
export function decodeCanFrame(payload) {
  const p = toBytes(payload);
  if (p.length < 13) return null;
  return {
    id: ByteUtils.leToUint32(p.slice(0, 4)),
    dlc: p[4],
    data: p.slice(5, 13)
  };
}

/** Decode a LOG_TEXT payload -> {level, moduleId, text}. */
export function decodeLogText(payload) {
  const p = toBytes(payload);
  if (p.length < 3) return null;
  return {
    level: p[0],
    moduleId: ByteUtils.leToUint16(p.slice(1, 3)),
    text: new TextDecoder('ascii').decode(p.slice(3))
  };
}

/** Decode a CONSENSUS payload -> control snapshot. */
export function decodeConsensus(payload) {
  const p = toBytes(payload);
  if (p.length < 14) return null;
  return {
    consensusPpo2: p[0],
    ppo2: [p[1], p[2], p[3]],
    millivolts: [
      ByteUtils.leToUint16(p.slice(4, 6)),
      ByteUtils.leToUint16(p.slice(6, 8)),
      ByteUtils.leToUint16(p.slice(8, 10))
    ],
    statusPacked: ByteUtils.leToUint16(p.slice(10, 12)),
    confidence: p[12],
    setpoint: p[13]
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
    default: return null;
  }
}
