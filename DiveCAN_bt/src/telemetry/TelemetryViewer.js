/**
 * Telemetry viewer controller.
 *
 * Owns the channel picker, the uPlot instance, the discrete-event overlay, and
 * the cursor readout for `examples/telemetry-viewer.html`. Decoding happens in
 * `telemetry-worker.js`; this module never touches raw bytes.
 *
 * Design notes:
 *
 *  * **The viewer owns the x range, not uPlot.** Every zoom/pan gesture updates
 *    `this.view` and re-runs the decimation, so the number of points uPlot
 *    draws is bounded by the plot width no matter how large the log is.
 *  * **The cursor readout reads full-resolution arrays.** Decimation is a
 *    drawing concern; hover values come from a binary search over the original
 *    samples, so a readout is never a bucket average.
 *  * **Axes are grouped by unit**, not by series. PPO2, duty, depth, pressure,
 *    temperature and raw counts each get their own scale, which is the only way
 *    a 1e5-count phase channel and a 0.7 bar PPO2 channel share a plot.
 */

import { AXES, SERIES_COLOURS, TABLES, formatElapsed } from './TelemetryModel.js';
import { annotateIntervals, buildDrawData, nearestIndex } from './TelemetrySeries.js';
import {
  eventOverlayPlugin, hitTest, hitToleranceS, errorName, errorDescription,
  errorColour, markerLabel, pxRatioOf
} from './EventOverlay.js';
import { applySurfaceReference } from './TelemetryBuilder.js';
import { FL_TYPE_NAMES } from '../uds/constants.js';

/** Fraction of the visible span zoomed per wheel notch. */
const WHEEL_ZOOM_STEP = 0.18;
/** Never zoom in past this span, in seconds. */
const MIN_SPAN_S = 0.05;
/** Beyond this many axis groups the axis text labels are dropped. */
const AXIS_LABEL_LIMIT = 4;
/** Floors so a collapsed or hidden container never yields a zero-size canvas. */
const MIN_PLOT_WIDTH_PX = 320;
const MIN_PLOT_HEIGHT_PX = 300;
/** Channels selected on first load, in preference order. */
const DEFAULT_CHANNELS = [
  'consensus/consensusPpo2',
  'consensus/setpoint',
  'pid/duty',
  'diveo2_c0/depth',
  'diveo2_c0/temperature'
];

/** Overlay layers, in the order they appear in the sidebar. */
const OVERLAY_LAYERS = [
  ['solenoid', 'Solenoid fires'],
  ['errors', 'Error events'],
  ['drops', 'Drop markers (data gaps)'],
  ['markers', 'Boot / dive markers'],
  ['epochGaps', 'Reboot gaps']
];

const el = (id) => document.getElementById(id);

/** Build a DOM node from a tag, attributes and children. @private */
function h(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on')) node.addEventListener(k.slice(2), v);
    else node.setAttribute(k, v);
  }
  for (const c of children) {
    if (c !== null && c !== undefined) node.append(c);
  }
  return node;
}

/** Format a channel value for the readout, scaling precision to magnitude. */
function formatValue(v, unit) {
  if (v === null || v === undefined || Number.isNaN(v)) return '—';
  const abs = Math.abs(v);
  let text;
  if (abs >= 10000) text = v.toFixed(0);
  else if (abs >= 100) text = v.toFixed(1);
  else if (abs >= 1) text = v.toFixed(3);
  else text = v.toFixed(4);
  return unit ? `${text} ${unit}` : text;
}

export class TelemetryViewer {
  constructor() {
    this.model = null;
    this.selection = [];      // pending picker state: [{tableId, key, axis, colour}]
    this.plotSelection = [];  // resolved set the live uPlot instance was built with
    this.plot = null;
    this.rebuildQueued = false;
    this.view = { t0: 0, t1: 1 };
    this.scopes = new Map();
    this.activeScope = null;
    this.timeMode = 'elapsed';
    this.show = Object.fromEntries(OVERLAY_LAYERS.map(([k]) => [k, true]));
    this.pan = null;
  }

  /* ---- Loading ---- */

