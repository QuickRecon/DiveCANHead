/**
 * Flash-log stream test fixtures.
 *
 * Byte-level builders matching flash_log_entries.h / dut.py so LogParser can be
 * tested against hand-authored TLV streams.
 */

import { LOG_DOWNLOAD_MAGIC } from '../../src/uds/constants.js';
export {
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

export function bootMarkerPayload(bootId, fwVersion, resetCause, prevCrash = null) {
  const ver = new Uint8Array(16);
  ver.set(new TextEncoder().encode(fwVersion).slice(0, 16));
  const head = [...u32le(bootId), ...ver, ...u32le(resetCause)];
  if (prevCrash === null) return head;
  return [...head, ...u32le(prevCrash.magic), ...u32le(prevCrash.reason),
    ...u32le(prevCrash.pc), ...u32le(prevCrash.lr)];
}

function i32le(v) { return u32le(v >>> 0); }

function f32le(v) {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setFloat32(0, v, true);
  return [...b];
}

export function consensusPayload({
  consensusPpo2 = 0, ppo2 = [0, 0, 0], millivolts = [0, 0, 0],
  statusPacked = 0, confidence = 0, setpoint = 0
} = {}) {
  return [consensusPpo2, ...ppo2, ...u16le(millivolts[0]), ...u16le(millivolts[1]),
    ...u16le(millivolts[2]), ...u16le(statusPacked), confidence, setpoint];
}

export function pidPayload({ integral = 0, saturationCount = 0, duty = 0, setpoint = 0 } = {}) {
  return [...f32le(integral), ...u16le(saturationCount), ...f32le(duty), setpoint];
}

export function solenoidFirePayload(kind, requestedOnUs, offUs) {
  return [kind, ...u32le(requestedOnUs), ...u32le(offUs)];
}

export function solenoidCurrentPayload(role, classification, baselineUa, fireUa, deltaUa) {
  return [role, classification, ...i32le(baselineUa), ...i32le(fireUa), ...i32le(deltaUa)];
}

export function atmosPressurePayload(pressureMbar) {
  return u16le(pressureMbar);
}

export function powerSnapshotPayload({
  vbusVoltage = -1, vccVoltage = -1, batteryVoltage = -1, canVoltage = -1,
  batteryThreshold = 0, currentUa = 0, currentAgeMs = 0xFFFFFFFF,
  poseidonAgeSeconds = 0xFFFF, poseidonPercent = 0xFF, flags = 0
} = {}) {
  return [...f32le(vbusVoltage), ...f32le(vccVoltage), ...f32le(batteryVoltage),
    ...f32le(canVoltage), ...f32le(batteryThreshold), ...i32le(currentUa),
    ...u32le(currentAgeMs), ...u16le(poseidonAgeSeconds), poseidonPercent, flags];
}

export function cellDiveO2Payload({
  cellIndex = 0, ppo2 = 0, temperatureDc = 0, errCode = 0, phase = 0,
  intensity = 0, ambientLight = 0, pressureUhpa = 0, humidityMrh = 0
} = {}) {
  return [cellIndex, ppo2, ...i32le(temperatureDc), ...u32le(errCode), ...i32le(phase),
    ...i32le(intensity), ...i32le(ambientLight), ...u32le(pressureUhpa), ...i32le(humidityMrh)];
}

export function cellO2SPayload(cellIndex, ppo2, status) {
  return [cellIndex, ppo2, status];
}

export function cellAnalogPayload(cellIndex, ppo2, rawAdc, millivolts) {
  return [cellIndex, ppo2, ...i32le(rawAdc), ...u16le(millivolts)];
}

export function errorEventPayload(code, detail) {
  return [...u32le(code), ...u32le(detail)];
}

export function dropMarkerPayload(count, lastDroppedType) {
  return [...u32le(count), lastDroppedType];
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
