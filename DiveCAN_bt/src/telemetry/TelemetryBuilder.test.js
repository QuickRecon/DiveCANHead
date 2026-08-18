/**
 * Telemetry decode + model-building tests.
 *
 * Covers the decoders added to LogParser for the viewer, the boot-epoch
 * segmentation and global time axis, the derived depth channel, solenoid span
 * pairing, and the viewport decimation invariants the plot depends on.
 */

import { describe, it, expect } from 'vitest';
import {
  buildRecord, buildStream, bootMarkerPayload, diveMarkerPayload,
  consensusPayload, pidPayload, solenoidFirePayload, solenoidCurrentPayload,
  cellDiveO2Payload, cellO2SPayload, cellAnalogPayload, errorEventPayload,
  dropMarkerPayload
} from '../../tests/fixtures/log-streams.js';
import {
  decodeConsensus, decodePidSnapshot, decodeSolenoidFire, decodeSolenoidCurrent,
  decodeCellDiveO2, decodeCellO2S, decodeCellAnalog, decodeErrorEvent,
  decodeDropMarker, decodeBootMarker, decodeRecord, unpackConsensusStatus,
  consensusStatusArray, consensusIncludeArray
} from '../logs/LogParser.js';
import { buildTelemetry, applySurfaceReference, EPOCH_GAP_S } from './TelemetryBuilder.js';
import {
  decimateChannel, buildGrid, lowerBound, nearestIndex, medianInterval,
  bucketCountFor
} from './TelemetrySeries.js';
import { formatElapsed } from './TelemetryModel.js';
import {
  FL_TYPE_BOOT_MARKER, FL_TYPE_DIVE_START, FL_TYPE_DIVE_END,
  FL_TYPE_CONSENSUS, FL_TYPE_PID_SNAPSHOT, FL_TYPE_SOLENOID_FIRE,
  FL_TYPE_SOLENOID_CURRENT, FL_TYPE_CELL_RAW_DIVEO2, FL_TYPE_CELL_RAW_O2S,
  FL_TYPE_CELL_RAW_ANALOG, FL_TYPE_ERROR_EVENT, FL_TYPE_DROP_MARKER,
  FL_CRASH_MAGIC
} from '../uds/constants.js';

const U8 = (arr) => new Uint8Array(arr);
const S = 1_000_000; // microseconds per second