  /** Wire up the file input, drop target and worker. */
  init() {
    const picker = el('file-input');
    picker.addEventListener('change', () => {
      if (picker.files.length > 0) this.load(picker.files[0]);
    });

    const drop = el('drop-zone');
    drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
    drop.addEventListener('dragleave', () => drop.classList.remove('over'));
    drop.addEventListener('drop', (e) => {
      e.preventDefault();
      drop.classList.remove('over');
      if (e.dataTransfer.files.length > 0) this.load(e.dataTransfer.files[0]);
    });

    el('reset-zoom').addEventListener('click', () => this.resetZoom());
    el('view-scope').addEventListener('change', (e) => this.selectScope(e.target.value));
    el('time-mode').addEventListener('change', (e) => {
      this.timeMode = e.target.value;
      if (this.plot) this.plot.redraw();
    });
    el('surface-apply').addEventListener('click', () => this.applySurface());

    this.renderOverlayToggles();
  }

  /**
   * Decode a picked file in the worker and take ownership of the result.
   * @param {File} file `.bin` download or `.csv` export
   */
  load(file) {
    this.setStatus(`Loading ${file.name} (${(file.size / 1e6).toFixed(1)} MB)…`, 0);
    el('workspace').classList.add('busy');
    const started = performance.now();

    const worker = new Worker(new URL('./telemetry-worker.js', import.meta.url), { type: 'module' });
    worker.onmessage = (ev) => {
      const msg = ev.data;
      if (msg.kind === 'progress') {
        this.setStatus(msg.message, msg.fraction);
      } else if (msg.kind === 'done') {
        const elapsed = performance.now() - started;
        worker.terminate();
        this.adopt(msg.model, file, elapsed);
      } else if (msg.kind === 'error') {
        worker.terminate();
        this.setStatus(`Failed: ${msg.message}`, 1, true);
        console.error(msg.stack);
      }
    };
    worker.onerror = (e) => {
      worker.terminate();
      this.setStatus(`Worker error: ${e.message}`, 1, true);
    };
    worker.postMessage({ kind: 'load', file });
  }

  /** Install a freshly built model and draw it. @private */
  adopt(model, file, elapsedMs) {
    this.model = model;
    // Loading a second log must not inherit the previous one's selection —
    // the channel ids may not even exist in the new model.
    this.selection = [];
    annotateIntervals(model);
    this.renderScopePicker();
    this.view = { t0: this.activeScope.t0, t1: this.activeScope.t1 };

    el('workspace').classList.remove('busy');
    el('drop-zone').classList.add('loaded');
    this.setStatus(
      `${model.meta.totalRecords.toLocaleString()} records from ${file.name} `
      + `in ${(elapsedMs / 1000).toFixed(1)} s`, 1);

    this.renderSummary(file, elapsedMs);
    this.renderChannelPicker();
    this.applyDefaultSelection();
    this.rebuildPlot();
  }

  /* ---- Status + summary ---- */

  /** Build whole-dive and per-boot range choices for the loaded log. @private */
  renderScopePicker() {
    const picker = el('view-scope');
    picker.replaceChildren();
    this.scopes = new Map();

    const wholeLog = {
      id: 'all', label: 'Whole log', t0: 0, t1: Math.max(1, this.model.meta.durationS)
    };
    this.scopes.set(wholeLog.id, wholeLog);
    picker.append(h('option', { value: wholeLog.id, text: wholeLog.label }));

    for (const [index, dive] of (this.model.meta.dives || []).entries()) {
      const notes = [];
      if (!dive.complete) notes.push('partial at log edge');
      if (dive.markerOrder === 'reversed') notes.push('reversed markers corrected');
      const group = h('optgroup', {
        label: `Dive ${dive.diveNumber}${notes.length ? ` — ${notes.join(', ')}` : ''}`
      });

      const diveScope = {
        id: `dive-${index}`,
        label: `Dive ${dive.diveNumber} — whole dive`,
        t0: dive.t0,
        t1: dive.t1
      };
      this.scopes.set(diveScope.id, diveScope);
      group.append(h('option', {
        value: diveScope.id,
        text: `Whole dive · ${formatElapsed(dive.t1 - dive.t0)}`
      }));

      for (const boot of dive.boots) {
        const bootName = boot.bootId === null
          ? `Unknown boot (epoch ${boot.epochIndex})` : `Boot ${boot.bootId}`;
        const bootScope = {
          id: `dive-${index}-epoch-${boot.epochIndex}`,
          label: `Dive ${dive.diveNumber} · ${bootName}`,
          t0: boot.t0,
          t1: boot.t1
        };
        this.scopes.set(bootScope.id, bootScope);
        group.append(h('option', {
          value: bootScope.id,
          text: `${bootName} only · ${formatElapsed(boot.t1 - boot.t0)}`
        }));
      }
      picker.append(group);
    }

    picker.value = wholeLog.id;
    picker.disabled = this.scopes.size === 1;
    this.activeScope = wholeLog;
  }

