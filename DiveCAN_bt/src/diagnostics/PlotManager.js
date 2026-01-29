/**
 * PlotManager - Real-time plotting using Chart.js
 *
 * Provides a wrapper around Chart.js for real-time streaming data visualization.
 * Supports dual Y-axes for comparing different scales.
 */

export class PlotManager {
  /**
   * Create a PlotManager
   * @param {string} canvasId - ID of the canvas element
   * @param {Object} options - Configuration options
   */
  constructor(canvasId, options = {}) {
    this.canvas = document.getElementById(canvasId);
    this.ctx = this.canvas.getContext('2d');
    this.chart = null;
    this.dataStore = null;
    this.activeSeries = []; // { key, label, axis: 'y' | 'y1' }
    this.updateInterval = options.updateInterval || 100;
    this.windowSize = options.windowSize || 60;
    this.updateTimer = null;
    this.referenceTime = null;  // Time offset for X axis (set on first data or clear)

    // Color palette - first color for left axis, second for right axis
    this.colors = {
      y: '#00bcd4',  // Cyan for left axis
      y1: '#4caf50'  // Green for right axis
    };
  }

  /**
   * Initialize the chart (must be called after Chart.js is loaded)
   * @param {Object} Chart - Chart.js constructor
   */
  init(Chart) {
    this.Chart = Chart;

    this.chart = new Chart(this.ctx, {
      type: 'line',
      data: {
        datasets: []
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        scales: {
          x: {
            type: 'linear',
            title: { display: true, text: 'Time (s)', color: '#888' },
            ticks: { color: '#888' },
            grid: { color: '#333' }
          },
          y: {
            type: 'linear',
            position: 'left',
            title: { display: true, text: '', color: '#00bcd4' },
            ticks: { color: '#00bcd4' },
            grid: { color: '#333' }
          },
          y1: {
            type: 'linear',
            position: 'right',
            title: { display: true, text: '', color: '#4caf50' },
            ticks: { color: '#4caf50' },
            grid: { drawOnChartArea: false }
          }
        },
        plugins: {
          legend: {
            display: true,
            labels: { color: '#e0e0e0' }
          }
        },
        interaction: {
          intersect: false,
          mode: 'index'
        }
      }
    });

    // Initially hide right axis
    this.chart.options.scales.y1.display = false;
  }

  /**
   * Set the data store to pull data from
   * @param {DataStore} dataStore
   */
  setDataStore(dataStore) {
    this.dataStore = dataStore;
  }

  /**
   * Set the left axis (Y) series
   * @param {string} seriesKey - Key from DataStore
   * @param {string} label - Display label for legend
   */
  setLeftAxis(seriesKey, label = null) {
    // Remove existing left axis series
    const existingIndex = this.activeSeries.findIndex(s => s.axis === 'y');
    if (existingIndex !== -1) {
      this.activeSeries.splice(existingIndex, 1);
      this.chart.data.datasets.splice(existingIndex, 1);
    }

    if (!seriesKey) {
      this.chart.options.scales.y.title.text = '';
      this.chart.update('none');
      return;
    }

    const displayLabel = label || seriesKey;
    this.activeSeries.push({ key: seriesKey, label: displayLabel, axis: 'y' });

    this.chart.data.datasets.push({
      label: displayLabel,
      data: [],
      borderColor: this.colors.y,
      backgroundColor: this.colors.y + '20',
      borderWidth: 2,
      tension: 0.1,
      pointRadius: 0,
      fill: false,
      yAxisID: 'y'
    });

    this.chart.options.scales.y.title.text = displayLabel;
    this.chart.update('none');
  }