describe('payload decoders', () => {
  it('decodes CONSENSUS with raw firmware field names and order', () => {
    const p = U8(consensusPayload({
      consensusPpo2: 69, ppo2: [70, 68, 0], millivolts: [1234, 0, 5678],
      statusPacked: 164, confidence: 2, setpoint: 70
    }));
    expect(decodeConsensus(p)).toEqual({
      consensusPpo2: 69,
      ppo2: [70, 68, 0],
      millivolts: [1234, 0, 5678],
      statusPacked: 164,
      confidence: 2,
      setpoint: 70
    });
  });

  it('unpacks the CONSENSUS status/include bitfield', () => {
    // 164 = 0b0_1010_0100: cell0 OK+included, cell1 OK+included, cell2 FAIL+excluded.
    expect(unpackConsensusStatus(164)).toEqual([
      { status: 0, include: true },
      { status: 0, include: true },
      { status: 2, include: false }
    ]);
    expect(consensusStatusArray(164)).toEqual([0, 0, 2]);
    expect(consensusIncludeArray(164)).toEqual([1, 1, 0]);
  });

  it('round-trips every include bit independently', () => {
    // Bit 8 lives in the second byte; a 16-bit read is required to see it.
    expect(consensusIncludeArray(1 << 8)).toEqual([0, 0, 1]);
    expect(consensusStatusArray((3 << 0) | (2 << 3) | (1 << 6))).toEqual([3, 2, 1]);
  });

  it('decodes PID_SNAPSHOT floats from unaligned offsets', () => {
    const d = decodePidSnapshot(U8(pidPayload({
      integral: 0.25, saturationCount: 7, duty: 0.0034977, setpoint: 70
    })));
    expect(d.integral).toBeCloseTo(0.25, 6);
    expect(d.saturationCount).toBe(7);
    expect(d.duty).toBeCloseTo(0.0034977, 6);
    expect(d.setpoint).toBe(70);
  });

  it('decodes SOLENOID_FIRE', () => {
    expect(decodeSolenoidFire(U8(solenoidFirePayload(1, 256000, 4744000))))
      .toEqual({ kind: 1, requestedOnUs: 256000, offUs: 4744000 });
  });

  it('decodes SOLENOID_CURRENT with signed microamps', () => {
    expect(decodeSolenoidCurrent(U8(solenoidCurrentPayload(1, 2, -500, 120000, 120500))))
      .toEqual({ role: 1, classification: 2, baselineUa: -500, fireUa: 120000, deltaUa: 120500 });
  });

  it('decodes CELL_RAW_DIVEO2 raw fields and SI conversions', () => {
    const d = decodeCellDiveO2(U8(cellDiveO2Payload({
      cellIndex: 1, ppo2: 69, temperatureDc: 22921, errCode: 0, phase: 8832,
      intensity: 131503, ambientLight: -12, pressureUhpa: 1013250, humidityMrh: 45210
    })));
    expect(d.cellIndex).toBe(1);
    expect(d.temperatureDc).toBe(22921);
    expect(d.ambientLight).toBe(-12);        // signed field
    expect(d.ppo2Bar).toBeCloseTo(0.69, 6);
    // The _dc / _uhpa field names are misnomers: both are milli-units.
    expect(d.temperatureC).toBeCloseTo(22.921, 6);
    expect(d.pressureMbar).toBeCloseTo(1013.25, 6);
    expect(d.humidityPct).toBeCloseTo(45.21, 6);
  });

  it('decodes CELL_RAW_O2S and CELL_RAW_ANALOG', () => {
    expect(decodeCellO2S(U8(cellO2SPayload(2, 71, 0))))
      .toEqual({ cellIndex: 2, ppo2: 71, status: 0, ppo2Bar: 0.71 });
    const a = decodeCellAnalog(U8(cellAnalogPayload(0, 70, -12345, 4520)));
    expect(a.rawAdc).toBe(-12345);
    expect(a.mv).toBeCloseTo(45.2, 6);
  });

  it('decodes ERROR_EVENT and DROP_MARKER', () => {
    expect(decodeErrorEvent(U8(errorEventPayload(3, 0x40000010))))
      .toEqual({ code: 3, detail: 0x40000010 });
    expect(decodeDropMarker(U8(dropMarkerPayload(17, 0x11))))
      .toEqual({ count: 17, lastDroppedType: 0x11 });
  });

  it('surfaces a previous-boot crash record only when the magic matches', () => {
    const crash = { magic: FL_CRASH_MAGIC, reason: 2, pc: 0x080273dc, lr: 0x86944a91 };
    expect(decodeBootMarker(U8(bootMarkerPayload(184, 'fw', 3, crash))).prevCrash)
      .toEqual(crash);
    const clean = { magic: 0, reason: 0, pc: 0, lr: 0 };
    expect(decodeBootMarker(U8(bootMarkerPayload(183, 'fw', 0x80, clean))).prevCrash)
      .toBeNull();
    // A 24-byte payload predates the crash fields and must still decode.
    expect(decodeBootMarker(U8(bootMarkerPayload(1, 'fw', 0))).prevCrash).toBeNull();
  });

  it('returns null for every truncated payload', () => {
    expect(decodeConsensus(U8(new Array(13).fill(0)))).toBeNull();
    expect(decodePidSnapshot(U8(new Array(10).fill(0)))).toBeNull();
    expect(decodeSolenoidFire(U8(new Array(8).fill(0)))).toBeNull();
    expect(decodeSolenoidCurrent(U8(new Array(13).fill(0)))).toBeNull();
    expect(decodeCellDiveO2(U8(new Array(29).fill(0)))).toBeNull();
    expect(decodeCellO2S(U8(new Array(2).fill(0)))).toBeNull();
    expect(decodeCellAnalog(U8(new Array(7).fill(0)))).toBeNull();
    expect(decodeErrorEvent(U8(new Array(7).fill(0)))).toBeNull();
    expect(decodeDropMarker(U8(new Array(4).fill(0)))).toBeNull();
  });

  it('dispatches every new type through decodeRecord', () => {
    const cases = [
      [FL_TYPE_PID_SNAPSHOT, pidPayload()],
      [FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(0, 1, 2)],
      [FL_TYPE_SOLENOID_CURRENT, solenoidCurrentPayload(0, 0, 0, 0, 0)],
      [FL_TYPE_CELL_RAW_DIVEO2, cellDiveO2Payload()],
      [FL_TYPE_CELL_RAW_O2S, cellO2SPayload(0, 0, 0)],
      [FL_TYPE_CELL_RAW_ANALOG, cellAnalogPayload(0, 0, 0, 0)],
      [FL_TYPE_ERROR_EVENT, errorEventPayload(1, 2)],
      [FL_TYPE_DROP_MARKER, dropMarkerPayload(1, 2)]
    ];
    for (const [type, payload] of cases) {
      expect(decodeRecord({ type, payload: U8(payload) })).not.toBeNull();
    }
  });
});

