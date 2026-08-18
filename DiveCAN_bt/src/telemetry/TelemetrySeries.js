/**
 * Viewport decimation for large telemetry series.
 *
 * A five-hour log holds a few hundred thousand samples per channel. Handing
 * that to a canvas renderer wastes work: a 1200 px plot can only resolve ~1200
 * columns. This module reduces every selected channel onto one **shared,
 * uniform time grid** covering the visible range, keeping each bucket's min and
 * max so spikes survive.
 *
 * Two properties make the grid safe:
 *
 *  * It is shared, which is what lets channels with different sample times
 *    (consensus, PID, per-cell raw) share a single uPlot x array.
 *  * Bucket width is derived from the pixel width, so the time quantisation is
 *    always sub-pixel — the decimated line is visually identical to the full
 *    series at every zoom level.
 *
 * Gaps are preserved rather than bridged: a run of empty buckets wider than the
 * channel's expected sample interval emits `null`, so a logging dropout or a
 * reboot shows as a break instead of a straight line through missing data.
 */

/** Buckets per pixel of plot width. Two gives one min/max pair per column. */
const BUCKETS_PER_PX = 1;

/** Lower/upper bounds on bucket count regardless of plot width. */
const MIN_BUCKETS = 64;
const MAX_BUCKETS = 4000;

/** Multiple of a channel's median sample interval that still counts as continuous. */
const GAP_TOLERANCE = 4;

/** Cap on samples inspected when estimating a channel's sample interval. */
const INTERVAL_SAMPLE_CAP = 5000;

/**
 * Estimate the typical sample interval of a time array, in seconds.
 *
 * Uses the median of a strided sample of successive deltas so a handful of
 * dropouts or a reboot gap cannot inflate the estimate.
 *
 * @param {Float64Array} time Monotonic sample times
 * @param {number} n Number of valid entries
 * @returns {number} Median interval in seconds, or 0 for a degenerate series
 */
export function medianInterval(time, n) {
  if (n < 2) return 0;
  const step = Math.max(1, Math.floor(n / INTERVAL_SAMPLE_CAP));
  const deltas = [];
  for (let i = step; i < n; i += step) {
    const d = (time[i] - time[i - step]) / step;
    if (d > 0) deltas.push(d);
  }
  if (deltas.length === 0) return 0;
  deltas.sort((a, b) => a - b);
  return deltas[deltas.length >> 1];
}

/**
 * Index of the first sample at or after `t`.
 * @param {Float64Array} time Monotonic sample times
 * @param {number} n Number of valid entries
 * @param {number} t Target time
 * @returns {number} Insertion index in [0, n]
 */
