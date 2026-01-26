/**
 * CellUIAdapter - Dynamically update cell UI based on detected cell types
 *
 * Each cell panel adapts to show type-appropriate fields.
 */

import { DataStore } from './DataStore.js';

export class CellUIAdapter {
  /**
   * Create a CellUIAdapter
   * @param {DataStore} dataStore - Data store instance
   * @param {Function} onPlotLeftRequest - Callback for left axis plot (seriesKey, label)
   * @param {Function} onPlotRightRequest - Callback for right axis plot (seriesKey, label)
   */
  constructor(dataStore, onPlotLeftRequest, onPlotRightRequest) {
    this.dataStore = dataStore;
    this.onPlotLeftRequest = onPlotLeftRequest;
    this.onPlotRightRequest = onPlotRightRequest;
    this.previousCellTypes = new Map();
  }

  /**
   * Update all cell panels based on latest data
   */
  update() {
    for (let cellNum = 0; cellNum < 3; cellNum++) {
      this._updateCellPanel(cellNum);
    }
  }

  /**
   * Update a single cell panel
   * @private
   */
  _updateCellPanel(cellNum) {
    const panel = document.getElementById(`cell-panel-${cellNum}`);
    if (!panel) return;

    const cellType = this.dataStore.getCellType(cellNum);
    const previousType = this.previousCellTypes.get(cellNum);

    // Update header
    const header = panel.querySelector('.cell-header');
    if (header) {
      if (cellType) {
        header.textContent = `Cell ${cellNum}: ${cellType}`;
        header.classList.add('active');
      } else {
        header.textContent = `Cell ${cellNum}: --`;
        header.classList.remove('active');
      }
    }

    // Rebuild fields if type changed
    const fieldsContainer = panel.querySelector('.cell-fields');
    if (fieldsContainer && cellType !== previousType) {
      fieldsContainer.innerHTML = this._buildFieldsHTML(cellNum, cellType);
      this.previousCellTypes.set(cellNum, cellType);
    }

    // Update field values
    this._updateFieldValues(cellNum, cellType);
  }

  /**
   * Build HTML for cell fields based on type
   * @private
   */
  _buildFieldsHTML(cellNum, cellType) {
    if (!cellType) {
      return '<div class="field-inactive">(No data)</div>';
    }

    const fields = this._getFieldDefinitions(cellType);
    return fields.map(f => this._fieldRowHTML(cellNum, cellType, f)).join('');
  }

  /**
   * Get field definitions for a cell type
   * @private
   */
  _getFieldDefinitions(cellType) {
    switch (cellType) {
      case 'DIVEO2':
        return [
          { field: 'ppo2', label: 'PPO2', unit: 'mbar', decimals: 0 },
          { field: 'temperature', label: 'Temp', unit: '', decimals: 0 },
          { field: 'error', label: 'Error', unit: '', decimals: 0 },
          { field: 'phase', label: 'Phase', unit: '', decimals: 0 },
          { field: 'intensity', label: 'Intensity', unit: '', decimals: 0 },
          { field: 'ambientLight', label: 'Ambient', unit: '', decimals: 0 },
          { field: 'pressure', label: 'Pressure', unit: '', decimals: 0 },
          { field: 'humidity', label: 'Humidity', unit: '', decimals: 0 }
        ];
      case 'O2S':
        return [
          { field: 'ppo2', label: 'PPO2', unit: 'bar', decimals: 3 }
        ];
      case 'ANALOGCELL':
        return [
          { field: 'sample', label: 'ADC', unit: '', decimals: 0 }
        ];
      default:
        return [];
    }
  }

  /**
   * Generate HTML for a single field row
   * @private
   */
  _fieldRowHTML(cellNum, cellType, fieldDef) {
    const seriesKey = DataStore.key(cellType, fieldDef.field, cellNum);
    const label = `${fieldDef.label} (Cell ${cellNum})`;
    return `
      <div class="field-row" data-series-key="${seriesKey}" data-cell="${cellNum}" data-field="${fieldDef.field}">
        <span class="field-label">${fieldDef.label}:</span>
        <span class="field-value" data-decimals="${fieldDef.decimals}">--</span>
        <span class="field-unit">${fieldDef.unit}</span>
        <button class="plot-btn plot-left" data-series-key="${seriesKey}" data-label="${label}" title="Plot on left axis">L</button>
        <button class="plot-btn plot-right" data-series-key="${seriesKey}" data-label="${label}" title="Plot on right axis">R</button>
      </div>
    `;
  }

  /**
   * Update field values from latest data
   * @private
   */
  _updateFieldValues(cellNum, cellType) {
    if (!cellType) return;

    const panel = document.getElementById(`cell-panel-${cellNum}`);
    if (!panel) return;

    const fieldRows = panel.querySelectorAll('.field-row');
    fieldRows.forEach(row => {
      const seriesKey = row.dataset.seriesKey;
      const valueEl = row.querySelector('.field-value');
      if (!valueEl) return;

      const latest = this.dataStore.getLatest(seriesKey);
      const decimals = parseInt(valueEl.dataset.decimals || '0', 10);

      if (latest) {
        valueEl.textContent = latest.value.toFixed(decimals);
      } else {
        valueEl.textContent = '--';
      }
    });
  }

  /**
   * Initialize click handlers for plot buttons
   * Call this after the page is loaded
   */
  initPlotButtons() {
    document.addEventListener('click', (e) => {
      if (e.target.classList.contains('plot-left')) {
        const seriesKey = e.target.dataset.seriesKey;
        const label = e.target.dataset.label;
        if (this.onPlotLeftRequest && seriesKey) {
          this.onPlotLeftRequest(seriesKey, label);
        }
      } else if (e.target.classList.contains('plot-right')) {
        const seriesKey = e.target.dataset.seriesKey;
        const label = e.target.dataset.label;
        if (this.onPlotRightRequest && seriesKey) {
          this.onPlotRightRequest(seriesKey, label);
        }
      }
    });
  }
}