  /** Activate a fixed range boundary and reset the zoom to that boundary. @private */
  selectScope(id) {
    const scope = this.scopes.get(id);
    if (!scope) return;
    this.activeScope = scope;
    this.view = { t0: scope.t0, t1: scope.t1 };
    this.refreshData();
  }

  setStatus(message, fraction, isError = false) {
    el('status-text').textContent = message;
    el('status-text').classList.toggle('error', isError);
    const bar = el('progress-bar');
    bar.style.width = `${Math.round(fraction * 100)}%`;
    bar.classList.toggle('hidden', fraction >= 1);
  }

  /** Populate the summary panel: counts, epochs, anomalies. @private */
  renderSummary(file, elapsedMs) {
    const m = this.model.meta;
    const box = el('summary');
    box.replaceChildren();

    const kv = (k, v) => h('div', { class: 'kv' },
      h('span', { class: 'k', text: k }), h('span', { class: 'v', text: v }));

    box.append(h('div', { class: 'summary-grid' },
      kv('File', file.name),
      kv('Size', `${(file.size / 1e6).toFixed(1)} MB`),
      kv('Records', m.totalRecords.toLocaleString()),
      kv('Decode time', `${(elapsedMs / 1000).toFixed(2)} s`),
      kv('Span', formatElapsed(m.durationS)),
      kv('Boot epochs', String(m.epochs.length)),
      kv('Dive windows', String((m.dives || []).length)),
      kv('Legacy marker order', (m.dives || []).some((d) => d.markerOrder === 'reversed')
        ? 'detected and corrected' : 'not detected'),
      kv('Surface ref', m.surfaceMbar ? `${m.surfaceMbar.toFixed(1)} mbar` : 'n/a'),
      kv('Wall-clock anchor', m.anchor
        ? `${new Date(m.anchor.unix * 1000).toISOString().slice(0, 19)}Z (${m.anchor.from})`
        : 'none')));

    if (m.surfaceMbar) el('surface-input').value = m.surfaceMbar.toFixed(1);

    const types = h('table', { class: 'mini' },
      h('tr', {}, h('th', { text: 'Record type' }), h('th', { text: 'Count' })));
    for (const [name, n] of Object.entries(m.byType).sort((a, b) => b[1] - a[1])) {
      types.append(h('tr', {}, h('td', { text: name }), h('td', { text: n.toLocaleString() })));
    }
    box.append(h('h3', { text: 'Record types' }), types);

    const epochs = h('table', { class: 'mini' },
      h('tr', {}, h('th', { text: '#' }), h('th', { text: 'Global window' }),
        h('th', { text: 'Boot' }), h('th', { text: 'Notes' })));
    for (const e of m.epochs) {
      const b = e.boot;
      let note = 'ring tail — no boot marker in this window';
      if (b) {
        note = `fw ${b.fwVersion}, reset cause 0x${b.resetCause.toString(16)}`;
        if (b.prevCrash) {
          note += ` — previous boot CRASHED (reason ${b.prevCrash.reason},`
            + ` pc 0x${b.prevCrash.pc.toString(16)})`;
        }
      }
      epochs.append(h('tr', { class: b && b.prevCrash ? 'warn' : '' },
        h('td', { text: String(e.index) }),
        h('td', { text: `${formatElapsed(e.startS)} – ${formatElapsed(e.startS + e.spanS)}` }),
        h('td', { text: b ? String(b.bootId) : '—' }),
        h('td', { text: note })));
    }
    box.append(h('h3', { text: 'Boot epochs' }), epochs);

    box.append(h('h3', { text: 'Errors' }), this.errorTable());
    box.append(h('h3', { text: 'Data gaps' }), this.dropTable());
  }

