/**
 * Discrete-event overlay for the telemetry plot.
 *
 * Everything in a flash log that is an *instant* or a *span* rather than a
 * continuous signal is drawn here, as a uPlot plugin that paints under the
 * series (bands, gaps) and over them (markers, rug):
 *
 *  * **Epoch gaps** — the synthetic separation between boot epochs. Time on the
 *    global axis is not real inside these; they are hatched so nobody reads a
 *    trend across a reboot.
 *  * **Drop markers** — the firmware's ingest queue overflowed and `count`
 *    records were lost just before this instant. Drawn as a hatched red band so
 *    a gap in the data is never mistaken for a flat signal.
 *  * **Solenoid fires** — spans from open to close, using the paired
 *    start/end records. A short fire is still at least one pixel wide.
 *  * **Error events** — a colour-coded rug along the bottom. At full zoom a
 *    five-hour log has more errors than pixels, so each column shows the
 *    dominant code and the density is conveyed by opacity.
 *  * **Boot / dive markers** — full-height rules with labels.
 *
 * The plugin owns no state beyond the model; hit-testing for tooltips is
 * exposed via `hitTest` so the host page can render a readout.
 */

import { OP_ERRORS } from '../errors/ErrorHistogram.js';
import { SOL_FIRE_EVT_NAMES, FL_TYPE_NAMES } from '../uds/constants.js';
import { EPOCH_GAP_S } from './TelemetryBuilder.js';
import { formatElapsed } from './TelemetryModel.js';

/** Height of the solenoid band, as a fraction of the plot area. */
const SOLENOID_BAND = 0.06;
/** Height of the error rug, as a fraction of the plot area. */
const ERROR_RUG = 0.035;
/** Height of the drop-marker density strip, as a fraction of the plot area. */
const DROP_STRIP = 0.02;
/** Above this many visible drop markers the full-height hatch is suppressed. */
const DROP_HATCH_LIMIT = 40;
/** Minimum on-screen width for a span or gap, in CSS pixels. */
const MIN_SPAN_PX = 1.5;
/** Pixel radius within which a marker counts as hovered. */
const HIT_RADIUS_PX = 6;

/** Colour per solenoid event kind (open events only; closes end a span). */
const SOLENOID_COLOURS = {
  0: 'rgba(120, 200, 255, 0.55)',  // inject
  2: 'rgba(255, 190, 90, 0.65)'    // setpoint-change flush
};

/** Stable colour per error code, derived from the code so it never shifts. */
export function errorColour(code) {
  const hue = (code * 47) % 360;
  return `hsl(${hue}, 70%, 58%)`;
}

/** Render an OpError_t code as its firmware enum name. */
export function errorName(code) {
  const e = OP_ERRORS[code];
  return e ? `OP_ERR_${e.name}` : `OP_ERR_UNKNOWN(${code})`;
}

/** Render an OpError_t code's one-line description. */
export function errorDescription(code) {
  const e = OP_ERRORS[code];
  return e ? e.description : 'Unrecognised error code';
}

/** Marker styling by kind. */
const MARKER_STYLE = {
  boot: { colour: '#ff7b72', dash: [6, 4], label: 'BOOT' },
  diveStart: { colour: '#7ee787', dash: [], label: 'DIVE START' },
  diveEnd: { colour: '#7ee787', dash: [], label: 'DIVE END' }
};

/**
 * Device-pixel ratio of a uPlot instance.
 *
 * uPlot 1.6 exposes the ratio as a module-level static (`uPlot.pxRatio`), not
 * as an instance property — reading `u.pxRatio` yields `undefined`, and any
 * arithmetic on it silently poisons a whole overlay with `NaN` widths. Derive
 * it from the canvas instead, which is true regardless of uPlot version.
 *
 * @param {Object} u uPlot instance
 * @returns {number} device pixels per CSS pixel, never non-finite
 */
export function pxRatioOf(u) {
  const canvas = u.ctx && u.ctx.canvas;
  if (canvas && u.width > 0) {
    const ratio = canvas.width / u.width;
    if (Number.isFinite(ratio) && ratio > 0) return ratio;
  }
  return 1;
}

/**
 * Compute the on-canvas x for a time value.
 * @private
 */
