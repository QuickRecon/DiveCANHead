/**
 * Build plottable typed-array channels from a flash-log telemetry stream.
 *
 * Input is the raw downloaded `.bin` (optional 16-byte DCLG header + TLV
 * records, BATCH containers flattened). Output is a `TelemetryModel`: one
 * table per record type (split per cell index where applicable), each holding
 * a Float64Array of times and a Float32Array per channel, plus discrete-event
 * arrays for the overlay layer.
 *
 * Two passes over the buffer: the first counts records and discovers boot
 * epochs, the second fills pre-sized arrays. No per-record object allocation,
 * so a 24 MB / 850k-record log builds in a couple of seconds and the result
 * transfers to the main thread as a handful of ArrayBuffers.
 *
 * ---- Time base ----
 * `ts_boot_us` restarts at zero on every reboot, and a ring that has wrapped
 * begins part-way through an epoch. Records are therefore segmented at each
 * BOOT_MARKER and each epoch is laid end-to-end on a synthetic global axis
 * separated by EPOCH_GAP_S, so a multi-boot log plots as one continuous
 * timeline without pretending the reboot took no time. Every table also keeps
 * the boot-relative time so the readout can name the epoch.
 */

import {
  LOG_DCLG_HEADER_LEN,
  FL_ENTRY_HDR_LEN,
  FL_TYPE_BATCH,
  FL_TYPE_END_OF_STREAM,
  FL_TYPE_BOOT_MARKER,
  FL_TYPE_DIVE_START,
  FL_TYPE_DIVE_END,
  FL_TYPE_SOLENOID_FIRE,
  FL_TYPE_SOLENOID_CURRENT,
  FL_TYPE_ERROR_EVENT,
  FL_TYPE_DROP_MARKER,
  FL_TYPE_NAMES,
  SOL_FIRE_EVT_INJECT_START,
  SOL_FIRE_EVT_FLUSH_START
} from '../uds/constants.js';
import {
  parseDclgHeader,
  decodeBootMarker,
  decodeDiveMarker,
  DIVEO2_PRESSURE_LSB_PER_MBAR,
  MBAR_PER_METRE
} from '../logs/LogParser.js';
import { TABLES, tableForType, CELL_COUNT } from './TelemetryModel.js';

/** Synthetic separation inserted between boot epochs on the global axis. */
export const EPOCH_GAP_S = 30;

/** Microseconds per second. */
const US_PER_S = 1e6;

/** Percentile of observed ambient pressure taken as the surface reference. */
const SURFACE_PERCENTILE = 0.02;

/** Sample cap for the surface-pressure percentile estimate. */
const SURFACE_SAMPLE_CAP = 20000;

/* ---- Little-endian readers over a Uint8Array ---- */

/** Reused scratch for the f32 reinterpret — flash-log f32 fields are unaligned. */
const F32_SCRATCH = new DataView(new ArrayBuffer(4));

const READERS = {
  u8: (p, o) => p[o],
  u16: (p, o) => p[o] | (p[o + 1] << 8),
  i32: (p, o) => (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) | 0,
  u32: (p, o) => ((p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) >>> 0),
  f32: (p, o) => {
    F32_SCRATCH.setUint8(0, p[o]);
    F32_SCRATCH.setUint8(1, p[o + 1]);
    F32_SCRATCH.setUint8(2, p[o + 2]);
    F32_SCRATCH.setUint8(3, p[o + 3]);
    return F32_SCRATCH.getFloat32(0, true);
  }
};

/**
 * Walk a TLV buffer, invoking `visit(type, flags, tsUs, payloadOffset, payloadLen)`
 * for every record. BATCH containers recurse; END_OF_STREAM and a truncated
 * tail both stop the walk.
 *
 * Operates on offsets into the original buffer so no slicing occurs.
 * @private
 */
