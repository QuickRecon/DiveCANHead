/**
 * DID parsing test vectors
 *
 * Format: { did, rawBytes, expectedValue, type, description }
 */

// IEEE 754 float32 test values (little-endian)
export const FLOAT32_VECTORS = [
  {
    did: 0xF200,
    rawBytes: [0x00, 0x00, 0x80, 0x3F],  // 1.0 in little-endian IEEE 754
    expectedValue: 1.0,
    type: 'float32',
    description: 'Float32 value 1.0'
  },
  {
    did: 0xF200,
    rawBytes: [0x00, 0x00, 0x00, 0x00],  // 0.0
    expectedValue: 0.0,
    type: 'float32',
    description: 'Float32 value 0.0'
  },
  {
    did: 0xF200,
    rawBytes: [0x66, 0x66, 0x86, 0x3F],  // ~1.05
    expectedValue: 1.05,
    tolerance: 0.001,
    type: 'float32',
    description: 'Float32 value 1.05 (typical PPO2)'
  },
  {
    did: 0xF200,
    rawBytes: [0xCD, 0xCC, 0xA6, 0x3F],  // 1.30
    expectedValue: 1.3,
    tolerance: 0.001,
    type: 'float32',
    description: 'Float32 value 1.30 (typical setpoint)'
  },
  {
    did: 0xF200,
    rawBytes: [0x00, 0x00, 0x00, 0x40],  // 2.0
    expectedValue: 2.0,
    type: 'float32',
    description: 'Float32 value 2.0'
  },
  {
    did: 0xF200,
    rawBytes: [0x00, 0x00, 0x80, 0xBF],  // -1.0
    expectedValue: -1.0,
    type: 'float32',
    description: 'Float32 negative value -1.0'
  }
];

export const INT32_VECTORS = [
  {
    did: 0xF406,  // Cell temperature
    rawBytes: [0x00, 0x00, 0x00, 0x00],  // 0
    expectedValue: 0,
    type: 'int32',
    description: 'Int32 value 0'
  },
  {
    did: 0xF406,
    rawBytes: [0x19, 0x00, 0x00, 0x00],  // 25 (little-endian)
    expectedValue: 25,
    type: 'int32',
    description: 'Int32 value 25 (25C temperature)'
  },
  {
    did: 0xF406,
    rawBytes: [0xE7, 0xFF, 0xFF, 0xFF],  // -25 (two's complement, little-endian)
    expectedValue: -25,
    type: 'int32',
    description: 'Int32 negative value -25'
  },
  {
    did: 0xF406,
    rawBytes: [0xFF, 0xFF, 0xFF, 0x7F],  // MAX_INT32
    expectedValue: 2147483647,
    type: 'int32',
    description: 'Int32 max value'
  }
];

export const UINT32_VECTORS = [
  {
    did: 0xF220,  // Uptime seconds
    rawBytes: [0x00, 0x00, 0x00, 0x00],  // 0
    expectedValue: 0,
    type: 'uint32',
    description: 'Uint32 value 0'
  },
  {
    did: 0xF220,
    rawBytes: [0x3C, 0x00, 0x00, 0x00],  // 60 (1 minute)
    expectedValue: 60,
    type: 'uint32',
    description: 'Uint32 value 60'
  },
  {
    did: 0xF220,
    rawBytes: [0x10, 0x0E, 0x00, 0x00],  // 3600 (1 hour)
    expectedValue: 3600,
    type: 'uint32',
    description: 'Uint32 value 3600'
  },
  {
    did: 0xF220,
    rawBytes: [0xFF, 0xFF, 0xFF, 0xFF],  // MAX_UINT32
    expectedValue: 4294967295,
    type: 'uint32',
    description: 'Uint32 max value'
  }
];

export const INT16_VECTORS = [
  {
    did: 0xF404,  // Raw ADC
    rawBytes: [0x00, 0x00],  // 0
    expectedValue: 0,
    type: 'int16',
    description: 'Int16 value 0'
  },
  {
    did: 0xF404,
    rawBytes: [0xE8, 0x03],  // 1000 (little-endian)
    expectedValue: 1000,
    type: 'int16',
    description: 'Int16 value 1000'
  },
  {
    did: 0xF404,
    rawBytes: [0x18, 0xFC],  // -1000 (two's complement, little-endian)
    expectedValue: -1000,
    type: 'int16',
    description: 'Int16 negative value -1000'
  },
  {
    did: 0xF404,
    rawBytes: [0xFF, 0x7F],  // 32767 (MAX_INT16)
    expectedValue: 32767,
    type: 'int16',
    description: 'Int16 max value'
  }
];

