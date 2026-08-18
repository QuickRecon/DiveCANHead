/**
 * Telemetry channel model.
 *
 * Declares every plottable numeric channel that can be extracted from a
 * flash-log telemetry stream, plus the axis groups they hang off. The
 * builder (TelemetryBuilder.js) consumes these declarations to fill typed
 * arrays; the viewer consumes them to populate the channel picker and to
 * decide which uPlot axis a series belongs to.
 *
 * A "table" is a set of channels that share one time base — i.e. one flash-log
 * record type (further split per cell index where a record carries a cell
 * index, so cell 0/1/2 become separate series with independent sample times).
 *
 * Units: see the scale constants in ../logs/LogParser.js. Everything declared
 * here is already in SI/display units; the builder applies `scale` while
 * filling.
 */

import {
  FL_TYPE_CONSENSUS,
  FL_TYPE_PID_SNAPSHOT,
  FL_TYPE_ATMOS_PRESSURE,
  FL_TYPE_POWER_SNAPSHOT,
  FL_TYPE_CELL_RAW_DIVEO2,
  FL_TYPE_CELL_RAW_O2S,
  FL_TYPE_CELL_RAW_ANALOG,
  FL_POWER_BATTERY_VALID,
  FL_POWER_VBUS_VALID,
  FL_POWER_VCC_VALID,
  FL_POWER_CAN_VALID,
  FL_POWER_CURRENT_VALID,
  FL_POWER_POSEIDON_PERCENT_VALID
} from '../uds/constants.js';
import {
  PPO2_CBAR_PER_BAR,
  MILLIVOLT_LSB_PER_MV,
  DIVEO2_TEMP_LSB_PER_DEGC,
  DIVEO2_PRESSURE_LSB_PER_MBAR,
  DIVEO2_HUMIDITY_LSB_PER_PCT
} from '../logs/LogParser.js';

/** Number of oxygen cells the head supports. Mirrors CELL_COUNT in firmware. */
export const CELL_COUNT = 3;

/**
 * Axis groups. Channels sharing a group share a uPlot y-axis and scale, which
 * is what keeps PPO2 (0-2 bar) from being flattened by phase counts (~1e4).
 *
 * `side` 0 = top, 1 = right, 2 = bottom, 3 = left (uPlot convention).
 */
export const AXES = {
  ppo2: { key: 'ppo2', label: 'PPO2 / setpoint', unit: 'bar', side: 3, colour: '#4ea3ff' },
  duty: { key: 'duty', label: 'Duty / integral', unit: '0-1', side: 3, colour: '#ffb454' },
  depth: { key: 'depth', label: 'Depth', unit: 'm', side: 3, colour: '#7ee787', invert: true },
  pressure: { key: 'pressure', label: 'Ambient pressure', unit: 'mbar', side: 1, colour: '#79c0ff' },
  temperature: { key: 'temperature', label: 'Temperature', unit: '°C', side: 1, colour: '#ff7b72' },
  humidity: { key: 'humidity', label: 'Humidity', unit: '%RH', side: 1, colour: '#d2a8ff' },
  millivolts: { key: 'millivolts', label: 'Cell output', unit: 'mV', side: 1, colour: '#f0883e' },
  voltage: { key: 'voltage', label: 'Supply voltage', unit: 'V', side: 1, colour: '#56d4dd' },
  current: { key: 'current', label: 'Supply current', unit: 'mA', side: 1, colour: '#ff9ec6' },
  power: { key: 'power', label: 'Supply power', unit: 'W', side: 1, colour: '#e3b341' },
  percent: { key: 'percent', label: 'Battery', unit: '%', side: 1, colour: '#a2d2a0' },
  age: { key: 'age', label: 'Sample age', unit: 's', side: 1, colour: '#c9a0ff' },
  counts: { key: 'counts', label: 'Raw counts', unit: 'counts', side: 1, colour: '#8b949e' },
  code: { key: 'code', label: 'Status / code', unit: '', side: 1, colour: '#a5a5a5' }
};