  /** Aggregate error counts by code for the summary panel. @private */
  errorTable() {
    const e = this.model.events.errors;
    const byCode = new Map();
    const byPair = new Map();
    for (let i = 0; i < e.n; ++i) {
      byCode.set(e.code[i], (byCode.get(e.code[i]) || 0) + 1);
      const key = `${e.code[i]}:${e.detail[i]}`;
      byPair.set(key, (byPair.get(key) || 0) + 1);
    }
    const table = h('table', { class: 'mini' },
      h('tr', {}, h('th', { text: '' }), h('th', { text: 'Code' }),
        h('th', { text: 'Count' }), h('th', { text: 'Top details' })));
    for (const [code, n] of [...byCode].sort((a, b) => b[1] - a[1])) {
      const details = [...byPair]
        .filter(([k]) => k.startsWith(`${code}:`))
        .sort((a, b) => b[1] - a[1]).slice(0, 3)
        .map(([k, c]) => `${k.split(':')[1]} ×${c.toLocaleString()}`).join(', ');
      table.append(h('tr', {},
        h('td', {}, h('span', { class: 'swatch', style: `background:${errorColour(code)}` })),
        h('td', { text: errorName(code), title: errorDescription(code) }),
        h('td', { text: n.toLocaleString() }),
        h('td', { class: 'dim', text: details })));
    }
    if (e.n === 0) table.append(h('tr', {}, h('td', { colspan: '4', text: 'none' })));
    return table;
  }

  /** Aggregate drop markers by the record type that was lost. @private */
  dropTable() {
    const d = this.model.events.drops;
    const byType = new Map();
    let total = 0;
    for (let i = 0; i < d.n; ++i) {
      byType.set(d.lastType[i], (byType.get(d.lastType[i]) || 0) + d.count[i]);
      total += d.count[i];
    }
    const table = h('table', { class: 'mini' },
      h('tr', {}, h('th', { text: 'Last dropped type' }), h('th', { text: 'Records lost' })));
    for (const [type, n] of [...byType].sort((a, b) => b[1] - a[1])) {
      table.append(h('tr', {},
        h('td', { text: FL_TYPE_NAMES[type] || `0x${type.toString(16)}` }),
        h('td', { text: n.toLocaleString() })));
    }
    table.append(h('tr', { class: 'total' },
      h('td', { text: `${d.n} drop markers` }),
      h('td', { text: `${total.toLocaleString()} total` })));
    return table;
  }

  /* ---- Channel picker ---- */

  /** Build the grouped channel checkbox tree. @private */
  renderChannelPicker() {
    const host = el('channels');
    host.replaceChildren();

    // Preserve the declaration order of TABLES, then per-cell splits.
    const ordered = [];
    for (const decl of TABLES) {
      for (const t of Object.values(this.model.tables)) {
        if (t.tableId === decl.id) ordered.push(t);
      }
    }

    for (const table of ordered) {
      const group = h('details', { class: 'chan-group', open: '' });
      group.append(h('summary', {},
        h('span', { text: table.label }),
        h('span', { class: 'dim', text: ` ${table.n.toLocaleString()} samples` })));
      for (const [key, ch] of Object.entries(table.channels)) {
        const id = `ch-${table.id}-${key}`;
        const row = h('label', { class: 'chan', for: id, title: ch.def.desc || '' },
          h('input', {
            type: 'checkbox', id,
            onchange: (e) => this.toggleChannel(table.id, key, e.target.checked)
          }),
          h('span', { class: 'chan-name', text: ch.def.label }),
          h('span', { class: 'chan-unit', text: ch.def.unit || '' }));
        group.append(row);
      }
      host.append(group);
    }
  }

  /** Select a sensible starting set of channels. @private */
  applyDefaultSelection() {
    for (const path of DEFAULT_CHANNELS) {
      const [tableId, key] = path.split('/');
      const table = this.model.tables[tableId];
      if (!table || !table.channels[key]) continue;
      const box = el(`ch-${tableId}-${key}`);
      if (box) box.checked = true;
      this.selection.push({ tableId, key, axis: table.channels[key].def.axis });
    }
    if (this.selection.length === 0) {
      // Fall back to whatever the first table offers.
      const table = Object.values(this.model.tables)[0];
      if (table) {
        const key = Object.keys(table.channels)[0];
        const box = el(`ch-${table.id}-${key}`);
        if (box) box.checked = true;
        this.selection.push({ tableId: table.id, key, axis: table.channels[key].def.axis });
      }
    }
    this.assignColours();
    this.renderSeriesList();
  }