function xOf(u, t) {
  return u.valToPos(t, 'x', true);
}

/**
 * Fill a hatched rectangle (used for anything representing absent data).
 * @private
 */
function hatch(ctx, x, y, w, h, colour) {
  ctx.save();
  ctx.beginPath();
  ctx.rect(x, y, w, h);
  ctx.clip();
  ctx.fillStyle = colour;
  ctx.fillRect(x, y, w, h);
  ctx.strokeStyle = colour;
  ctx.lineWidth = 1;
  ctx.globalAlpha = 0.9;
  const step = 6;
  for (let i = -h; i < w + h; i += step) {
    ctx.beginPath();
    ctx.moveTo(x + i, y + h);
    ctx.lineTo(x + i + h, y);
    ctx.stroke();
  }
  ctx.restore();
}

/**
 * Build the uPlot plugin.
 *
 * @param {Object} model TelemetryModel from TelemetryBuilder
 * @param {{show: Object}} opts `show` toggles each overlay layer by name
 * @returns {Object} uPlot plugin
 */
export function eventOverlayPlugin(model, opts) {
  const show = opts.show;

  const drawEpochGaps = (u, ctx, L, T, W, H) => {
    const dpr = pxRatioOf(u);
    for (let i = 1; i < model.meta.epochs.length; ++i) {
      const prev = model.meta.epochs[i - 1];
      const gapStart = prev.startS + prev.spanS;
      const a = xOf(u, gapStart);
      const b = xOf(u, gapStart + EPOCH_GAP_S);
      if (b < L || a > L + W) continue;
      const x = Math.max(L, a);
      const w = Math.max(MIN_SPAN_PX * dpr, Math.min(L + W, b) - x);
      hatch(ctx, x, T, w, H, 'rgba(90, 90, 110, 0.35)');
    }
  };

  /**
   * Drop markers, drawn two ways depending on how many are in view.
   *
   * A full-height hatch per marker is the right signal when you can count them,
   * but this firmware emits one per flush that overflowed — a bad stretch
   * produces hundreds, and at full extent they paint over the traces they exist
   * to qualify. So: always draw a density strip along the top (presence and
   * severity are never hidden), and add the full-height hatch only once the
   * view is zoomed enough that individual gaps mean something.
   */
  const drawDrops = (u, ctx, L, T, W, H) => {
    const dpr = pxRatioOf(u);
    const d = model.events.drops;
    if (d.n === 0) return;

    const visible = [];
    for (let i = 0; i < d.n; ++i) {
      const px = xOf(u, d.t[i]);
      if (px >= L - 4 && px <= L + W + 4) visible.push({ px, count: d.count[i] });
    }
    if (visible.length === 0) return;

    ctx.save();
    const stripH = H * DROP_STRIP;
    ctx.fillStyle = 'rgba(255, 255, 255, 0.04)';
    ctx.fillRect(L, T, W, stripH);
    let heaviest = 1;
    for (const v of visible) heaviest = Math.max(heaviest, v.count);
    for (const v of visible) {
      ctx.globalAlpha = 0.45 + 0.55 * Math.min(1, v.count / heaviest);
      ctx.fillStyle = 'rgb(255, 90, 90)';
      ctx.fillRect(v.px - dpr, T, MIN_SPAN_PX * dpr, stripH);
    }
    ctx.restore();

    if (visible.length <= DROP_HATCH_LIMIT) {
      for (const v of visible) {
        hatch(ctx, v.px - MIN_SPAN_PX * dpr, T, MIN_SPAN_PX * 2 * dpr, H,
          'rgba(255, 90, 90, 0.30)');
      }
    }
  };

  const drawSolenoid = (u, ctx, L, T, W, H) => {
    const dpr = pxRatioOf(u);
    const s = model.events.solenoid;
    const bandH = H * SOLENOID_BAND;
    const bandY = T + H - bandH;
    ctx.save();
    ctx.fillStyle = 'rgba(255, 255, 255, 0.04)';
    ctx.fillRect(L, bandY, W, bandH);
    for (let i = 0; i < s.n; ++i) {
      const a = xOf(u, s.t0[i]);
      const b = xOf(u, s.t1[i]);
      if (b < L || a > L + W) continue;
      ctx.fillStyle = SOLENOID_COLOURS[s.kind[i]] || SOLENOID_COLOURS[0];
      const x = Math.max(L, a);
      const w = Math.max(MIN_SPAN_PX * dpr, Math.min(L + W, b) - x);
      ctx.fillRect(x, bandY, w, bandH);
    }
    ctx.restore();
  };

  const drawErrorRug = (u, ctx, L, T, W, H) => {
    const e = model.events.errors;
    if (e.n === 0) return;
    const rugH = H * ERROR_RUG;
    const rugY = T + H - H * SOLENOID_BAND - rugH - 2;
    const cols = Math.max(1, Math.round(W));
    // Per-column tally so a dense log renders as density, not a solid block.
    const counts = new Int32Array(cols);
    const dominant = new Int32Array(cols).fill(-1);
    const domCount = new Int32Array(cols);
    const perCol = new Map();
    let maxCount = 1;
    for (let i = 0; i < e.n; ++i) {
      const px = xOf(u, e.t[i]);
      const c = Math.floor(px - L);
      if (c < 0 || c >= cols) continue;
      ++counts[c];
      if (counts[c] > maxCount) maxCount = counts[c];
      const key = c * 1000 + e.code[i];
      const seen = (perCol.get(key) || 0) + 1;
      perCol.set(key, seen);
      if (seen > domCount[c]) { domCount[c] = seen; dominant[c] = e.code[i]; }
    }
    ctx.save();
    ctx.fillStyle = 'rgba(255, 255, 255, 0.04)';
    ctx.fillRect(L, rugY, W, rugH);
    for (let c = 0; c < cols; ++c) {
      if (counts[c] === 0) continue;
      ctx.globalAlpha = 0.35 + 0.65 * Math.min(1, Math.log1p(counts[c]) / Math.log1p(maxCount));
      ctx.fillStyle = errorColour(dominant[c]);
      ctx.fillRect(L + c, rugY, 1, rugH);
    }
    ctx.restore();
  };

  const drawMarkers = (u, ctx, L, T, W, H) => {
    const dpr = pxRatioOf(u);
    ctx.save();
    ctx.font = `${11 * dpr}px ui-monospace, monospace`;
    ctx.textBaseline = 'top';
    for (const m of model.events.markers) {
      const displayKind = m.semanticKind || m.kind;
      const style = MARKER_STYLE[displayKind];
      if (!style) continue;
      const px = xOf(u, m.t);
      if (px < L || px > L + W) continue;
      ctx.setLineDash(style.dash.map((v) => v * dpr));
      ctx.strokeStyle = style.colour;
      ctx.lineWidth = 1.5 * dpr;
      ctx.beginPath();
      ctx.moveTo(px, T);
      ctx.lineTo(px, T + H);
      ctx.stroke();
      ctx.setLineDash([]);
      const corrected = displayKind !== m.kind;
      const label = m.kind === 'boot' ? `${style.label} ${m.bootId}`
        : `${style.label}${corrected ? '*' : ''}`;
      const tw = ctx.measureText(label).width;
      // Sit clear of the drop-marker density strip that occupies the top edge.
      const labelY = T + H * DROP_STRIP + 3 * dpr;
      ctx.fillStyle = 'rgba(20, 20, 24, 0.85)';
      ctx.fillRect(px + 3 * dpr, labelY, tw + 6 * dpr, 15 * dpr);
      ctx.fillStyle = style.colour;
      ctx.fillText(label, px + 6 * dpr, labelY + 2 * dpr);
    }
    ctx.restore();
  };

  return {
    hooks: {
      // Bands and gaps go under the series so the traces stay readable.
      drawClear: (u) => {
        const ctx = u.ctx;
        const { left: L, top: T, width: W, height: H } = u.bbox;
        if (show.epochGaps) drawEpochGaps(u, ctx, L, T, W, H);
        if (show.drops) drawDrops(u, ctx, L, T, W, H);
      },
      // Markers and the rug go over the series so they are never hidden.
      draw: (u) => {
        const ctx = u.ctx;
        const { left: L, top: T, width: W, height: H } = u.bbox;
        if (show.solenoid) drawSolenoid(u, ctx, L, T, W, H);
        if (show.errors) drawErrorRug(u, ctx, L, T, W, H);
        if (show.markers) drawMarkers(u, ctx, L, T, W, H);
      }
    }
  };
}