describe('buildTelemetry', () => {
  /** A two-epoch stream: some records, a reboot, then more records. */
  function twoEpochStream() {
    return buildStream([
      buildRecord(FL_TYPE_CONSENSUS,
        consensusPayload({ consensusPpo2: 70, setpoint: 70, statusPacked: 164, confidence: 2 }),
        { tsUs: 100 * S }),
      buildRecord(FL_TYPE_CONSENSUS,
        consensusPayload({ consensusPpo2: 72, setpoint: 70, statusPacked: 164, confidence: 2 }),
        { tsUs: 110 * S }),
      buildRecord(FL_TYPE_CELL_RAW_DIVEO2,
        cellDiveO2Payload({ cellIndex: 0, ppo2: 70, pressureUhpa: 1013000, temperatureDc: 20000 }),
        { tsUs: 100 * S }),
      buildRecord(FL_TYPE_CELL_RAW_DIVEO2,
        cellDiveO2Payload({ cellIndex: 1, ppo2: 71, pressureUhpa: 1613000, temperatureDc: 19000 }),
        { tsUs: 105 * S }),
      buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(9, 'fw1', 3), { tsUs: 2 * S }),
      buildRecord(FL_TYPE_CONSENSUS,
        consensusPayload({ consensusPpo2: 68, setpoint: 140, statusPacked: 164, confidence: 2 }),
        { tsUs: 5 * S })
    ]);
  }

  it('segments boot epochs and lays them end to end on a global axis', () => {
    const m = buildTelemetry(twoEpochStream());
    expect(m.meta.epochs).toHaveLength(2);

    const [first, second] = m.meta.epochs;
    // Epoch 0 starts mid-ring at ts 100 s and runs to 110 s.
    expect(first.startS).toBe(0);
    expect(first.spanS).toBeCloseTo(10, 6);
    expect(first.bootRelStartS).toBeCloseTo(100, 6);
    expect(first.boot).toBeNull();
    // Epoch 1 begins one gap after epoch 0 ends, not at its own ts_boot_us.
    expect(second.startS).toBeCloseTo(10 + EPOCH_GAP_S, 6);
    expect(second.boot.bootId).toBe(9);

    // Consensus samples land on the global axis, in order, across the reboot.
    const t = m.tables.consensus.time;
    expect(Array.from(t)).toEqual([0, 10, 10 + EPOCH_GAP_S + 3]);
  });

  it('splits per-cell record types into one table per cell index', () => {
    const m = buildTelemetry(twoEpochStream());
    expect(Object.keys(m.tables).sort()).toEqual(['consensus', 'diveo2_c0', 'diveo2_c1']);
    expect(m.tables.diveo2_c0.n).toBe(1);
    expect(m.tables.diveo2_c1.n).toBe(1);
    expect(m.tables.diveo2_c1.cellIndex).toBe(1);
  });

  it('applies unit scales and derives depth from the surface reference', () => {
    const m = buildTelemetry(twoEpochStream());
    const c0 = m.tables.diveo2_c0;
    expect(c0.channels.ppo2.data[0]).toBeCloseTo(0.70, 6);
    expect(c0.channels.temperature.data[0]).toBeCloseTo(20.0, 6);
    expect(c0.channels.pressureMbar.data[0]).toBeCloseTo(1013.0, 3);

    // Surface reference is the low percentile of observed pressure — here the
    // shallower of the two samples — so cell 0 sits at the datum.
    expect(m.meta.surfaceMbar).toBeCloseTo(1013.0, 3);
    expect(c0.channels.depth.data[0]).toBeCloseTo(0, 3);
    expect(m.tables.diveo2_c1.channels.depth.data[0]).toBeCloseTo(6.0, 3);
  });

  it('re-datums depth when the surface reference is overridden', () => {
    const m = buildTelemetry(twoEpochStream());
    applySurfaceReference(m, 913.0);
    expect(m.meta.surfaceMbar).toBe(913.0);
    expect(m.tables.diveo2_c0.channels.depth.data[0]).toBeCloseTo(1.0, 3);
  });

  it('unpacks consensus status/include bits into per-cell channels', () => {
    const m = buildTelemetry(twoEpochStream());
    const ch = m.tables.consensus.channels;
    expect(ch.status_c0.data[0]).toBe(0);
    expect(ch.status_c2.data[0]).toBe(2);
    expect(ch.include_c0.data[0]).toBe(1);
    expect(ch.include_c2.data[0]).toBe(0);
  });

  it('pairs solenoid open/close records into spans', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(0, 300000, 0), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(1, 300000, 0), { tsUs: 1.3 * S }),
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(2, 900000, 0), { tsUs: 5 * S }),
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(3, 900000, 0), { tsUs: 5.9 * S })
    ]));
    const s = m.events.solenoid;
    expect(s.n).toBe(2);
    expect(s.t1[0] - s.t0[0]).toBeCloseTo(0.3, 5);
    expect(s.kind[0]).toBe(0);
    expect(s.kind[1]).toBe(2);
    expect(s.requestedS[1]).toBeCloseTo(0.9, 5);
  });

  it('closes an unmatched solenoid open at its requested duration', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(0, 250000, 0), { tsUs: 1 * S })
    ]));
    expect(m.events.solenoid.n).toBe(1);
    expect(m.events.solenoid.t1[0] - m.events.solenoid.t0[0]).toBeCloseTo(0.25, 5);
  });

  it('ignores a close with no matching open (ring starts mid-fire)', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(1, 250000, 0), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(0, 250000, 0), { tsUs: 2 * S }),
      buildRecord(FL_TYPE_SOLENOID_FIRE, solenoidFirePayload(1, 250000, 0), { tsUs: 2.2 * S })
    ]));
    expect(m.events.solenoid.n).toBe(1);
    expect(m.events.solenoid.t0[0]).toBeCloseTo(1, 5);
  });

  it('collects errors, drops and markers as discrete events', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_DIVE_START, diveMarkerPayload(35, 1786795552), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_ERROR_EVENT, errorEventPayload(3, 11), { tsUs: 2 * S }),
      buildRecord(FL_TYPE_ERROR_EVENT, errorEventPayload(3, 114), { tsUs: 3 * S }),
      buildRecord(FL_TYPE_DROP_MARKER, dropMarkerPayload(42, 0x11), { tsUs: 4 * S }),
      buildRecord(FL_TYPE_DIVE_END, diveMarkerPayload(35, 1786799000), { tsUs: 5 * S })
    ]));
    expect(m.events.errors.n).toBe(2);
    expect(Array.from(m.events.errors.code)).toEqual([3, 3]);
    expect(Array.from(m.events.errors.detail)).toEqual([11, 114]);
    expect(m.events.drops.n).toBe(1);
    expect(m.events.drops.count[0]).toBe(42);
    expect(m.events.drops.lastType[0]).toBe(0x11);
    expect(m.events.markers.map((x) => x.kind)).toEqual(['diveStart', 'diveEnd']);
    // The first dive marker anchors absolute wall-clock time.
    expect(m.meta.anchor.unix).toBe(1786795552);
    expect(m.meta.anchor.atS).toBeCloseTo(0, 6);
  });

  it('turns normal dive markers into whole-dive and per-boot windows', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_DIVE_START, diveMarkerPayload(7, 1700000000), { tsUs: 10 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 20 * S }),
      buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(12, 'fw', 0), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 10 * S }),
      buildRecord(FL_TYPE_DIVE_END, diveMarkerPayload(7, 1700000060), { tsUs: 15 * S })
    ]));

    expect(m.meta.dives).toHaveLength(1);
    const dive = m.meta.dives[0];
    expect(dive).toMatchObject({ diveNumber: 7, complete: true, markerOrder: 'normal' });
    expect(dive.boots).toHaveLength(2);
    expect(dive.boots[0]).toMatchObject({ epochIndex: 0, bootId: null });
    expect(dive.boots[1]).toMatchObject({ epochIndex: 1, bootId: 12 });
    expect(m.events.markers.filter((x) => x.kind !== 'boot').map((x) => x.semanticKind))
      .toEqual(['diveStart', 'diveEnd']);
  });

  it('corrects reversed dive markers without discarding the recorded kinds', () => {
    const m = buildTelemetry(buildStream([
      // The observed reversed convention records the physical start as DIVE_END.
      buildRecord(FL_TYPE_DIVE_END, diveMarkerPayload(35, 1786789920), { tsUs: 10 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 20 * S }),
      buildRecord(FL_TYPE_BOOT_MARKER, bootMarkerPayload(184, 'old-fw', 3), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 10 * S }),
      // And the physical end as DIVE_START.
      buildRecord(FL_TYPE_DIVE_START, diveMarkerPayload(35, 1786795552), { tsUs: 15 * S })
    ]));

    const dive = m.meta.dives[0];
    expect(dive).toMatchObject({ diveNumber: 35, complete: true, markerOrder: 'reversed' });
    expect(dive.boots).toHaveLength(2);
    const diveMarkers = m.events.markers.filter((x) => x.kind !== 'boot');
    expect(diveMarkers.map((x) => x.kind)).toEqual(['diveEnd', 'diveStart']);
    expect(diveMarkers.map((x) => x.semanticKind)).toEqual(['diveStart', 'diveEnd']);
    // Absolute time must be pinned to the physical start, not the reversed
    // record that happens to be named DIVE_START.
    expect(m.meta.anchor).toMatchObject({
      unix: 1786789920,
      from: 'dive 35 start'
    });
    expect(m.meta.anchor.atS).toBeCloseTo(dive.t0, 6);
  });

  it('uses paired marker order to expose an incomplete ring-edge dive safely', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_DIVE_END, diveMarkerPayload(4, 1000), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_DIVE_START, diveMarkerPayload(4, 1100), { tsUs: 5 * S }),
      // A lone reversed DIVE_END is a physical start. Another complete pair
      // pair provides the evidence needed to extend it to the log edge.
      buildRecord(FL_TYPE_DIVE_END, diveMarkerPayload(5, 1200), { tsUs: 10 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 20 * S })
    ]));

    expect(m.meta.dives).toHaveLength(2);
    expect(m.meta.dives[1]).toMatchObject({
      diveNumber: 5,
      complete: false,
      markerOrder: 'reversed',
      t1: m.meta.durationS
    });
    const lone = m.events.markers.find((x) => x.diveNumber === 5);
    expect(lone).toMatchObject({
      kind: 'diveEnd',
      semanticKind: 'diveStart',
      markerOrderInferred: true
    });
  });

  it('does not invent a dive window from a lone marker of unknown polarity', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_DIVE_END, diveMarkerPayload(9, 1000), { tsUs: 1 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload(), { tsUs: 10 * S })
    ]));
    expect(m.meta.dives).toEqual([]);
    expect(m.events.markers[0].semanticKind).toBeUndefined();
  });

  it('sorts a table whose samples arrive out of order', () => {
    // Batched flushes can emit a record slightly out of order; both uPlot and
    // the cursor binary search require monotonic time.
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload({ consensusPpo2: 10 }), { tsUs: 30 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload({ consensusPpo2: 20 }), { tsUs: 10 * S }),
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload({ consensusPpo2: 30 }), { tsUs: 20 * S })
    ]));
    expect(Array.from(m.tables.consensus.time)).toEqual([0, 10, 20]);
    // Channel values must be permuted alongside the times, not just sorted.
    expect(Array.from(m.tables.consensus.channels.consensusPpo2.data.map((v) => Math.round(v * 100))))
      .toEqual([20, 30, 10]);
    expect(m.meta.resortedTables).toBe(1);
  });

  it('counts records by type and flattens nothing it should not', () => {
    const m = buildTelemetry(buildStream([
      buildRecord(FL_TYPE_CONSENSUS, consensusPayload()),
      buildRecord(FL_TYPE_PID_SNAPSHOT, pidPayload()),
      buildRecord(FL_TYPE_CELL_RAW_O2S, cellO2SPayload(0, 0, 2)),
      buildRecord(FL_TYPE_CELL_RAW_ANALOG, cellAnalogPayload(0, 70, 100, 4500))
    ]));
    expect(m.meta.totalRecords).toBe(4);
    expect(m.meta.byTypeCode[FL_TYPE_CONSENSUS]).toBe(1);
    expect(m.tables.o2s_c0.n).toBe(1);
    expect(m.tables.analog_c0.channels.millivolts.data[0]).toBeCloseTo(45.0, 6);
  });
});

