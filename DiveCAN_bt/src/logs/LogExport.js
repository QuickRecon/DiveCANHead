/**
 * Flash-log export helpers: JSON, CSV, and raw binary.
 *
 * Operates on the record list produced by parseLogStream(). Each record's
 * type-specific payload is decoded via decodeRecord() and flattened for export.
 */

import { decodeRecord } from './LogParser.js';
import { ByteUtils } from '../utils/ByteUtils.js';

/**
 * Build plain export rows from parsed records (decoded fields flattened,
 * BigInt timestamps stringified so they survive JSON/CSV).
 * @param {Array} records - Output of parseLogStream()
 * @returns {Array<Object>}
 */
export function buildExportRows(records) {
  return records.map((rec, index) => {
    const decoded = decodeRecord(rec);
    return {
      index,
      type: rec.type,
      typeName: rec.typeName,
      flags: rec.flags,
      tsUs: rec.tsUs.toString(),
      decoded: decoded || {},
      payloadHex: ByteUtils.toHexString(rec.payload, '')
    };
  });
}

/** Serialise a decoded object into a compact "k=v" summary string. */
function summarise(decoded) {
  return Object.entries(decoded)
    .map(([k, v]) => `${k}=${Array.isArray(v) ? `[${v.join(',')}]` : v}`)
    .join(' ');
}

/**
 * Export records as pretty-printed JSON.
 * @param {Array} records - Output of parseLogStream()
 * @returns {string}
 */
export function toJSON(records) {
  return JSON.stringify(buildExportRows(records), null, 2);
}

/**
 * Export records as CSV (one row per record, decoded fields in a summary column).
 * @param {Array} records - Output of parseLogStream()
 * @returns {string}
 */
export function toCSV(records) {
  const header = ['index', 'type', 'type_name', 'ts_us', 'flags', 'summary', 'payload_hex'];
  const escape = (s) => {
    const str = String(s);
    return /[",\n]/.test(str) ? `"${str.replaceAll('"', '""')}"` : str;
  };
  const lines = [header.join(',')];
  for (const row of buildExportRows(records)) {
    lines.push([
      row.index,
      `0x${row.type.toString(16).padStart(2, '0')}`,
      row.typeName,
      row.tsUs,
      row.flags,
      summarise(row.decoded),
      row.payloadHex
    ].map(escape).join(','));
  }
  return lines.join('\n');
}

/**
 * Return the raw stream bytes as a Blob (for a .bin download).
 * @param {Uint8Array|Array} streamBytes - The concatenated download bytes
 * @returns {Blob}
 */
export function toRawBin(streamBytes) {
  const bytes = streamBytes instanceof Uint8Array ? streamBytes : new Uint8Array(streamBytes);
  return new Blob([bytes], { type: 'application/octet-stream' });
}

/**
 * Trigger a browser download for a string or Blob. No-op outside a DOM.
 * @param {string} filename
 * @param {string|Blob} content
 * @param {string} [mime='application/octet-stream']
 */
export function triggerDownload(filename, content, mime = 'application/octet-stream') {
  if (typeof document === 'undefined' || typeof URL === 'undefined') return;
  const blob = content instanceof Blob ? content : new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
