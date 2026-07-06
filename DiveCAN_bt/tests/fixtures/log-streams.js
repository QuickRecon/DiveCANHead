/**
 * Flash-log stream test fixtures.
 *
 * Byte-level builders matching flash_log_entries.h / dut.py so LogParser can be
 * tested against hand-authored TLV streams.
 */

import {
  LOG_DOWNLOAD_MAGIC,
  FL_ENTRY_HDR_LEN,
  FL_TYPE_BOOT_MARKER,
  FL_TYPE_DIVE_START,
  FL_TYPE_LOG_TEXT,
  FL_TYPE_CAN_RX,
  FL_TYPE_BATCH,
  FL_TYPE_END_OF_STREAM
} from '../../src/uds/constants.js';

function u16le(v) { return [v & 0xFF, (v >> 8) & 0xFF]; }
function u32le(v) { return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF]; }
function u64le(v) {
  const out = [];
  let n = BigInt(v);
  for (let i = 0; i < 8; i++) { out.push(Number(n & 0xFFn)); n >>= 8n; }
  return out;
}

/** Build a single TLV record (12-byte header + payload). */
export function buildRecord(type, payload = [], { flags = 0, tsUs = 0 } = {}) {
  return [type, flags, ...u16le(payload.length), ...u64le(tsUs), ...payload];
}

/** Build the 16-byte DCLG download header. */
export function buildDclgHeader({ version = 1, flags = 0, stream = 0, totalBytes = 0, entryCount = 0 } = {}) {
  return [...u32le(LOG_DOWNLOAD_MAGIC), version, flags, stream, 0, ...u32le(totalBytes), ...u32le(entryCount)];
}

/** Concatenate a DCLG header + records into a Uint8Array stream. */
export function buildStream(records, headerOpts = {}) {
  const bytes = [...buildDclgHeader(headerOpts)];
  for (const r of records) bytes.push(...r);
  return new Uint8Array(bytes);
}

// --- payload encoders (mirror the decode_* helpers) ---

export function bootMarkerPayload(bootId, fwVersion, resetCause) {
  const ver = new Uint8Array(16);
  ver.set(new TextEncoder().encode(fwVersion).slice(0, 16));
  return [...u32le(bootId), ...ver, ...u32le(resetCause)];
}

export function diveMarkerPayload(diveNumber, unixTimestamp) {
  return [...u16le(diveNumber), ...u32le(unixTimestamp)];
}

export function canFramePayload(id, dlc, data) {
  const d = new Uint8Array(8);
  d.set((data || []).slice(0, 8));
  return [...u32le(id), dlc, ...d];
}

export function logTextPayload(level, moduleId, text) {
  return [level, ...u16le(moduleId), ...new TextEncoder().encode(text)];
}

export { FL_ENTRY_HDR_LEN, FL_TYPE_BOOT_MARKER, FL_TYPE_DIVE_START, FL_TYPE_LOG_TEXT,
  FL_TYPE_CAN_RX, FL_TYPE_BATCH, FL_TYPE_END_OF_STREAM };