function walkStream(bytes, view, start, visit) {
  const U32_SPAN = 4294967296;
  const stack = [[start, bytes.length]];
  let finished = false;
  while (stack.length > 0 && !finished) {
    const [from, to] = stack.pop();
    let i = from;
    while (i + FL_ENTRY_HDR_LEN <= to) {
      const type = bytes[i];
      const flags = bytes[i + 1];
      const len = view.getUint16(i + 2, true);
      // ts_boot_us is a u64; the low 53 bits are exact in a double and a
      // 4.5e6-second uptime is nowhere near that, so read it as two u32s.
      const tsUs = view.getUint32(i + 8, true) * U32_SPAN + view.getUint32(i + 4, true);
      const body = i + FL_ENTRY_HDR_LEN;
      const end = body + len;
      if (end > to) break;                       // truncated tail
      if (type === FL_TYPE_END_OF_STREAM) { finished = true; break; }
      if (type === FL_TYPE_BATCH) {
        // Continue this level after the batch, then descend into it (LIFO, so
        // the batch body is visited before the records that follow it).
        stack.push([end, to]);
        stack.push([body, end]);
        break;
      }
      visit(type, flags, tsUs, body, len);
      i = end;
    }
  }
}

/**
 * Discover boot epochs and per-table record counts.
 * @private
 */
function scanStream(bytes, view, start) {
  const counts = new Map();      // tableId -> count
  const byType = new Map();      // record type -> count
  const epochs = [];             // {index, minUs, maxUs, boot}
  let epoch = { index: 0, minUs: Infinity, maxUs: -Infinity, boot: null };
  epochs.push(epoch);
  let total = 0;
  const eventCounts = { solenoid: 0, error: 0, drop: 0, solCurrent: 0, marker: 0 };

  walkStream(bytes, view, start, (type, flags, tsUs, off, len) => {
    ++total;
    byType.set(type, (byType.get(type) || 0) + 1);

    if (type === FL_TYPE_BOOT_MARKER) {
      // A boot marker opens a new epoch. Its own timestamp belongs to the new
      // epoch, not the one that just ended.
      if (epoch.minUs !== Infinity) {
        epoch = { index: epochs.length, minUs: Infinity, maxUs: -Infinity, boot: null };
        epochs.push(epoch);
      }
      epoch.boot = decodeBootMarker(bytes.subarray(off, off + len));
    }

    if (tsUs < epoch.minUs) epoch.minUs = tsUs;
    if (tsUs > epoch.maxUs) epoch.maxUs = tsUs;

    switch (type) {
      case FL_TYPE_SOLENOID_FIRE: ++eventCounts.solenoid; break;
      case FL_TYPE_SOLENOID_CURRENT: ++eventCounts.solCurrent; break;
      case FL_TYPE_ERROR_EVENT: ++eventCounts.error; break;
      case FL_TYPE_DROP_MARKER: ++eventCounts.drop; break;
      case FL_TYPE_BOOT_MARKER:
      case FL_TYPE_DIVE_START:
      case FL_TYPE_DIVE_END: ++eventCounts.marker; break;
      default: break;
    }

    const table = tableForType(type);
    if (table !== null) {
      const id = table.perCell ? `${table.id}_c${bytes[off + table.cellOff]}` : table.id;
      counts.set(id, (counts.get(id) || 0) + 1);
    }
  });

  // Epoch indices are positional and must match the second pass exactly, so an
  // empty epoch is normalised rather than removed.
  for (const e of epochs) {
    if (e.minUs === Infinity) { e.minUs = 0; e.maxUs = 0; }
  }
  return { counts, byType, epochs, total, eventCounts };
}

/**
 * Lay epochs end-to-end on a global seconds axis.
 * @private
 */
function assignEpochOffsets(epochs) {
  let cursor = 0;
  for (const e of epochs) {
    e.startS = cursor;
    e.spanS = (e.maxUs - e.minUs) / US_PER_S;
    e.offsetS = cursor - e.minUs / US_PER_S;
    cursor += e.spanS + EPOCH_GAP_S;
  }
  return Math.max(0, cursor - EPOCH_GAP_S);
}