/**
 * Find the discrete events near a cursor time.
 *
 * @param {Object} model TelemetryModel
 * @param {number} t Cursor time on the global axis (s)
 * @param {number} tolS Half-width of the search window (s)
 * @returns {{solenoid: Array, errors: Array, drops: Array, markers: Array}}
 */
export function hitTest(model, t, tolS) {
  const out = { solenoid: [], errors: [], drops: [], markers: [] };
  // A non-finite cursor would make every comparison below false and silently
  // match the entire log, so bail before any scan runs.
  if (!Number.isFinite(t) || !Number.isFinite(tolS)) return out;

  const s = model.events.solenoid;
  for (let i = 0; i < s.n; ++i) {
    if (s.t1[i] >= t - tolS && s.t0[i] <= t + tolS) {
      out.solenoid.push({
        kind: s.kind[i],
        kindName: SOL_FIRE_EVT_NAMES[s.kind[i]] || `kind ${s.kind[i]}`,
        t0: s.t0[i],
        t1: s.t1[i],
        actualS: s.t1[i] - s.t0[i],
        requestedS: s.requestedS[i]
      });
      if (out.solenoid.length >= 8) break;
    }
  }

  const e = model.events.errors;
  const tally = new Map();
  for (let i = 0; i < e.n; ++i) {
    if (e.t[i] < t - tolS || e.t[i] > t + tolS) continue;
    const key = `${e.code[i]}:${e.detail[i]}`;
    const prev = tally.get(key);
    if (prev) ++prev.count;
    else tally.set(key, { code: e.code[i], detail: e.detail[i], count: 1, t: e.t[i] });
  }
  out.errors = [...tally.values()].sort((a, b) => b.count - a.count).slice(0, 8)
    .map((x) => ({ ...x, name: errorName(x.code), description: errorDescription(x.code) }));

  const d = model.events.drops;
  for (let i = 0; i < d.n; ++i) {
    if (d.t[i] >= t - tolS && d.t[i] <= t + tolS) {
      out.drops.push({
        t: d.t[i],
        count: d.count[i],
        lastType: d.lastType[i],
        lastTypeName: FL_TYPE_NAMES[d.lastType[i]] || `0x${d.lastType[i].toString(16)}`
      });
      if (out.drops.length >= 8) break;
    }
  }

  for (const m of model.events.markers) {
    if (Math.abs(m.t - t) <= tolS) out.markers.push(m);
  }

  return out;
}