/** Distinct series colours, cycled in channel-selection order. */
export const SERIES_COLOURS = [
  '#4ea3ff', '#ff7b72', '#7ee787', '#ffb454', '#d2a8ff', '#79c0ff',
  '#f0883e', '#56d4dd', '#e3b341', '#ff9ec6', '#a2d2a0', '#c9a0ff'
];

/**
 * Per-cell channel definitions for CELL_RAW_DIVEO2 (0x20).
 *
 * `off` is the byte offset into the record payload, `read` names the reader
 * the builder uses, `scale` converts raw LSBs to the declared unit.
 */
const DIVEO2_FIELDS = [
  {
    key: 'ppo2', label: 'PPO2', unit: 'bar', axis: 'ppo2',
    off: 1, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR,
    desc: 'Cell PPO2 as transmitted on the wire (uint8 centibar).'
  },
  {
    key: 'temperature', label: 'Temperature', unit: '°C', axis: 'temperature',
    off: 2, read: 'i32', scale: 1 / DIVEO2_TEMP_LSB_PER_DEGC,
    desc: 'Sensor die temperature. Raw field is milli-°C despite the _dc name.'
  },
  {
    key: 'errCode', label: 'Error code', unit: '', axis: 'code',
    off: 6, read: 'u32', scale: 1,
    desc: 'DiveO2 sensor error bitfield; 0 = healthy.'
  },
  {
    key: 'phase', label: 'Phase', unit: 'counts', axis: 'counts',
    off: 10, read: 'i32', scale: 1,
    desc: 'Luminescence phase shift — the primary optical measurand.'
  },
  {
    key: 'intensity', label: 'Intensity', unit: 'counts', axis: 'counts',
    off: 14, read: 'i32', scale: 1,
    desc: 'Return-signal intensity. Falls as the luminophore ages.'
  },
  {
    key: 'ambientLight', label: 'Ambient light', unit: 'counts', axis: 'counts',
    off: 18, read: 'i32', scale: 1,
    desc: 'Stray-light background. Non-zero implies a light leak.'
  },
  {
    key: 'pressureMbar', label: 'Ambient pressure', unit: 'mbar', axis: 'pressure',
    off: 22, read: 'u32', scale: 1 / DIVEO2_PRESSURE_LSB_PER_MBAR,
    desc: 'Absolute loop pressure. Raw field is milli-hPa despite the _uhpa name.'
  },
  {
    key: 'humidity', label: 'Humidity', unit: '%RH', axis: 'humidity',
    off: 26, read: 'i32', scale: 1 / DIVEO2_HUMIDITY_LSB_PER_PCT,
    desc: 'Relative humidity inside the sensor housing.'
  },
  {
    key: 'depth', label: 'Depth', unit: 'm', axis: 'depth',
    off: 22, read: 'u32', scale: 1 / DIVEO2_PRESSURE_LSB_PER_MBAR,
    derived: 'depth',
    desc: 'Derived: (pressure_mbar - surface_mbar) / 100. Surface reference is '
        + 'the 2nd-percentile pressure over the whole log unless overridden.'
  }
];

/** Per-cell channel definitions for CELL_RAW_O2S (0x21). */
const O2S_FIELDS = [
  {
    key: 'ppo2', label: 'PPO2', unit: 'bar', axis: 'ppo2',
    off: 1, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR,
    desc: 'Cell PPO2 (uint8 centibar).'
  },
  {
    key: 'status', label: 'Status', unit: '', axis: 'code',
    off: 2, read: 'u8', scale: 1,
    desc: 'O2S sensor status byte.'
  }
];