/** Opposite dive-marker kind, for logs whose marker convention is reversed. */
function invertDiveMarkerKind(kind) {
  return kind === 'diveStart' ? 'diveEnd' : 'diveStart';
}

/**
 * Turn recorded dive markers into selectable dive and per-boot windows.
 *
 * Some DiveCAN logs have the observed marker quirk `DIVE_END` at the physical
 * start and `DIVE_START` at the physical end. A paired dive is unambiguous in
 * either order: whichever kind occurs first determines whether that dive uses
 * normal or reversed semantics. The
 * original `marker.kind` is retained as wire evidence and `semanticKind` is
 * added for labels, anchors and range selection.
 *
 * A marker missing at a ring edge is exposed only when the order can be
 * inferred from another complete dive in the same stream.  This avoids
 * presenting an arbitrary side of a lone marker as a known dive.
 *
 * @param {Array<Object>} markers All boot and dive markers (annotated in place)
 * @param {Array<Object>} epochs Epochs after global offsets have been assigned
 * @param {number} durationS Full synthetic-axis duration
 * @returns {Array<Object>} dive windows, each with its intersecting boot epochs
 */
export function buildDiveWindows(markers, epochs, durationS) {
  const byDive = new Map();
  for (const marker of markers) {
    if ((marker.kind !== 'diveStart' && marker.kind !== 'diveEnd')
        || !Number.isFinite(marker.diveNumber) || !Number.isFinite(marker.t)) continue;
    const group = byDive.get(marker.diveNumber) || [];
    group.push(marker);
    byDive.set(marker.diveNumber, group);
  }

  // Complete pairs tell us how to interpret incomplete ring-edge fragments.
  let normalPairs = 0;
  let reversedPairs = 0;
  const orderByDive = new Map();
  for (const [diveNumber, group] of byDive) {
    const starts = group.filter((m) => m.kind === 'diveStart').sort((a, b) => a.t - b.t);
    const ends = group.filter((m) => m.kind === 'diveEnd').sort((a, b) => a.t - b.t);
    if (starts.length === 0 || ends.length === 0) continue;
    const order = starts[0].t <= ends[0].t ? 'normal' : 'reversed';
    orderByDive.set(diveNumber, order);
    if (order === 'normal') ++normalPairs;
    else ++reversedPairs;
  }
  const inferredOrder = normalPairs > reversedPairs ? 'normal'
    : reversedPairs > normalPairs ? 'reversed' : null;

  const dives = [];
  for (const [diveNumber, unsorted] of byDive) {
    const group = [...unsorted].sort((a, b) => a.t - b.t);
    const kinds = new Set(group.map((m) => m.kind));
    const complete = kinds.has('diveStart') && kinds.has('diveEnd');
    const markerOrder = orderByDive.get(diveNumber) || inferredOrder;

    // With a lone kind and no evidence about polarity, keep the raw marker in
    // the overlay but do not invent a selectable dive interval.
    if (markerOrder === null || markerOrder === undefined) continue;

    for (const marker of group) {
      marker.semanticKind = markerOrder === 'reversed'
        ? invertDiveMarkerKind(marker.kind) : marker.kind;
      marker.markerOrder = markerOrder;
      marker.markerOrderInferred = !complete;
    }

    let t0;
    let t1;
    if (complete) {
      t0 = group[0].t;
      t1 = group[group.length - 1].t;
    } else {
      const semanticKind = group[0].semanticKind;
      if (semanticKind === 'diveStart') {
        t0 = group[0].t;
        t1 = durationS;
      } else {
        t0 = 0;
        t1 = group[group.length - 1].t;
      }
    }

    t0 = Math.max(0, Math.min(durationS, t0));
    t1 = Math.max(0, Math.min(durationS, t1));
    if (!(t1 > t0)) continue;

    const boots = [];
    for (const epoch of epochs) {
      const epochEnd = epoch.startS + epoch.spanS;
      const bootStart = Math.max(t0, epoch.startS);
      const bootEnd = Math.min(t1, epochEnd);
      if (!(bootEnd > bootStart)) continue;
      boots.push({
        epochIndex: epoch.index,
        bootId: epoch.boot ? epoch.boot.bootId : null,
        t0: bootStart,
        t1: bootEnd
      });
    }

    dives.push({
      diveNumber,
      t0,
      t1,
      complete,
      markerOrder,
      markerCount: group.length,
      boots
    });
  }

  return dives.sort((a, b) => a.t0 - b.t0 || a.diveNumber - b.diveNumber);
}