export function lowerBound(time, n, t) {
  let lo = 0;
  let hi = n;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (time[mid] < t) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/**
 * Nearest sample index to `t`, or -1 when the series is empty.
 * @param {Float64Array} time
 * @param {number} n
 * @param {number} t
 */
export function nearestIndex(time, n, t) {
  if (n === 0) return -1;
  const i = lowerBound(time, n, t);
  if (i === 0) return 0;
  if (i >= n) return n - 1;
  return (t - time[i - 1] <= time[i] - t) ? i - 1 : i;
}

/**
 * Choose a bucket count for a plot width.
 *
 * Tolerates a non-finite or non-positive width — a plot mid-teardown, or one
 * whose axes momentarily consume the whole canvas, must not produce a bucket
 * count that blows up the allocation.
 *
 * @param {number} widthPx
 */
export function bucketCountFor(widthPx) {
  if (!Number.isFinite(widthPx) || widthPx <= 0) return MIN_BUCKETS;
  return Math.max(MIN_BUCKETS,
    Math.min(MAX_BUCKETS, Math.round(widthPx * BUCKETS_PER_PX)));
}

/**
 * Build the shared x grid for a visible range.
 *
 * Each bucket contributes two slots (its min sample and its max sample), placed
 * at the bucket's first and third quartile so the pair renders as a short
 * vertical envelope rather than a zero-width spike.
 *
 * @param {number} t0 Range start (s)
 * @param {number} t1 Range end (s)
 * @param {number} buckets Bucket count
 * @returns {Float64Array} Grid of length `2 * buckets`
 */
export function buildGrid(t0, t1, buckets) {
  const dt = (t1 - t0) / buckets;
  const x = new Float64Array(buckets * 2);
  for (let b = 0; b < buckets; ++b) {
    x[b * 2] = t0 + dt * (b + 0.25);
    x[b * 2 + 1] = t0 + dt * (b + 0.75);
  }
  return x;
}

/**
 * Reduce one channel onto a prepared grid.
 *
 * @param {Float64Array} time Sample times
 * @param {Float32Array} values Sample values
 * @param {number} n Sample count
 * @param {number} t0 Range start (s)
 * @param {number} t1 Range end (s)
 * @param {number} buckets Bucket count (grid length is `2 * buckets`)
 * @param {number} gapS Emit a break when consecutive samples are further apart
 * @returns {Array<number|null>} Values aligned to `buildGrid(t0, t1, buckets)`
 */
export function decimateChannel(time, values, n, t0, t1, buckets, gapS) {
  const out = new Array(buckets * 2).fill(null);
  if (n === 0) return out;

  const dt = (t1 - t0) / buckets;
  const from = lowerBound(time, n, t0);
  const to = lowerBound(time, n, t1);

  // Bucket the visible samples, tracking min/max per bucket.
  let cursor = from;
  for (let b = 0; b < buckets && cursor < to; ++b) {
    const edge = t0 + dt * (b + 1);
    let lo = Infinity;
    let hi = -Infinity;
    let any = false;
    while (cursor < to && time[cursor] < edge) {
      const v = values[cursor];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      any = true;
      ++cursor;
    }
    if (any) {
      out[b * 2] = lo;
      out[b * 2 + 1] = hi;
    }
  }

  const maxEmptyBuckets = gapS > 0 ? Math.floor(gapS / dt) : 0;
  if (maxEmptyBuckets === 0) return out;

  // Anchor the edge buckets to the samples just outside the range, *before*
  // bridging, so a channel whose nearest sample sits off-screen still reaches
  // the plot edge and the bridging pass can run all the way out to it.
  const last = buckets - 1;
  if (out[0] === null && from > 0 && (t0 - time[from - 1]) <= gapS) {
    out[0] = values[from - 1];
    out[1] = values[from - 1];
  }
  if (out[last * 2] === null && to < n && (time[to] - t1) <= gapS) {
    out[last * 2] = values[to];
    out[last * 2 + 1] = values[to];
  }

  // Buckets are finer than the sample rate when zoomed in, leaving holes
  // between real samples. Bridge holes narrower than the gap threshold so the
  // line stays continuous; leave wider ones null so genuine dropouts show as
  // breaks rather than as a straight line through missing data.
  let prev = -1;
  for (let b = 0; b < buckets; ++b) {
    if (out[b * 2] === null) continue;
    const span = b - prev - 1;
    if (prev >= 0 && span > 0 && span <= maxEmptyBuckets) {
      const a = out[prev * 2 + 1];
      const z = out[b * 2];
      for (let k = prev + 1; k < b; ++k) {
        const v = a + (z - a) * ((k - prev) / (b - prev));
        out[k * 2] = v;
        out[k * 2 + 1] = v;
      }
    }
    prev = b;
  }

  return out;
}

/**
 * Reduce a set of selected channels onto one shared grid.
 *
 * @param {Array<{table: Object, channel: Object}>} selection
 * @param {number} t0 Range start (s)
 * @param {number} t1 Range end (s)
 * @param {number} widthPx Plot width in CSS pixels
 * @returns {{data: Array, buckets: number, points: number}} uPlot-ready data
 *   (`data[0]` is the shared x array) plus the reduced point count
 */
export function buildDrawData(selection, t0, t1, widthPx) {
  const buckets = bucketCountFor(widthPx);
  const x = buildGrid(t0, t1, buckets);
  const data = [x];
  for (const { table, channel } of selection) {
    const gapS = (table.intervalS || 0) * GAP_TOLERANCE;
    data.push(decimateChannel(table.time, channel.data, table.n,
      t0, t1, buckets, gapS));
  }
  return { data, buckets, points: buckets * 2 * selection.length };
}

/**
 * Annotate every table with its median sample interval.
 *
 * Called once after a model is built; `buildDrawData` reads `table.intervalS`
 * to decide how wide a hole has to be before it counts as a real gap.
 * @param {Object} model TelemetryModel
 */
export function annotateIntervals(model) {
  for (const table of Object.values(model.tables)) {
    table.intervalS = medianInterval(table.time, table.n);
  }
}