/** Per-cell channel definitions for CELL_RAW_ANALOG (0x22). */
const ANALOG_FIELDS = [
  {
    key: 'ppo2', label: 'PPO2', unit: 'bar', axis: 'ppo2',
    off: 1, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR,
    desc: 'Calibrated cell PPO2 (uint8 centibar).'
  },
  {
    key: 'rawAdc', label: 'Raw ADC', unit: 'counts', axis: 'counts',
    off: 2, read: 'i32', scale: 1,
    desc: 'External ADS1115 differential conversion result.'
  },
  {
    key: 'millivolts', label: 'Cell output', unit: 'mV', axis: 'millivolts',
    off: 6, read: 'u16', scale: 1 / MILLIVOLT_LSB_PER_MV,
    desc: 'Galvanic cell output voltage.'
  }
];

/** Channel definitions for CONSENSUS (0x10) — one table, no cell split. */
const CONSENSUS_FIELDS = [
  {
    key: 'consensusPpo2', label: 'Consensus PPO2', unit: 'bar', axis: 'ppo2',
    off: 0, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR,
    desc: 'Voted PPO2 the controller acts on.'
  },
  {
    key: 'setpoint', label: 'Setpoint', unit: 'bar', axis: 'ppo2',
    off: 13, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR,
    desc: 'Active PPO2 setpoint.'
  },
  {
    key: 'confidence', label: 'Confidence', unit: 'cells', axis: 'code',
    off: 12, read: 'u8', scale: 1,
    desc: 'Number of cells that agreed in the vote.'
  },
  {
    key: 'ppo2_c0', label: 'Cell 0 PPO2 (voted)', unit: 'bar', axis: 'ppo2',
    off: 1, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR, desc: 'Cell 0 PPO2 as seen by the vote.'
  },
  {
    key: 'ppo2_c1', label: 'Cell 1 PPO2 (voted)', unit: 'bar', axis: 'ppo2',
    off: 2, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR, desc: 'Cell 1 PPO2 as seen by the vote.'
  },
  {
    key: 'ppo2_c2', label: 'Cell 2 PPO2 (voted)', unit: 'bar', axis: 'ppo2',
    off: 3, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR, desc: 'Cell 2 PPO2 as seen by the vote.'
  },
  {
    key: 'mv_c0', label: 'Cell 0 output', unit: 'mV', axis: 'millivolts',
    off: 4, read: 'u16', scale: 1 / MILLIVOLT_LSB_PER_MV, desc: 'Cell 0 mV (0 for digital cells).'
  },
  {
    key: 'mv_c1', label: 'Cell 1 output', unit: 'mV', axis: 'millivolts',
    off: 6, read: 'u16', scale: 1 / MILLIVOLT_LSB_PER_MV, desc: 'Cell 1 mV (0 for digital cells).'
  },
  {
    key: 'mv_c2', label: 'Cell 2 output', unit: 'mV', axis: 'millivolts',
    off: 8, read: 'u16', scale: 1 / MILLIVOLT_LSB_PER_MV, desc: 'Cell 2 mV (0 for digital cells).'
  },
  {
    key: 'status_c0', label: 'Cell 0 status', unit: '', axis: 'code',
    off: 10, read: 'u16', scale: 1, bits: { shift: 0, mask: 0x03 },
    desc: '0=OK 1=DEGRADED 2=FAIL 3=NEED_CAL'
  },
  {
    key: 'status_c1', label: 'Cell 1 status', unit: '', axis: 'code',
    off: 10, read: 'u16', scale: 1, bits: { shift: 3, mask: 0x03 },
    desc: '0=OK 1=DEGRADED 2=FAIL 3=NEED_CAL'
  },
  {
    key: 'status_c2', label: 'Cell 2 status', unit: '', axis: 'code',
    off: 10, read: 'u16', scale: 1, bits: { shift: 6, mask: 0x03 },
    desc: '0=OK 1=DEGRADED 2=FAIL 3=NEED_CAL'
  },
  {
    key: 'include_c0', label: 'Cell 0 included', unit: '', axis: 'code',
    off: 10, read: 'u16', scale: 1, bits: { shift: 2, mask: 0x01 },
    desc: '1 when cell 0 contributed to the vote.'
  },
  {
    key: 'include_c1', label: 'Cell 1 included', unit: '', axis: 'code',
    off: 10, read: 'u16', scale: 1, bits: { shift: 5, mask: 0x01 },
    desc: '1 when cell 1 contributed to the vote.'
  },
  {
    key: 'include_c2', label: 'Cell 2 included', unit: '', axis: 'code',
    off: 10, read: 'u16', scale: 1, bits: { shift: 8, mask: 0x01 },
    desc: '1 when cell 2 contributed to the vote.'
  }
];