/**
 * Sort a table in place by time, permuting every channel alongside.
 *
 * Batched flushes can emit a handful of records out of order (a producer
 * enqueued just before a flush lands in the following batch), and uPlot plus
 * the cursor binary search both require a monotonic x. Only runs when an
 * inversion is actually present.
 * @private
 */
function sortTableByTime(table) {
  const { time, n } = table;
  let ordered = true;
  for (let i = 1; i < n; ++i) {
    if (time[i] < time[i - 1]) { ordered = false; break; }
  }
  if (ordered) return 0;

  const order = new Array(n);
  for (let i = 0; i < n; ++i) order[i] = i;
  order.sort((a, b) => time[a] - time[b]);

  const sortedTime = new Float64Array(n);
  for (let i = 0; i < n; ++i) sortedTime[i] = time[order[i]];
  table.time.set(sortedTime);

  const sortedBoot = new Float64Array(n);
  for (let i = 0; i < n; ++i) sortedBoot[i] = table.bootTime[order[i]];
  table.bootTime.set(sortedBoot);

  const scratch = new Float32Array(n);
  for (const ch of Object.values(table.channels)) {
    for (let i = 0; i < n; ++i) scratch[i] = ch.data[order[i]];
    ch.data.set(scratch);
  }
  return 1;
}

/**
 * Estimate the surface reference pressure from the ambient-pressure channel.
 *
 * Takes a low percentile rather than the minimum so a single dropout or a
 * pre-dive altitude excursion does not set the datum. Returns null when no
 * pressure channel is present (all-analog head).
 * @private
 */
function estimateSurfaceMbar(tables) {
  const samples = [];
  for (const table of Object.values(tables)) {
    const ch = table.channels.pressureMbar;
    if (!ch) continue;
    const step = Math.max(1, Math.ceil(table.n / SURFACE_SAMPLE_CAP));
    for (let i = 0; i < table.n; i += step) {
      const v = ch.data[i];
      if (v > 0) samples.push(v);
    }
  }
  if (samples.length === 0) return null;
  samples.sort((a, b) => a - b);
  return samples[Math.min(samples.length - 1, Math.floor(samples.length * SURFACE_PERCENTILE))];
}

/**
 * (Re)compute the derived depth channel against a surface reference pressure.
 *
 * Exported so the viewer can re-datum the profile when the operator overrides
 * the automatic surface estimate (dive at altitude, sensor offset, etc.).
 *
 * @param {Object} model TelemetryModel (or its `tables` map)
 * @param {number} surfaceMbar Reference pressure in mbar
 */
export function applySurfaceReference(model, surfaceMbar) {
  const tables = model.tables || model;
  for (const table of Object.values(tables)) {
    const depth = table.channels.depth;
    const press = table.channels.pressureMbar;
    if (!depth || !press) continue;
    for (let i = 0; i < table.n; ++i) {
      depth.data[i] = (press.data[i] - surfaceMbar) / MBAR_PER_METRE;
    }
  }
  if (model.meta) model.meta.surfaceMbar = surfaceMbar;
}