  /** Add or remove a channel from the plot. @private */
  toggleChannel(tableId, key, on) {
    if (on) {
      if (!this.selection.some((s) => s.tableId === tableId && s.key === key)) {
        const def = this.model.tables[tableId].channels[key].def;
        this.selection.push({ tableId, key, axis: def.axis });
      }
    } else {
      this.selection = this.selection.filter((s) => !(s.tableId === tableId && s.key === key));
    }
    this.assignColours();
    this.renderSeriesList();
    this.scheduleRebuild();
  }

  /** Give each selected series a stable colour. @private */
  assignColours() {
    this.selection.forEach((s, i) => { s.colour = SERIES_COLOURS[i % SERIES_COLOURS.length]; });
  }

  /** Render the selected-series list with per-series axis overrides. @private */
  renderSeriesList() {
    const host = el('series-list');
    host.replaceChildren();
    if (this.selection.length === 0) {
      host.append(h('div', { class: 'dim', text: 'No channels selected.' }));
      return;
    }
    for (const s of this.selection) {
      const table = this.model.tables[s.tableId];
      const def = table.channels[s.key].def;
      const axisSelect = h('select', {
        class: 'axis-select',
        onchange: (e) => { s.axis = e.target.value; this.scheduleRebuild(); }
      });
      for (const [key, axis] of Object.entries(AXES)) {
        const opt = h('option', { value: key, text: `${axis.label}${axis.unit ? ` (${axis.unit})` : ''}` });
        if (key === s.axis) opt.selected = true;
        axisSelect.append(opt);
      }
      host.append(h('div', { class: 'series-row' },
        h('span', { class: 'swatch', style: `background:${s.colour}` }),
        h('span', { class: 'series-name', text: `${table.label} · ${def.label}` }),
        axisSelect,
        h('button', {
          class: 'link', text: '✕', title: 'Remove',
          onclick: () => {
            const box = el(`ch-${s.tableId}-${s.key}`);
            if (box) box.checked = false;
            this.toggleChannel(s.tableId, s.key, false);
          }
        })));
    }
  }

  /** Overlay layer toggles. @private */
  renderOverlayToggles() {
    const host = el('overlays');
    host.replaceChildren();
    for (const [key, label] of OVERLAY_LAYERS) {
      host.append(h('label', { class: 'chan' },
        h('input', {
          type: 'checkbox', checked: '',
          onchange: (e) => {
            this.show[key] = e.target.checked;
            if (this.plot) this.plot.redraw();
          }
        }),
        h('span', { class: 'chan-name', text: label })));
    }
  }

  /* ---- Plot ---- */

  /**
   * Resolve the pending selection into live table/channel references.
   *
   * Only `rebuildPlot` consumes this. Everything that touches the *existing*
   * plot must use `this.plotSelection`, which is the set the current uPlot
   * instance was actually constructed with — a rebuild is deferred to the next
   * frame, so between a toggle and that frame the two disagree, and handing
   * uPlot a data array of the wrong arity throws inside its draw path.
   * @private
   */
  resolveSelection() {
    return this.selection.map((s) => ({
      spec: s,
      table: this.model.tables[s.tableId],
      channel: this.model.tables[s.tableId].channels[s.key]
    })).filter((r) => r.table && r.channel);
  }

  /**
   * Queue a plot rebuild for the next frame.
   *
   * Changing the series set means recreating the uPlot instance (its series and
   * axis lists are fixed at construction). Coalescing keeps a burst of channel
   * toggles — "select all", or clicking through a group — to one rebuild.
   */
  scheduleRebuild() {
    if (this.rebuildQueued) return;
    this.rebuildQueued = true;
    requestAnimationFrame(() => {
      this.rebuildQueued = false;
      this.rebuildPlot();
    });
  }