/** Channel definitions for PID_SNAPSHOT (0x11). */
const PID_FIELDS = [
  {
    key: 'duty', label: 'Solenoid duty', unit: '0-1', axis: 'duty',
    off: 6, read: 'f32', scale: 1,
    desc: 'PID output — fraction of the control period the solenoid is open.'
  },
  {
    key: 'integral', label: 'PID integral', unit: '0-1', axis: 'duty',
    off: 0, read: 'f32', scale: 1,
    desc: 'Accumulated integral term.'
  },
  {
    key: 'saturationCount', label: 'Saturation count', unit: 'counts', axis: 'code',
    off: 4, read: 'u16', scale: 1,
    desc: 'Consecutive iterations the output was clamped.'
  },
  {
    key: 'setpoint', label: 'Setpoint (PID)', unit: 'bar', axis: 'ppo2',
    off: 10, read: 'u8', scale: 1 / PPO2_CBAR_PER_BAR,
    desc: 'Setpoint the PID iteration used.'
  }
];

/** Channel definitions for ATMOS_PRESSURE (0x14). */
const ATMOS_FIELDS = [
  {
    key: 'pressureMbar', label: 'Atmospheric pressure', unit: 'mbar', axis: 'pressure',
    off: 0, read: 'u16', scale: 1, validWhen: 'positive',
    desc: 'Ambient pressure from chan_atmos_pressure (handset PPO2_ATMOS frame).'
  },
  {
    key: 'depth', label: 'Depth', unit: 'm', axis: 'depth',
    off: 0, read: 'u16', scale: 1, derived: 'depth',
    desc: 'Derived from atmospheric pressure and the selected surface reference.'
  }
];

/** Channel definitions for POWER_SNAPSHOT (0x15); validity bits live at byte 31. */
const POWER_FIELDS = [
  {
    key: 'vbusVoltage', label: 'VBUS voltage', unit: 'V', axis: 'voltage',
    off: 0, read: 'f32', scale: 1, validBit: FL_POWER_VBUS_VALID,
    desc: 'Peripheral-bus rail voltage; unavailable readings render as gaps.'
  },
  {
    key: 'vccVoltage', label: 'VCC voltage', unit: 'V', axis: 'voltage',
    off: 4, read: 'f32', scale: 1, validBit: FL_POWER_VCC_VALID,
    desc: 'MCU supply-rail voltage.'
  },
  {
    key: 'batteryVoltage', label: 'Battery voltage', unit: 'V', axis: 'voltage',
    off: 8, read: 'f32', scale: 1, validBit: FL_POWER_BATTERY_VALID,
    desc: 'Primary battery measurement.'
  },
  {
    key: 'canVoltage', label: 'CAN voltage', unit: 'V', axis: 'voltage',
    off: 12, read: 'f32', scale: 1, validBit: FL_POWER_CAN_VALID,
    desc: 'CAN-side supply voltage where sensing hardware is present.'
  },
  {
    key: 'batteryThreshold', label: 'Low-battery threshold', unit: 'V', axis: 'voltage',
    off: 16, read: 'f32', scale: 1,
    desc: 'Threshold selected by the active battery chemistry setting.'
  },
  {
    key: 'currentMa', label: 'Device current', unit: 'mA', axis: 'current',
    off: 20, read: 'i32', scale: 1 / 1000, validBit: FL_POWER_CURRENT_VALID,
    desc: 'Whole-device current; positive means draw from the supply.'
  },
  {
    key: 'powerW', label: 'Battery power', unit: 'W', axis: 'power',
    off: 20, read: 'i32', scale: 1, derived: 'power',
    desc: 'Derived from battery voltage × whole-device current.'
  },
  {
    key: 'currentAge', label: 'Current sample age', unit: 's', axis: 'age',
    off: 24, read: 'u32', scale: 1 / 1000, validBit: FL_POWER_CURRENT_VALID,
    desc: 'Age of the whole-device current sample when this snapshot was logged.'
  },
  {
    key: 'poseidonAge', label: 'Poseidon gauge age', unit: 's', axis: 'age',
    off: 28, read: 'u16', scale: 1, validBit: FL_POWER_POSEIDON_PERCENT_VALID,
    desc: 'Age of the latest Poseidon battery-percentage frame.'
  },
  {
    key: 'poseidonPercent', label: 'Poseidon battery', unit: '%', axis: 'percent',
    off: 30, read: 'u8', scale: 1, validBit: FL_POWER_POSEIDON_PERCENT_VALID,
    desc: 'Poseidon DS2782 fuel percentage when a gauge frame has been received.'
  },
  {
    key: 'lowBattery', label: 'Low battery', unit: '', axis: 'code',
    off: 31, read: 'u8', scale: 1, bits: { shift: 7, mask: 1 },
    desc: '1 when battery voltage is below the configured threshold.'
  },
  {
    key: 'poseidonFresh', label: 'Poseidon gauge fresh', unit: '', axis: 'code',
    off: 31, read: 'u8', scale: 1, bits: { shift: 6, mask: 1 },
    desc: '1 while the Poseidon percentage sample is within its freshness window.'
  }
];

