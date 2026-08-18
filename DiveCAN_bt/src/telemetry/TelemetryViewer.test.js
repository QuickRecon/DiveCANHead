import { describe, it, expect, vi } from 'vitest';
import { TelemetryViewer } from './TelemetryViewer.js';
import { markerLabel } from './EventOverlay.js';

describe('TelemetryViewer scoped ranges', () => {
  it('clamps zooming and panning to the selected dive or boot scope', () => {
    const viewer = new TelemetryViewer();
    viewer.model = { meta: { durationS: 100 } };
    viewer.activeScope = { id: 'boot', label: 'Dive 7 · Boot 12', t0: 20, t1: 40 };
    viewer.refreshData = vi.fn();

    viewer.setRange(0, 30);
    expect(viewer.view).toEqual({ t0: 20, t1: 40 });

    viewer.setRange(35, 55);
    expect(viewer.view).toEqual({ t0: 20, t1: 40 });

    viewer.setRange(25, 35);
    expect(viewer.view).toEqual({ t0: 25, t1: 35 });
  });

  it('resets zoom to the selected scope rather than the whole log', () => {
    const viewer = new TelemetryViewer();
    viewer.model = { meta: { durationS: 100 } };
    viewer.activeScope = { id: 'dive', label: 'Dive 7', t0: 10, t1: 60 };
    viewer.view = { t0: 25, t1: 30 };
    viewer.refreshData = vi.fn();

    viewer.resetZoom();
    expect(viewer.view).toEqual({ t0: 10, t1: 60 });
  });

  it('renders whole-dive and per-boot choices with the inversion called out', () => {
    document.body.innerHTML = '<select id="view-scope"></select>';
    const viewer = new TelemetryViewer();
    viewer.model = {
      meta: {
        durationS: 100,
        dives: [{
          diveNumber: 35,
          t0: 10,
          t1: 80,
          complete: true,
          markerOrder: 'reversed',
          boots: [
            { epochIndex: 0, bootId: null, t0: 10, t1: 30 },
            { epochIndex: 1, bootId: 184, t0: 40, t1: 80 }
          ]
        }]
      }
    };

    viewer.renderScopePicker();
    const picker = document.getElementById('view-scope');
    expect(picker.disabled).toBe(false);
    expect([...picker.options].map((x) => x.textContent)).toEqual([
      'Whole log',
      'Whole dive · 0:01:10',
      'Unknown boot (epoch 0) only · 0:00:20',
      'Boot 184 only · 0:00:40'
    ]);
    expect(picker.querySelector('optgroup').label).toContain('reversed markers corrected');
  });

  it('explains the corrected and recorded kinds in a reversed marker tooltip', () => {
    expect(markerLabel({
      kind: 'diveEnd',
      semanticKind: 'diveStart',
      diveNumber: 35,
      unixTimestamp: 1786789920
    })).toContain('Dive start (recorded as DIVE_END) — dive 35');
  });
});