  /** Tear down and rebuild the uPlot instance for the current selection. */
  rebuildPlot() {
    const host = el('plot');
    if (this.plot) { this.plot.destroy(); this.plot = null; }
    host.replaceChildren();
    this.plotSelection = [];
    if (!this.model || this.selection.length === 0) {
      el('draw-stats').textContent = '';
      return;
    }

    const resolved = this.resolveSelection();
    this.plotSelection = resolved;
    const { width, height } = this.plotBox();

    // One scale per axis group actually in use, in the model's declaration
    // order so the same channels always land on the same side.
    const usedAxes = [...new Set(resolved.map((r) => r.spec.axis))]
      .sort((a, b) => Object.keys(AXES).indexOf(a) - Object.keys(AXES).indexOf(b));

    const scales = {
      x: { time: false, auto: false, range: () => [this.view.t0, this.view.t1] }
    };
    for (const key of usedAxes) {
      scales[key] = { auto: true, dir: AXES[key].invert ? -1 : 1 };
    }

    const axes = [{
      scale: 'x',
      stroke: '#9aa0a6',
      grid: { stroke: 'rgba(255,255,255,0.07)' },
      ticks: { stroke: 'rgba(255,255,255,0.15)' },
      values: (u, splits) => splits.map((v) => this.formatTime(v)),
      space: 90
    }];
    // Selecting every channel puts ten scales on the plot; stacking them all on
    // one side would leave no room for the traces. Alternate sides so the gutters
    // stay balanced, and drop the text labels past four axes — the tick colour
    // still ties each axis to its series, and the sidebar names them in full.
    const showLabels = usedAxes.length <= AXIS_LABEL_LIMIT;
    usedAxes.forEach((key, i) => {
      const axis = AXES[key];
      axes.push({
        scale: key,
        side: (i % 2 === 0) ? 3 : 1,
        label: showLabels ? `${axis.label}${axis.unit ? ` (${axis.unit})` : ''}` : undefined,
        labelSize: showLabels ? 26 : 0,
        size: showLabels ? 58 : 46,
        stroke: axis.colour,
        grid: { show: i === 0, stroke: 'rgba(255,255,255,0.05)' },
        ticks: { stroke: 'rgba(255,255,255,0.12)' }
      });
    });

    const series = [{}];
    for (const r of resolved) {
      series.push({
        label: `${r.table.label} · ${r.channel.def.label}`,
        scale: r.spec.axis,
        stroke: r.spec.colour,
        width: 1.4,
        spanGaps: false,
        points: { show: false }
      });
    }

    const { data, points } = buildDrawData(resolved, this.view.t0, this.view.t1, width);

    this.plot = new window.uPlot({
      width, height,
      padding: [12, 12, 0, 0],
      legend: { show: false },
      cursor: {
        drag: { x: true, y: false, setScale: false },
        points: { show: false },
        focus: { prox: 24 }
      },
      scales, axes, series,
      hooks: {
        setSelect: [(u) => {
          if (u.select.width <= 2) return;
          const a = u.posToVal(u.select.left, 'x');
          const b = u.posToVal(u.select.left + u.select.width, 'x');
          u.setSelect({ left: 0, width: 0, top: 0, height: 0 }, false);
          this.setRange(a, b);
        }],
        setCursor: [(u) => this.renderReadout(u)]
      },
      plugins: [eventOverlayPlugin(this.model, { show: this.show })]
    }, data, host);

    this.installGestures(host);
    this.observeResize(host);
    this.updateDrawStats(points, resolved);
  }

  /**
   * Canvas size for the plot host, in CSS pixels.
   *
   * `clientHeight` includes the host's padding, and uPlot adds its canvas as a
   * child — sizing to `clientHeight` therefore overflows the flex row by
   * exactly the padding and pushes the bottom of the plot (where the solenoid
   * band and error rug live) off screen.
   * @private
   */
  plotBox() {
    const host = el('plot');
    const style = getComputedStyle(host);
    const padX = Number.parseFloat(style.paddingLeft) + Number.parseFloat(style.paddingRight);
    const padY = Number.parseFloat(style.paddingTop) + Number.parseFloat(style.paddingBottom);
    return {
      width: Math.max(MIN_PLOT_WIDTH_PX, (host.clientWidth || MIN_PLOT_WIDTH_PX) - padX),
      height: Math.max(MIN_PLOT_HEIGHT_PX, (host.clientHeight || MIN_PLOT_HEIGHT_PX) - padY)
    };
  }