/**
 * Table declarations. `perCell` tables are split into one table per observed
 * cell index at build time; `cellOff` is the payload offset of the cell index.
 */
export const TABLES = [
  {
    id: 'consensus', type: FL_TYPE_CONSENSUS, label: 'Consensus',
    payloadLen: 14, fields: CONSENSUS_FIELDS
  },
  {
    id: 'pid', type: FL_TYPE_PID_SNAPSHOT, label: 'PID',
    payloadLen: 11, fields: PID_FIELDS
  },
  {
    id: 'atmos', type: FL_TYPE_ATMOS_PRESSURE, label: 'Atmospheric pressure',
    payloadLen: 2, fields: ATMOS_FIELDS
  },
  {
    id: 'power', type: FL_TYPE_POWER_SNAPSHOT, label: 'Power',
    payloadLen: 32, flagsOff: 31, fields: POWER_FIELDS
  },
  {
    id: 'diveo2', type: FL_TYPE_CELL_RAW_DIVEO2, label: 'DiveO2 raw',
    payloadLen: 30, perCell: true, cellOff: 0, fields: DIVEO2_FIELDS
  },
  {
    id: 'o2s', type: FL_TYPE_CELL_RAW_O2S, label: 'O2S raw',
    payloadLen: 3, perCell: true, cellOff: 0, fields: O2S_FIELDS
  },
  {
    id: 'analog', type: FL_TYPE_CELL_RAW_ANALOG, label: 'Analog raw',
    payloadLen: 8, perCell: true, cellOff: 0, fields: ANALOG_FIELDS
  }
];

/** Look up a table declaration by record type. */
export function tableForType(type) {
  return TABLES.find((t) => t.type === type) || null;
}

/**
 * Format an elapsed time in seconds as h:mm:ss(.mmm).
 * @param {number} seconds
 * @param {boolean} [millis=false] include milliseconds
 */
export function formatElapsed(seconds, millis = false) {
  if (!Number.isFinite(seconds)) return '—';
  const negative = seconds < 0;
  const abs = Math.abs(seconds);
  const h = Math.floor(abs / 3600);
  const m = Math.floor((abs % 3600) / 60);
  const s = Math.floor(abs % 60);
  const pad = (n) => String(n).padStart(2, '0');
  let out = `${h}:${pad(m)}:${pad(s)}`;
  if (millis) {
    out += `.${String(Math.floor((abs % 1) * 1000)).padStart(3, '0')}`;
  }
  return negative ? `-${out}` : out;
}
