/**
 * Rebuild a raw flash-log byte stream from an exported CSV.
 *
 * `LogExport.toCSV` writes one row per flattened record, including the verbatim
 * `payload_hex`. That is lossless for everything the viewer needs, so a CSV can
 * be turned back into the `.bin` the builder already knows how to walk, instead
 * of maintaining a second decode path.
 *
 * The rebuilt stream has no BATCH containers (the CSV is already flattened),
 * which every reader handles — batches exist only to bound the firmware's
 * boot-time sector walk, and readers flatten them anyway.
 *
 * Parsing is chunked so an 87 MB CSV never materialises as one 87 MB JavaScript
 * string and progress can be reported while it runs.
 */

import { FL_ENTRY_HDR_LEN, LOG_DCLG_HEADER_LEN, LOG_DOWNLOAD_MAGIC } from '../uds/constants.js';

/** Bytes of CSV text decoded per chunk. */
const CHUNK_BYTES = 8 * 1024 * 1024;

/** Initial output capacity, grown geometrically as records arrive. */
const INITIAL_CAPACITY = 1 << 20;

/** Column indices in the header written by LogExport.toCSV. */
const COL = { index: 0, type: 1, typeName: 2, tsUs: 3, flags: 4, summary: 5, payloadHex: 6 };

/**
 * Split one CSV line, honouring the RFC 4180 quoting LogExport emits.
 * @private
 */
function splitRow(line) {
  const out = [];
  let cur = '';
  let quoted = false;
  for (let i = 0; i < line.length; ++i) {
    const c = line[i];
    if (quoted) {
      if (c !== '"') { cur += c; continue; }
      if (line[i + 1] === '"') { cur += '"'; ++i; } else quoted = false;
    } else if (c === '"') {
      quoted = true;
    } else if (c === ',') {
      out.push(cur);
      cur = '';
    } else {
      cur += c;
    }
  }
  out.push(cur);
  return out;
}

/** Hex-decode into `dest` at `at`; returns the byte count written. @private */
function writeHex(dest, at, hex) {
  const n = hex.length >> 1;
  for (let i = 0; i < n; ++i) {
    dest[at + i] = Number.parseInt(hex.substr(i * 2, 2), 16);
  }
  return n;
}

/**
 * Convert a CSV `File`/`Blob` into a DCLG-framed byte stream.
 *
 * @param {Blob} blob CSV exported by the download tool
 * @param {{onProgress?: (fraction:number, message:string)=>void}} [opts]
 * @returns {Promise<Uint8Array>} Bytes accepted by `buildTelemetry`
 */
export async function csvToStream(blob, opts = {}) {
  const report = opts.onProgress || (() => {});
  const decoder = new TextDecoder('utf-8');

  let out = new Uint8Array(INITIAL_CAPACITY);
  let len = 0;
  const view = new DataView(out.buffer);

  // DCLG header — totalBytes / entryCount are patched once the count is known.
  const header = new DataView(new ArrayBuffer(LOG_DCLG_HEADER_LEN));
  header.setUint32(0, LOG_DOWNLOAD_MAGIC, true);
  header.setUint8(4, 1); // version
  out.set(new Uint8Array(header.buffer), 0);
  len = LOG_DCLG_HEADER_LEN;

  let cursor = { buf: out, view, len };

  const ensure = (extra) => {
    if (cursor.len + extra <= cursor.buf.length) return;
    let cap = cursor.buf.length;
    while (cap < cursor.len + extra) cap *= 2;
    const grown = new Uint8Array(cap);
    grown.set(cursor.buf.subarray(0, cursor.len));
    cursor.buf = grown;
    cursor.view = new DataView(grown.buffer);
  };

  let carry = '';
  let headerSeen = false;
  let records = 0;

  for (let offset = 0; offset < blob.size; offset += CHUNK_BYTES) {
    const slice = blob.slice(offset, Math.min(blob.size, offset + CHUNK_BYTES));
    const text = decoder.decode(new Uint8Array(await slice.arrayBuffer()), { stream: true });
    const lines = (carry + text).split('\n');
    carry = lines.pop() ?? '';

    for (const line of lines) {
      if (!headerSeen) { headerSeen = true; continue; }
      if (line.length === 0) continue;
      const cells = splitRow(line);
      const hex = cells[COL.payloadHex];
      if (hex === undefined) continue;
      const payloadLen = hex.length >> 1;
      ensure(FL_ENTRY_HDR_LEN + payloadLen);
      const at = cursor.len;
      cursor.buf[at] = Number.parseInt(cells[COL.type], 16);
      cursor.buf[at + 1] = Number(cells[COL.flags]);
      cursor.view.setUint16(at + 2, payloadLen, true);
      cursor.view.setBigUint64(at + 4, BigInt(cells[COL.tsUs]), true);
      cursor.len = at + FL_ENTRY_HDR_LEN + writeHex(cursor.buf, at + FL_ENTRY_HDR_LEN, hex);
      ++records;
    }
    report(offset / blob.size, `Parsing CSV — ${records.toLocaleString()} records`);
  }

  cursor.view.setUint32(8, cursor.len - LOG_DCLG_HEADER_LEN, true);
  cursor.view.setUint32(12, records, true);
  report(1, `Parsed ${records.toLocaleString()} records`);
  return cursor.buf.subarray(0, cursor.len);
}