describe('viewport decimation', () => {
  const time = Float64Array.from({ length: 1000 }, (_, i) => i * 0.1);
  const values = Float32Array.from({ length: 1000 }, (_, i) => Math.sin(i / 10));

  it('preserves the exact min and max of every bucket', () => {
    const buckets = 20;
    const out = decimateChannel(time, values, 1000, 0, 100, buckets, 0.4);
    let rawMin = Infinity;
    let rawMax = -Infinity;
    for (let i = 0; i < 1000; ++i) {
      rawMin = Math.min(rawMin, values[i]);
      rawMax = Math.max(rawMax, values[i]);
    }
    const drawn = out.filter((v) => v !== null);
    expect(Math.min(...drawn)).toBe(rawMin);
    expect(Math.max(...drawn)).toBe(rawMax);
  });

  it('emits one value per grid slot', () => {
    const buckets = 32;
    expect(decimateChannel(time, values, 1000, 0, 100, buckets, 0.4))
      .toHaveLength(buckets * 2);
    expect(buildGrid(0, 100, buckets)).toHaveLength(buckets * 2);
  });

  it('renders a real gap as null rather than bridging it', () => {
    // Two clusters 50 s apart, sampled at 0.1 s within each.
    const t = Float64Array.from([...Array(50).keys()].map((i) => i * 0.1)
      .concat([...Array(50).keys()].map((i) => 50 + i * 0.1)));
    const v = Float32Array.from(t.map(() => 1));
    const out = decimateChannel(t, v, t.length, 0, 60, 60, 0.4);
    // Buckets covering 5 s .. 50 s hold no samples and are far wider than the
    // gap tolerance, so they must stay null.
    for (let b = 10; b < 45; ++b) {
      expect(out[b * 2]).toBeNull();
      expect(out[b * 2 + 1]).toBeNull();
    }
  });

  it('bridges holes narrower than the gap tolerance when zoomed in', () => {
    // 40 buckets over 1 s but samples only every 0.1 s: most buckets are empty
    // yet the line must stay continuous edge to edge, because the trailing
    // bucket is anchored to the first sample past the range before bridging.
    const out = decimateChannel(time, values, 1000, 0, 1, 40, 0.4);
    expect(out.filter((x) => x === null)).toHaveLength(0);
  });

  it('anchors the leading edge to the sample before the range', () => {
    // Window starts between samples: the first bucket must still carry a value
    // so the trace enters from the left rather than starting mid-plot.
    const out = decimateChannel(time, values, 1000, 5.05, 6, 40, 0.4);
    expect(out[0]).not.toBeNull();
  });

  it('returns an all-null column set for an empty series', () => {
    const out = decimateChannel(new Float64Array(0), new Float32Array(0), 0, 0, 10, 8, 1);
    expect(out).toHaveLength(16);
    expect(out.every((v) => v === null)).toBe(true);
  });

  it('clamps the bucket count for degenerate widths', () => {
    expect(bucketCountFor(Number.NaN)).toBeGreaterThan(0);
    expect(bucketCountFor(-5)).toBeGreaterThan(0);
    expect(bucketCountFor(0)).toBeGreaterThan(0);
    expect(bucketCountFor(1e9)).toBeLessThanOrEqual(4000);
  });
});