/** Fill POWER_SNAPSHOT's derived watts channel from validity-filtered inputs. */
function derivePowerChannels(tables) {
  for (const table of Object.values(tables)) {
    const watts = table.channels.powerW;
    const volts = table.channels.batteryVoltage;
    const milliamps = table.channels.currentMa;
    if (!watts || !volts || !milliamps) continue;
    for (let i = 0; i < table.n; ++i) {
      watts.data[i] = volts.data[i] * milliamps.data[i] / 1000;
    }
  }
}

/**
 * Pair SOLENOID_FIRE start/end records into spans.
 *
 * `kind` 0/2 open the valve, 1/3 close it. An unmatched open (log truncated
 * mid-fire, or the close record was dropped) is closed at its requested
 * duration so the span still renders.
 * @private
 */
function buildSolenoidSpans(raw) {
  const starts = [];
  const t0 = [];
  const t1 = [];
  const kind = [];
  const requestedS = [];
  const openIdx = new Map(); // kind-of-open -> index into t0 awaiting a close

  for (let i = 0; i < raw.n; ++i) {
    const k = raw.kind[i];
    const isOpen = (k === SOL_FIRE_EVT_INJECT_START) || (k === SOL_FIRE_EVT_FLUSH_START);
    const family = k >> 1; // 0 = inject, 1 = flush
    if (isOpen) {
      t0.push(raw.t[i]);
      t1.push(NaN);
      kind.push(k);
      requestedS.push(raw.requestedOnUs[i] / US_PER_S);
      openIdx.set(family, t0.length - 1);
      starts.push(i);
    } else {
      const at = openIdx.get(family);
      if (at !== undefined) {
        t1[at] = raw.t[i];
        openIdx.delete(family);
      }
    }
  }
  for (let i = 0; i < t1.length; ++i) {
    if (Number.isNaN(t1[i])) t1[i] = t0[i] + requestedS[i];
  }
  return {
    n: t0.length,
    t0: Float64Array.from(t0),
    t1: Float64Array.from(t1),
    kind: Uint8Array.from(kind),
    requestedS: Float32Array.from(requestedS)
  };
}

/**
 * Build a telemetry model from raw flash-log bytes.
 *
 * @param {ArrayBuffer|Uint8Array} input Raw `.bin` download bytes.
 * @param {{onProgress?: (fraction:number, message:string)=>void}} [opts]
 * @returns {Object} TelemetryModel
 */
