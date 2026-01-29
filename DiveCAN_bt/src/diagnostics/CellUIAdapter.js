/**
 * CellUIAdapter - Dynamically update cell UI based on state vector data
 *
 * Reads directly from state vector for current values and DataStore series for plots.
 */

import {
  CELL_TYPE_NONE,
  CELL_TYPE_ANALOG,
  CELL_TYPE_O2S,
  CELL_TYPE_DIVEO2
} from '../uds/constants.js';

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
    this.previousCellTypes = [CELL_TYPE_NONE, CELL_TYPE_NONE, CELL_TYPE_NONE];
  }

  /**
   * Update all cell panels based on latest state vector
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
    const previousType = this.previousCellTypes[cellNum];

    // Update header
    const header = panel.querySelector('.cell-header');
    if (header) {
      const typeName = this.dataStore.getCellTypeName(cellNum);
      const included = this.dataStore.isCellIncluded(cellNum);

      if (cellType !== CELL_TYPE_NONE) {
        header.textContent = `Cell ${cellNum}: ${typeName}${included ? '' : ' (excluded)'}`;
        header.classList.add('active');
        if (!included) {
          header.classList.add('excluded');
        } else {
          header.classList.remove('excluded');
        }
      } else {
        header.textContent = `Cell ${cellNum}: --`;
        header.classList.remove('active', 'excluded');
      }
    }

    // Rebuild fields if type changed
    const fieldsContainer = panel.querySelector('.cell-fields');
    if (fieldsContainer && cellType !== previousType) {
      fieldsContainer.innerHTML = this._buildFieldsHTML(cellNum, cellType);
      this.previousCellTypes[cellNum] = cellType;
    }

    // Update field values from state vector
    this._updateFieldValues(cellNum, cellType);
  }

  /**
   * Build HTML for cell fields based on type
   * @private
   */
  _buildFieldsHTML(cellNum, cellType) {
    if (cellType === CELL_TYPE_NONE) {
      return '<div class="field-inactive">(No data)</div>';
    }

    const fields = this._getFieldDefinitions(cellType);
    return fields.map(f => this._fieldRowHTML(cellNum, f)).join('');
  }

  /**
   * Get field definitions for a cell type
   * @private
   */
  _getFieldDefinitions(cellType) {
    switch (cellType) {
      case CELL_TYPE_DIVEO2:
        return [
          { field: 'ppo2', label: 'PPO2', unit: 'bar', decimals: 3 },
          { field: 'temperature', label: 'Temp', unit: 'mC', decimals: 0 },
          { field: 'error', label: 'Error', unit: '', decimals: 0 },
          { field: 'phase', label: 'Phase', unit: '', decimals: 0 },
          { field: 'intensity', label: 'Intensity', unit: '', decimals: 0 },
          { field: 'ambientLight', label: 'Ambient', unit: '', decimals: 0 },
          { field: 'pressure', label: 'Pressure', unit: 'ubar', decimals: 0 },
          { field: 'humidity', label: 'Humidity', unit: 'mRH', decimals: 0 }
        ];
      case CELL_TYPE_O2S:
        return [
          { field: 'ppo2', label: 'PPO2', unit: 'bar', decimals: 3 }
        ];
      case CELL_TYPE_ANALOG:
        return [
          { field: 'ppo2', label: 'PPO2', unit: 'bar', decimals: 3 },
          { field: 'rawAdc', label: 'ADC', unit: '', decimals: 0 }
        ];
      default:
        return [];
    }
  }

  /**
   * Generate HTML for a single field row
   * @private
   */
  _fieldRowHTML(cellNum, fieldDef) {
    const seriesKey = `cell${cellNum}.${fieldDef.field}`;
    const label = `Cell ${cellNum} ${fieldDef.label}`;
    return `
      <div class="field-row" data-cell="${cellNum}" data-field="${fieldDef.field}">
        <span class="field-label">${fieldDef.label}:</span>
        <span class="field-value" data-decimals="${fieldDef.decimals}">--</span>
        <span class="field-unit">${fieldDef.unit}</span>
        <button class="plot-btn plot-left" data-series-key="${seriesKey}" data-label="${label}" title="Plot on left axis">L</button>
        <button class="plot-btn plot-right" data-series-key="${seriesKey}" data-label="${label}" title="Plot on right axis">R</button>
      </div>
    `;
  }

  /**
   * Update field values from state vector
   * @private
   */
  _updateFieldValues(cellNum, cellType) {
    if (cellType === CELL_TYPE_NONE) return;

    const panel = document.getElementById(`cell-panel-${cellNum}`);
    if (!panel) return;

    const cell = this.dataStore.getCell(cellNum);
    if (!cell) return;

    const fieldRows = panel.querySelectorAll('.field-row');
    fieldRows.forEach(row => {
      const field = row.dataset.field;
      const valueEl = row.querySelector('.field-value');
      if (!valueEl || !field) return;

      const value = cell[field];
      const decimals = parseInt(valueEl.dataset.decimals || '0', 10);

      if (value !== undefined && value !== null && !isNaN(value)) {
        valueEl.textContent = value.toFixed(decimals);
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