describe('sample lookup helpers', () => {
  const time = Float64Array.from([0, 1, 2, 3, 4]);

  it('finds the insertion point with lowerBound', () => {
    expect(lowerBound(time, 5, -1)).toBe(0);
    expect(lowerBound(time, 5, 2)).toBe(2);
    expect(lowerBound(time, 5, 2.5)).toBe(3);
    expect(lowerBound(time, 5, 99)).toBe(5);
  });

  it('picks the closest sample with nearestIndex', () => {
    expect(nearestIndex(time, 5, -10)).toBe(0);
    expect(nearestIndex(time, 5, 2.4)).toBe(2);
    expect(nearestIndex(time, 5, 2.6)).toBe(3);
    expect(nearestIndex(time, 5, 100)).toBe(4);
    expect(nearestIndex(time, 0, 1)).toBe(-1);
  });

  it('estimates a sample interval robust to outliers', () => {
    const t = Float64Array.from({ length: 500 }, (_, i) => (i < 250 ? i * 0.5 : 1000 + i * 0.5));
    expect(medianInterval(t, 500)).toBeCloseTo(0.5, 3);
    expect(medianInterval(new Float64Array([0]), 1)).toBe(0);
  });
});

describe('formatElapsed', () => {
  it('formats hours, minutes and seconds', () => {
    expect(formatElapsed(0)).toBe('0:00:00');
    expect(formatElapsed(3661)).toBe('1:01:01');
    expect(formatElapsed(27591)).toBe('7:39:51');
    expect(formatElapsed(-90)).toBe('-0:01:30');
    expect(formatElapsed(1.25, true)).toBe('0:00:01.250');
  });

  it('degrades to a dash for a non-finite time', () => {
    expect(formatElapsed(Number.NaN)).toBe('—');
    expect(formatElapsed(Infinity)).toBe('—');
  });
});