  /**
   * Drawing-area width in CSS pixels, used to size the decimation grid.
   *
   * Prefers uPlot's measured plot area (which excludes the axis gutters),
   * falling back to the container while the plot is torn down.
   * @private
   */
  plotWidth() {
    if (this.plot && Number.isFinite(this.plot.bbox?.width) && this.plot.bbox.width > 0) {
      return this.plot.bbox.width / pxRatioOf(this.plot);
    }
    return this.plotBox().width;
  }

  /** Report the current window and the decimation ratio. @private */
  updateDrawStats(points, resolved) {
    const source = resolved.reduce((a, r) => a + r.table.n, 0);
    el('draw-stats').textContent =
      `${this.activeScope ? `${this.activeScope.label} · ` : ''}`
      + `${formatElapsed(this.view.t1 - this.view.t0)} window · `
      + `${points.toLocaleString()} drawn points · `
      + `${source.toLocaleString()} source samples`;
  }

  /** Wheel zoom and shift/middle-drag pan on the plot surface. @private */
  installGestures(host) {
    const over = this.plot.over;

    over.addEventListener('wheel', (e) => {
      e.preventDefault();
      const rect = over.getBoundingClientRect();
      const frac = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
      const { t0, t1 } = this.view;
      const span = t1 - t0;
      const factor = e.deltaY > 0 ? (1 + WHEEL_ZOOM_STEP) : (1 - WHEEL_ZOOM_STEP);
      const anchor = t0 + span * frac;
      this.setRange(anchor - (anchor - t0) * factor, anchor + (t1 - anchor) * factor);
    }, { passive: false });

    over.addEventListener('mousedown', (e) => {
      if (!e.shiftKey && e.button !== 1) return;
      e.preventDefault();
      const rect = over.getBoundingClientRect();
      this.pan = { x: e.clientX, width: rect.width, t0: this.view.t0, t1: this.view.t1 };
      over.style.cursor = 'grabbing';
    });

    const endPan = () => {
      if (!this.pan) return;
      this.pan = null;
      over.style.cursor = '';
    };
    window.addEventListener('mouseup', endPan);
    window.addEventListener('mousemove', (e) => {
      if (!this.pan) return;
      const span = this.pan.t1 - this.pan.t0;
      const shift = -((e.clientX - this.pan.x) / this.pan.width) * span;
      this.setRange(this.pan.t0 + shift, this.pan.t1 + shift);
    });

    over.addEventListener('dblclick', () => this.resetZoom());
  }

  /** Keep the plot sized to its container. @private */
  observeResize(host) {
    if (this.resizeObserver) this.resizeObserver.disconnect();
    this.resizeObserver = new ResizeObserver(() => {
      if (!this.plot) return;
      this.plot.setSize(this.plotBox());
      this.refreshData();
    });
    this.resizeObserver.observe(host);
  }

  /**
   * Set the visible time range and redraw.
   * @param {number} a Range start (s)
   * @param {number} b Range end (s)
   * @param {boolean} [clamp=true] Keep the range inside the active scope
   */
  setRange(a, b, clamp = true) {
    if (!this.model) return;
    const bounds = this.activeScope || {
      t0: 0, t1: Math.max(1, this.model.meta.durationS)
    };
    const lower = bounds.t0;
    const upper = bounds.t1;
    const extent = upper - lower;
    let t0 = Math.min(a, b);
    let t1 = Math.max(a, b);
    if (t1 - t0 < MIN_SPAN_S) {
      const mid = (t0 + t1) / 2;
      t0 = mid - MIN_SPAN_S / 2;
      t1 = mid + MIN_SPAN_S / 2;
    }
    if (clamp) {
      const span = Math.min(t1 - t0, extent);
      if (t0 < lower) { t0 = lower; t1 = lower + span; }
      if (t1 > upper) { t1 = upper; t0 = upper - span; }
    }
    this.view = { t0, t1 };
    this.refreshData();
  }

  /** Reset to the active whole-log, dive, or dive/boot extent. */
  resetZoom() {
    if (!this.model) return;
    const scope = this.activeScope || { t0: 0, t1: Math.max(1, this.model.meta.durationS) };
    this.setRange(scope.t0, scope.t1);
  }

