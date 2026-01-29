/**
 * EventParser - Parse CSV event lines from DiveCANHead log stream
 *
 * Event format: timestamp,eventIndex,TYPE,field1,field2,...
 */

export class EventParser {
  static EVENT_TYPES = {
    DIVEO2: 'DIVEO2',
    O2S: 'O2S',
    ANALOGCELL: 'ANALOGCELL',
    CAN: 'CAN',
    PID: 'PID',
    PPO2STATE: 'PPO2STATE'
  };

  /**
   * Parse a CSV event line into a structured object
   * @param {string} line - Raw CSV line from event stream
   * @returns {Object|null} Parsed event or null if invalid
   */
  static parse(line) {
    const trimmed = line.trim();
    if (!trimmed) return null;

    const parts = trimmed.split(',');
    if (parts.length < 3) return null;

    const timestamp = parseFloat(parts[0]);
    const eventIndex = parseInt(parts[1], 10);
    const eventType = parts[2];

    switch (eventType) {
      case 'DIVEO2':
        return this._parseDiveO2(timestamp, eventIndex, parts);
      case 'O2S':
        return this._parseO2S(timestamp, eventIndex, parts);
      case 'ANALOGCELL':
        return this._parseAnalogCell(timestamp, eventIndex, parts);
      case 'CAN':
        return this._parseCAN(timestamp, eventIndex, parts);
      case 'PID':
        return this._parsePID(timestamp, eventIndex, parts);
      case 'PPO2STATE':
        return this._parsePPO2State(timestamp, eventIndex, parts);
      default:
        return { timestamp, eventIndex, type: 'UNKNOWN', raw: trimmed };
    }
  }

  /**
   * Parse DiveO2 digital cell event
   * Format: timestamp,idx,DIVEO2,cellNum,PPO2,temp,err,phase,intensity,ambientLight,pressure,humidity
   */
  static _parseDiveO2(timestamp, eventIndex, parts) {
    return {
      type: 'DIVEO2',
      timestamp,
      eventIndex,
      cellNumber: parseInt(parts[3], 10),
      ppo2: parseInt(parts[4], 10),
      temperature: parseInt(parts[5], 10),
      error: parseInt(parts[6], 10),
      phase: parseInt(parts[7], 10),
      intensity: parseInt(parts[8], 10),
      ambientLight: parseInt(parts[9], 10),
      pressure: parseInt(parts[10], 10),
      humidity: parseInt(parts[11], 10)
    };
  }

  /**
   * Parse O2S digital cell event
   * Format: timestamp,idx,O2S,cellNum,PPO2
   */
  static _parseO2S(timestamp, eventIndex, parts) {
    return {
      type: 'O2S',
      timestamp,
      eventIndex,
      cellNumber: parseInt(parts[3], 10),
      ppo2: parseFloat(parts[4])
    };
  }

  /**
   * Parse analog galvanic cell event
   * Format: timestamp,idx,ANALOGCELL,cellNum,sample
   */
  static _parseAnalogCell(timestamp, eventIndex, parts) {
    return {
      type: 'ANALOGCELL',
      timestamp,
      eventIndex,
      cellNumber: parseInt(parts[3], 10),
      sample: parseInt(parts[4], 10)
    };
  }

  /**
   * Parse CAN message event
   * Format: timestamp,idx,CAN,dir,msgType,len,msgId,b0,b1,b2,b3,b4,b5,b6,b7
   */
  static _parseCAN(timestamp, eventIndex, parts) {
    return {
      type: 'CAN',
      timestamp,
      eventIndex,
      direction: parts[3],
      messageType: parts[4],
      length: parseInt(parts[5], 10),
      messageId: parseInt(parts[6], 16),
      data: parts.slice(7, 15).map(b => parseInt(b, 16))
    };
  }

  /**
   * Parse PID controller state event
   * Format: timestamp,idx,PID,integralState,satCount,dutyCycle,setpoint
   */
  static _parsePID(timestamp, eventIndex, parts) {
    return {
      type: 'PID',
      timestamp,
      eventIndex,
      integralState: parseFloat(parts[3]),
      saturationCount: parseInt(parts[4], 10),
      dutyCycle: parseFloat(parts[5]),
      setpoint: parseFloat(parts[6])
    };
  }

  /**
   * Parse PPO2 consensus state event
   * Format: timestamp,idx,PPO2STATE,c1_inc,c1_val,c2_inc,c2_val,c3_inc,c3_val,consensus
   */
  static _parsePPO2State(timestamp, eventIndex, parts) {
    return {
      type: 'PPO2STATE',
      timestamp,
      eventIndex,
      c1_included: parts[3] === '1',
      c1_value: parseFloat(parts[4]),
      c2_included: parts[5] === '1',
      c2_value: parseFloat(parts[6]),
      c3_included: parts[7] === '1',
      c3_value: parseFloat(parts[8]),
      consensus: parseFloat(parts[9])
    };
  }

  /**
   * Get list of plottable field names for an event type
   * @param {string} eventType - Event type name
   * @returns {string[]} Array of field names
   */
  static getPlottableFields(eventType) {
    switch (eventType) {
      case 'DIVEO2':
        return ['ppo2', 'temperature', 'error', 'phase', 'intensity', 'ambientLight', 'pressure', 'humidity'];
      case 'O2S':
        return ['ppo2'];
      case 'ANALOGCELL':
        return ['sample'];
      case 'PID':
        return ['integralState', 'saturationCount', 'dutyCycle', 'setpoint'];
      case 'PPO2STATE':
        return ['c1_value', 'c2_value', 'c3_value', 'consensus'];
      default:
        return [];
    }
  }
}
