/**
 * Web Worker: turn a picked file into a TelemetryModel off the main thread.
 *
 * Decoding an 850k-record log takes long enough to drop frames if it runs on
 * the UI thread, and the CSV path allocates hundreds of megabytes of
 * intermediate strings. Both happen here; the finished model crosses back as
 * transferable ArrayBuffers, so there is no structured-clone copy of the
 * channel data.
 *
 * Protocol:
 *   -> { kind: 'load', file: File }
 *   <- { kind: 'progress', fraction, message }
 *   <- { kind: 'done', model }        (channel arrays transferred)
 *   <- { kind: 'error', message, stack }
 */

import { buildTelemetry, transferablesOf } from './TelemetryBuilder.js';
import { csvToStream } from './CsvSource.js';

/** Fraction of the progress bar given to the CSV-to-stream conversion. */
const CSV_PROGRESS_SHARE = 0.5;

const post = (kind, payload, transfer) => self.postMessage({ kind, ...payload }, transfer || []);

const progress = (fraction, message) =>
  post('progress', { fraction: Math.max(0, Math.min(1, fraction)), message });

/**
 * Channel `def` objects are shared module-level declarations; stripping them to
 * plain data keeps the postMessage clone small and avoids sending functions.
 * @private
 */
function serialisableModel(model) {
  const tables = {};
  for (const [id, t] of Object.entries(model.tables)) {
    const channels = {};
    for (const [key, ch] of Object.entries(t.channels)) {
      channels[key] = { def: { ...ch.def }, data: ch.data };
    }
    tables[id] = {
      id: t.id,
      tableId: t.tableId,
      type: t.type,
      cellIndex: t.cellIndex,
      label: t.label,
      n: t.n,
      time: t.time,
      bootTime: t.bootTime,
      epoch: t.epoch,
      channels
    };
  }
  return { meta: model.meta, tables, events: model.events };
}

self.onmessage = async (ev) => {
  const { kind, file } = ev.data;
  if (kind !== 'load') return;

  try {
    const name = (file.name || '').toLowerCase();
    let bytes;

    if (name.endsWith('.csv')) {
      progress(0, 'Reading CSV…');
      bytes = await csvToStream(file, {
        onProgress: (f, m) => progress(f * CSV_PROGRESS_SHARE, m)
      });
      progress(CSV_PROGRESS_SHARE, 'Decoding records…');
      const model = buildTelemetry(bytes, {
        onProgress: (f, m) => progress(CSV_PROGRESS_SHARE + f * (1 - CSV_PROGRESS_SHARE), m)
      });
      const out = serialisableModel(model);
      post('done', { model: out }, transferablesOf(model));
      return;
    }

    progress(0, 'Reading file…');
    bytes = new Uint8Array(await file.arrayBuffer());
    const model = buildTelemetry(bytes, { onProgress: progress });
    const out = serialisableModel(model);
    post('done', { model: out }, transferablesOf(model));
  } catch (err) {
    post('error', { message: String(err && err.message ? err.message : err), stack: err && err.stack });
  }
};