  /** Re-decimate for the current range and hand the result to uPlot. @private */
  refreshData() {
    if (!this.plot) return;
    const resolved = this.plotSelection || [];
    const { data, points } = buildDrawData(resolved, this.view.t0, this.view.t1,
      this.plotWidth());
    this.plot.setData(data);
    this.updateDrawStats(points, resolved);
  }

  /* ---- Readout ---- */

  /** Format a time for the axis and readout per the selected time mode. */
  formatTime(t) {
    const anchor = this.model && this.model.meta.anchor;
    if (this.timeMode === 'absolute' && anchor) {
      const unix = anchor.unix + (t - anchor.atS);
      return new Date(unix * 1000).toISOString().slice(11, 19);
    }
    return formatElapsed(t);
  }

  /** Update the hover panel from full-resolution samples. @private */
  renderReadout(u) {
    const host = el('readout');
    const left = u.cursor.left;
    if (left === null || left === undefined || left < 0) {
      host.replaceChildren(h('div', { class: 'dim', text: 'Hover the plot for values.' }));
      return;
    }
    const t = u.posToVal(left, 'x');
    if (!Number.isFinite(t)) {
      host.replaceChildren(h('div', { class: 'dim', text: 'Hover the plot for values.' }));
      return;
    }
    const rows = [h('div', { class: 'readout-time' },
      h('span', { text: this.formatTime(t) }),
      h('span', { class: 'dim', text: this.epochLabel(t) }))];

    for (const r of (this.plotSelection || [])) {
      const i = nearestIndex(r.table.time, r.table.n, t);
      const stale = i < 0 || Math.abs(r.table.time[i] - t)
        > Math.max(1, (r.table.intervalS || 1) * 4);
      rows.push(h('div', { class: 'readout-row' },
        h('span', { class: 'swatch', style: `background:${r.spec.colour}` }),
        h('span', { class: 'readout-name', text: `${r.table.label} · ${r.channel.def.label}` }),
        h('span', {
          class: `readout-val${stale ? ' stale' : ''}`,
          text: stale ? '— (no sample)' : formatValue(r.channel.data[i], r.channel.def.unit)
        })));
    }

    const hits = hitTest(this.model, t, hitToleranceS(u));
    for (const m of hits.markers) {
      rows.push(h('div', { class: 'readout-event marker', text: markerLabel(m) }));
    }
    for (const s of hits.solenoid) {
      rows.push(h('div', { class: 'readout-event solenoid' },
        h('strong', { text: s.kindName }),
        h('span', {
          text: ` open ${(s.actualS * 1000).toFixed(0)} ms`
            + ` (requested ${(s.requestedS * 1000).toFixed(0)} ms)`
            + ` at ${this.formatTime(s.t0)}`
        })));
    }
    for (const d of hits.drops) {
      rows.push(h('div', { class: 'readout-event drop' },
        h('strong', { text: 'DATA GAP' }),
        h('span', {
          text: ` ${d.count.toLocaleString()} record(s) dropped before `
            + `${this.formatTime(d.t)}; last lost was ${d.lastTypeName}`
        })));
    }
    for (const e of hits.errors) {
      rows.push(h('div', { class: 'readout-event error' },
        h('span', { class: 'swatch', style: `background:${errorColour(e.code)}` }),
        h('strong', { text: e.name }),
        h('span', {
          text: ` ×${e.count} · detail ${e.detail} (0x${e.detail.toString(16)}) — ${e.description}`
        })));
    }

    host.replaceChildren(...rows);
  }

  /** Which boot epoch a global time falls in, plus its boot-relative offset. */
  epochLabel(t) {
    for (const e of this.model.meta.epochs) {
      if (t >= e.startS && t <= e.startS + e.spanS) {
        const rel = e.bootRelStartS + (t - e.startS);
        const id = e.boot ? `boot ${e.boot.bootId}` : 'pre-marker epoch';
        return `${id} +${formatElapsed(rel)}`;
      }
    }
    return 'between epochs (synthetic gap)';
  }

  /* ---- Surface reference ---- */

  /** Re-datum the derived depth channel from the operator's value. @private */
  applySurface() {
    if (!this.model) return;
    const value = Number.parseFloat(el('surface-input').value);
    if (!Number.isFinite(value) || value <= 0) return;
    applySurfaceReference(this.model, value);
    this.refreshData();
    this.setStatus(`Surface reference set to ${value.toFixed(1)} mbar`, 1);
  }
}