/** Pixel tolerance converted to a time tolerance for hit testing. */
export function hitToleranceS(u) {
  const span = u.scales.x.max - u.scales.x.min;
  const widthCss = Math.max(1, u.bbox.width / pxRatioOf(u));
  return span * (HIT_RADIUS_PX / widthCss);
}

/** Short human label for a marker, used by the readout panel. */
export function markerLabel(m) {
  if (m.kind === 'boot') {
    let s = `Boot ${m.bootId} — fw ${m.fwVersion}, reset cause 0x${m.resetCause.toString(16)}`;
    if (m.prevCrash) {
      s += ` — PREVIOUS BOOT CRASHED (reason ${m.prevCrash.reason}, `
        + `pc 0x${m.prevCrash.pc.toString(16)}, lr 0x${m.prevCrash.lr.toString(16)})`;
    }
    return s;
  }
  const displayKind = m.semanticKind || m.kind;
  const kind = displayKind === 'diveStart' ? 'Dive start' : 'Dive end';
  const corrected = displayKind !== m.kind
    ? ` (recorded as ${m.kind === 'diveStart' ? 'DIVE_START' : 'DIVE_END'})`
    : '';
  const when = m.unixTimestamp
    ? new Date(m.unixTimestamp * 1000).toISOString().replace('T', ' ').slice(0, 19) + 'Z'
    : 'no RTC';
  return `${kind}${corrected} — dive ${m.diveNumber} @ ${when}`;
}

/** Format a time on the global axis for display. */
export function formatMarkerTime(t) {
  return formatElapsed(t);
}