export function buildTelemetry(input, opts = {}) {
  const report = opts.onProgress || (() => {});
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const start = parseDclgHeader(bytes) ? LOG_DCLG_HEADER_LEN : 0;

  report(0.05, 'Scanning records…');
  const scan = scanStream(bytes, view, start);
  const durationS = assignEpochOffsets(scan.epochs);
  const offsetForEpoch = scan.epochs.map((e) => e.offsetS);

  report(0.3, 'Allocating channels…');

  /* ---- Allocate tables ---- */
  const tables = {};
  for (const decl of TABLES) {
    const ids = decl.perCell
      ? Array.from({ length: CELL_COUNT }, (_, c) => `${decl.id}_c${c}`)
      : [decl.id];
    for (const id of ids) {
      const n = scan.counts.get(id) || 0;
      if (n === 0) continue;
      const cellIndex = decl.perCell ? Number(id.slice(id.lastIndexOf('_c') + 2)) : null;
      const channels = {};
      for (const f of decl.fields) {
        channels[f.key] = { def: f, data: new Float32Array(n) };
      }
      tables[id] = {
        id,
        tableId: decl.id,
        type: decl.type,
        cellIndex,
        label: decl.perCell ? `${decl.label} — cell ${cellIndex}` : decl.label,
        n,
        fill: 0,
        time: new Float64Array(n),
        bootTime: new Float64Array(n),
        epoch: new Uint8Array(n),
        channels
      };
    }
  }

  /* ---- Allocate event arrays ---- */
  const ec = scan.eventCounts;
  const solRaw = {
    n: 0,
    t: new Float64Array(ec.solenoid),
    kind: new Uint8Array(ec.solenoid),
    requestedOnUs: new Uint32Array(ec.solenoid),
    offUs: new Uint32Array(ec.solenoid)
  };
  const errors = {
    n: 0,
    t: new Float64Array(ec.error),
    code: new Uint16Array(ec.error),
    detail: new Uint32Array(ec.error)
  };
  const drops = {
    n: 0,
    t: new Float64Array(ec.drop),
    count: new Uint32Array(ec.drop),
    lastType: new Uint8Array(ec.drop)
  };
  const solCurrent = {
    n: 0,
    t: new Float64Array(ec.solCurrent),
    role: new Uint8Array(ec.solCurrent),
    classification: new Uint8Array(ec.solCurrent),
    baselineUa: new Int32Array(ec.solCurrent),
    fireUa: new Int32Array(ec.solCurrent),
    deltaUa: new Int32Array(ec.solCurrent)
  };
  const markers = [];
  /** Records whose header carried FL_ENTRY_FLAG_DROP_PRECEDED. */
  const dropFlagged = [];

  report(0.35, 'Decoding payloads…');

  let epochIdx = 0;
  let seenFirst = false;
  let processed = 0;
  const reportEvery = Math.max(1, Math.floor(scan.total / 20));

  walkStream(bytes, view, start, (type, flags, tsUs, off, len) => {
    if (type === FL_TYPE_BOOT_MARKER && seenFirst) ++epochIdx;
    seenFirst = true;
    const t = tsUs / US_PER_S + offsetForEpoch[Math.min(epochIdx, offsetForEpoch.length - 1)];

    if ((flags & 1) === 1) dropFlagged.push(t);

    switch (type) {
      case FL_TYPE_BOOT_MARKER: {
        const d = decodeBootMarker(bytes.subarray(off, off + len));
        markers.push({ kind: 'boot', t, epoch: epochIdx, ...d });
        break;
      }
      case FL_TYPE_DIVE_START:
      case FL_TYPE_DIVE_END: {
        const d = decodeDiveMarker(bytes.subarray(off, off + len));
        markers.push({
          kind: type === FL_TYPE_DIVE_START ? 'diveStart' : 'diveEnd',
          t, epoch: epochIdx, ...d
        });
        break;
      }
      case FL_TYPE_SOLENOID_FIRE: {
        const i = solRaw.n++;
        solRaw.t[i] = t;
        solRaw.kind[i] = bytes[off];
        solRaw.requestedOnUs[i] = READERS.u32(bytes, off + 1);
        solRaw.offUs[i] = READERS.u32(bytes, off + 5);
        break;
      }
      case FL_TYPE_SOLENOID_CURRENT: {
        const i = solCurrent.n++;
        solCurrent.t[i] = t;
        solCurrent.role[i] = bytes[off];
        solCurrent.classification[i] = bytes[off + 1];
        solCurrent.baselineUa[i] = READERS.i32(bytes, off + 2);
        solCurrent.fireUa[i] = READERS.i32(bytes, off + 6);
        solCurrent.deltaUa[i] = READERS.i32(bytes, off + 10);
        break;
      }
      case FL_TYPE_ERROR_EVENT: {
        const i = errors.n++;
        errors.t[i] = t;
        errors.code[i] = READERS.u32(bytes, off);
        errors.detail[i] = READERS.u32(bytes, off + 4);
        break;
      }
      case FL_TYPE_DROP_MARKER: {
        const i = drops.n++;
        drops.t[i] = t;
        drops.count[i] = READERS.u32(bytes, off);
        drops.lastType[i] = bytes[off + 4];
        break;
      }
      default: break;
    }

    const decl = tableForType(type);
    if (decl !== null && len >= decl.payloadLen) {
      const id = decl.perCell ? `${decl.id}_c${bytes[off + decl.cellOff]}` : decl.id;
      const table = tables[id];
      if (table !== undefined) {
        const i = table.fill++;
        table.time[i] = t;
        table.bootTime[i] = tsUs / US_PER_S;
        table.epoch[i] = epochIdx;
        for (const f of decl.fields) {
          if (f.derived) continue; // filled after the surface datum is known
          let raw = READERS[f.read](bytes, off + f.off);
          if (f.bits) raw = (raw >> f.bits.shift) & f.bits.mask;
          let valid = true;
          if (f.validBit !== undefined) {
            valid = (bytes[off + decl.flagsOff] & f.validBit) !== 0;
          }
          if (f.validWhen === 'positive') valid = valid && raw > 0;
          table.channels[f.key].data[i] = valid ? raw * f.scale : NaN;
        }
      }
    }

    if ((++processed % reportEvery) === 0) {
      report(0.35 + 0.5 * (processed / scan.total), 'Decoding payloads…');
    }
  });

  report(0.88, 'Ordering samples…');
  let resorted = 0;
  for (const table of Object.values(tables)) resorted += sortTableByTime(table);

  derivePowerChannels(tables);

  report(0.93, 'Deriving depth…');
  const surfaceMbar = estimateSurfaceMbar(tables);
  if (surfaceMbar !== null) applySurfaceReference({ tables }, surfaceMbar);

  report(0.97, 'Building event overlay…');
  const solenoid = buildSolenoidSpans(solRaw);
  const dives = buildDiveWindows(markers, scan.epochs, durationS);
  const diveStart = markers
    .filter((m) => (m.semanticKind || m.kind) === 'diveStart')
    .sort((a, b) => a.t - b.t)[0] || null;

  return {
    meta: {
      totalRecords: scan.total,
      byType: Object.fromEntries(
        [...scan.byType].map(([k, v]) => [FL_TYPE_NAMES[k] || `0x${k.toString(16)}`, v])
      ),
      byTypeCode: Object.fromEntries(scan.byType),
      durationS,
      dives,
      epochs: scan.epochs.map((e) => ({
        index: e.index,
        startS: e.startS,
        spanS: e.spanS,
        bootRelStartS: e.minUs / US_PER_S,
        bootRelEndS: e.maxUs / US_PER_S,
        boot: e.boot
      })),
      surfaceMbar,
      surfaceLsbPerMbar: DIVEO2_PRESSURE_LSB_PER_MBAR,
      // Anchor for absolute wall-clock display: the first semantic dive-start
      // marker pinned to its global position. Reversed-marker logs therefore
      // anchor from their recorded DIVE_END rather than their physical end.
      anchor: diveStart
        ? { unix: diveStart.unixTimestamp, atS: diveStart.t, from: `dive ${diveStart.diveNumber} start` }
        : null,
      resortedTables: resorted,
      dropFlaggedRecords: dropFlagged.length
    },
    tables,
    events: { solenoid, solenoidRaw: solRaw, errors, drops, solCurrent, markers, dropFlagged }
  };
}

/**
 * Collect the transferable ArrayBuffers in a model so it can cross a worker
 * boundary without a structured-clone copy.
 * @param {Object} model
 * @returns {ArrayBuffer[]}
 */
export function transferablesOf(model) {
  const out = [];
  for (const table of Object.values(model.tables)) {
    out.push(table.time.buffer, table.bootTime.buffer, table.epoch.buffer);
    for (const ch of Object.values(table.channels)) out.push(ch.data.buffer);
  }
  for (const group of [model.events.solenoid, model.events.solenoidRaw,
    model.events.errors, model.events.drops, model.events.solCurrent]) {
    for (const v of Object.values(group)) {
      if (ArrayBuffer.isView(v)) out.push(v.buffer);
    }
  }
  return [...new Set(out)];
}