  /**
   * Set the right axis (Y1) series
   * @param {string} seriesKey - Key from DataStore
   * @param {string} label - Display label for legend
   */
  setRightAxis(seriesKey, label = null) {
    // Remove existing right axis series
    const existingIndex = this.activeSeries.findIndex(s => s.axis === 'y1');
    if (existingIndex !== -1) {
      this.activeSeries.splice(existingIndex, 1);
      this.chart.data.datasets.splice(existingIndex, 1);
    }

    if (!seriesKey) {
      this.chart.options.scales.y1.display = false;
      this.chart.options.scales.y1.title.text = '';
      this.chart.update('none');
      return;
    }

    const displayLabel = label || seriesKey;
    this.activeSeries.push({ key: seriesKey, label: displayLabel, axis: 'y1' });

    this.chart.data.datasets.push({
      label: displayLabel,
      data: [],
      borderColor: this.colors.y1,
      backgroundColor: this.colors.y1 + '20',
      borderWidth: 2,
      tension: 0.1,
      pointRadius: 0,
      fill: false,
      yAxisID: 'y1'
    });

    this.chart.options.scales.y1.display = true;
    this.chart.options.scales.y1.title.text = displayLabel;
    this.chart.update('none');
  }

  /**
   * Clear all series from the plot
   */
  clearSeries() {
    this.activeSeries = [];
    this.chart.data.datasets = [];
    this.chart.options.scales.y.title.text = '';
    this.chart.options.scales.y1.display = false;
    this.chart.options.scales.y1.title.text = '';
    this.referenceTime = null;  // Reset reference time on clear
    this.chart.update('none');
  }

  /**
   * Get current left axis series key
   * @returns {string|null}
   */
  getLeftAxisKey() {
    const series = this.activeSeries.find(s => s.axis === 'y');
    return series ? series.key : null;
  }

  /**
   * Get current right axis series key
   * @returns {string|null}
   */
  getRightAxisKey() {
    const series = this.activeSeries.find(s => s.axis === 'y1');
    return series ? series.key : null;
  }

  /**
   * Start the update loop
   */
  start() {
    if (this.updateTimer) return;
    this.updateTimer = setInterval(() => this._updateChart(), this.updateInterval);
  }

  /**
   * Stop the update loop
   */
  stop() {
    if (this.updateTimer) {
      clearInterval(this.updateTimer);
      this.updateTimer = null;
    }
  }

  /**
   * Update chart with latest data
   * @private
   */
  _updateChart() {
    if (!this.dataStore || this.activeSeries.length === 0) return;

    // First pass: find the earliest and latest timestamps across all series
    let earliestTimestamp = Infinity;
    let latestTimestamp = 0;
    for (let i = 0; i < this.activeSeries.length; i++) {
      const series = this.activeSeries[i];
      const data = this.dataStore.getSeries(series.key);
      if (data.length > 0) {
        const seriesEarliest = data[0].timestamp;
        const seriesLatest = data[data.length - 1].timestamp;
        if (seriesEarliest < earliestTimestamp) {
          earliestTimestamp = seriesEarliest;
        }
        if (seriesLatest > latestTimestamp) {
          latestTimestamp = seriesLatest;
        }
      }
    }

    // Set reference time on first data if not already set
    if (this.referenceTime === null && earliestTimestamp !== Infinity) {
      this.referenceTime = earliestTimestamp;
    }

    const refTime = this.referenceTime || 0;

    // Calculate window bounds (relative to reference time)
    const relativeLatest = latestTimestamp - refTime;
    const windowMin = relativeLatest - this.windowSize;
    const absoluteWindowMin = latestTimestamp - this.windowSize;

    // Second pass: filter data to only include points within the window
    for (let i = 0; i < this.activeSeries.length; i++) {
      const series = this.activeSeries[i];
      const data = this.dataStore.getSeries(series.key);

      // Filter to only points within the visible window, offset by reference time
      const filteredData = data
        .filter(p => p.timestamp >= absoluteWindowMin)
        .map(p => ({ x: p.timestamp - refTime, y: p.value }));

      this.chart.data.datasets[i].data = filteredData;
    }

    // Set X axis bounds (relative to reference time)
    if (latestTimestamp > 0) {
      this.chart.options.scales.x.min = Math.max(0, windowMin);
      this.chart.options.scales.x.max = relativeLatest;
    }

    this.chart.update('none');
  }

  /**
   * Set the time window size
   * @param {number} seconds
   */
  setWindowSize(seconds) {
    this.windowSize = seconds;
  }

  /**
   * Destroy the chart and clean up
   */
  destroy() {
    this.stop();
    if (this.chart) {
      this.chart.destroy();
      this.chart = null;
    }
  }
}