export const UINT16_VECTORS = [
  {
    did: 0xF405,  // Millivolts
    rawBytes: [0x00, 0x00],  // 0
    expectedValue: 0,
    type: 'uint16',
    description: 'Uint16 value 0'
  },
  {
    did: 0xF405,
    rawBytes: [0x64, 0x00],  // 100
    expectedValue: 100,
    type: 'uint16',
    description: 'Uint16 value 100 (100mV)'
  },
  {
    did: 0xF405,
    rawBytes: [0xF4, 0x01],  // 500
    expectedValue: 500,
    type: 'uint16',
    description: 'Uint16 value 500 (500mV)'
  },
  {
    did: 0xF212,  // Saturation count
    rawBytes: [0xFF, 0xFF],  // 65535 (MAX_UINT16)
    expectedValue: 65535,
    type: 'uint16',
    description: 'Uint16 max value'
  }
];

export const UINT8_VECTORS = [
  {
    did: 0xF203,  // Cells Valid
    rawBytes: [0x00],  // 0 (no cells)
    expectedValue: 0,
    type: 'uint8',
    description: 'Uint8 value 0'
  },
  {
    did: 0xF203,
    rawBytes: [0x07],  // 7 (all 3 cells valid)
    expectedValue: 7,
    type: 'uint8',
    description: 'Uint8 value 7 (binary 111)'
  },
  {
    did: 0xF401,  // Cell Type
    rawBytes: [0x01],  // Analog
    expectedValue: 1,
    type: 'uint8',
    description: 'Uint8 value 1 (Analog cell type)'
  },
  {
    did: 0xF401,
    rawBytes: [0xFF],  // 255
    expectedValue: 255,
    type: 'uint8',
    description: 'Uint8 max value'
  }
];

export const BOOL_VECTORS = [
  {
    did: 0xF402,  // Cell Included
    rawBytes: [0x00],  // false
    expectedValue: false,
    type: 'bool',
    description: 'Bool false (0x00)'
  },
  {
    did: 0xF402,
    rawBytes: [0x01],  // true
    expectedValue: true,
    type: 'bool',
    description: 'Bool true (0x01)'
  },
  {
    did: 0xF402,
    rawBytes: [0xFF],  // true (any non-zero)
    expectedValue: true,
    type: 'bool',
    description: 'Bool true (0xFF - any non-zero is true)'
  },
  {
    did: 0xF402,
    rawBytes: [0x42],  // true (any non-zero)
    expectedValue: true,
    type: 'bool',
    description: 'Bool true (0x42 - any non-zero is true)'
  }
];

// Configuration bitfield test vectors
export const CONFIG_VECTORS = [
  {
    bytes: [0x03, 0x00, 0x00, 0x00],  // Firmware version 3
    fields: {
      firmwareVersion: 3,
      cell1: 0,
      cell2: 0,
      cell3: 0,
      powerMode: 0
    },
    description: 'Firmware v3, all defaults'
  },
  {
    bytes: [0x05, 0x15, 0x00, 0x00],  // v5, Cell1=Analog, Cell2=DiveO2, Cell3=Analog, PowerMode=Battery
    fields: {
      firmwareVersion: 5,
      cell1: 1,  // Analog
      cell2: 1,  // Analog
      cell3: 1,  // Analog
      powerMode: 0  // Battery
    },
    description: 'Firmware v5, all analog cells'
  },
  {
    bytes: [0x03, 0xAA, 0x08, 0x03],  // Complex config
    fields: {
      firmwareVersion: 3,
      cell1: 2,  // O2S
      cell2: 2,  // O2S
      cell3: 2,  // O2S
      powerMode: 2,  // CAN
      calibrationMode: 0,
      enableUartPrinting: 1,
      extendedMessages: 1,
      ppo2DepthCompensation: 1
    },
    description: 'O2S cells, CAN power, printing enabled, extended messages'
  }
];

// All vectors combined for iteration
export const ALL_PARSE_VECTORS = [
  ...FLOAT32_VECTORS,
  ...INT32_VECTORS,
  ...UINT32_VECTORS,
  ...INT16_VECTORS,
  ...UINT16_VECTORS,
  ...UINT8_VECTORS,
  ...BOOL_VECTORS
];
