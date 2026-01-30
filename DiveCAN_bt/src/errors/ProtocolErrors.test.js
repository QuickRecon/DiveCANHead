/**
 * ProtocolErrors unit tests
 */
import { describe, it, expect } from 'vitest';
import {
  ProtocolError,
  BLEError,
  SLIPError,
  DiveCANError,
  ISOTPError,
  UDSError,
  TimeoutError,
  ValidationError
} from './ProtocolErrors.js';

describe('ProtocolError', () => {
  describe('constructor', () => {
    it('sets message', () => {
      const error = new ProtocolError('Test message', 'TEST');
      expect(error.message).toBe('Test message');
    });

    it('sets layer', () => {
      const error = new ProtocolError('Test', 'SLIP');
      expect(error.layer).toBe('SLIP');
    });

    it('sets details', () => {
      const error = new ProtocolError('Test', 'TEST', { foo: 'bar' });
      expect(error.details.foo).toBe('bar');
    });

    it('sets timestamp', () => {
      const before = new Date();
      const error = new ProtocolError('Test', 'TEST');
      const after = new Date();

      expect(error.timestamp.getTime()).toBeGreaterThanOrEqual(before.getTime());
      expect(error.timestamp.getTime()).toBeLessThanOrEqual(after.getTime());
    });

    it('sets cause from details', () => {
      const cause = new Error('Root cause');
      const error = new ProtocolError('Test', 'TEST', { cause });
      expect(error.cause).toBe(cause);
    });

    it('sets name to ProtocolError', () => {
      const error = new ProtocolError('Test', 'TEST');
      expect(error.name).toBe('ProtocolError');
    });
  });

  describe('getFullError', () => {
    it('returns formatted error message', () => {
      const error = new ProtocolError('Test message', 'SLIP');
      expect(error.getFullError()).toBe('[SLIP] Test message');
    });

    it('includes cause in chain', () => {
      const cause = new Error('Root cause');
      const error = new ProtocolError('Wrapper', 'SLIP', { cause });
      const full = error.getFullError();

      expect(full).toContain('[SLIP] Wrapper');
      expect(full).toContain('Caused by:');
      expect(full).toContain('Root cause');
    });

    it('chains nested ProtocolErrors', () => {
      const inner = new ProtocolError('Inner', 'INNER');
      const outer = new ProtocolError('Outer', 'OUTER', { cause: inner });
      const full = outer.getFullError();

      expect(full).toContain('[OUTER] Outer');
      expect(full).toContain('[INNER] Inner');
    });
  });
});

describe('BLEError', () => {
  it('extends ProtocolError', () => {
    const error = new BLEError('Connection failed');
    expect(error).toBeInstanceOf(ProtocolError);
  });

  it('sets layer to BLE', () => {
    const error = new BLEError('Test');
    expect(error.layer).toBe('BLE');
  });

  it('sets name to BLEError', () => {
    const error = new BLEError('Test');
    expect(error.name).toBe('BLEError');
  });
});

describe('SLIPError', () => {
  it('extends ProtocolError', () => {
    const error = new SLIPError('Invalid escape');
    expect(error).toBeInstanceOf(ProtocolError);
  });

  it('sets layer to SLIP', () => {
    const error = new SLIPError('Test');
    expect(error.layer).toBe('SLIP');
  });

  it('sets name to SLIPError', () => {
    const error = new SLIPError('Test');
    expect(error.name).toBe('SLIPError');
  });
});

describe('DiveCANError', () => {
  it('extends ProtocolError', () => {
    const error = new DiveCANError('Invalid address');
    expect(error).toBeInstanceOf(ProtocolError);
  });

  it('sets layer to DiveCAN', () => {
    const error = new DiveCANError('Test');
    expect(error.layer).toBe('DiveCAN');
  });

  it('sets name to DiveCANError', () => {
    const error = new DiveCANError('Test');
    expect(error.name).toBe('DiveCANError');
  });
});

describe('ISOTPError', () => {
  it('extends ProtocolError', () => {
    const error = new ISOTPError('Sequence error');
    expect(error).toBeInstanceOf(ProtocolError);
  });

  it('sets layer to ISO-TP', () => {
    const error = new ISOTPError('Test');
    expect(error.layer).toBe('ISO-TP');
  });

  it('preserves state in details', () => {
    const error = new ISOTPError('Test', { state: 'RX_CONSECUTIVE' });
    expect(error.details.state).toBe('RX_CONSECUTIVE');
  });
});

describe('UDSError', () => {
  describe('constructor', () => {
    it('extends ProtocolError', () => {
      const error = new UDSError('Service error', 0x22);
      expect(error).toBeInstanceOf(ProtocolError);
    });

    it('sets layer to UDS', () => {
      const error = new UDSError('Test', 0x22);
      expect(error.layer).toBe('UDS');
    });

    it('sets SID', () => {
      const error = new UDSError('Test', 0x22);
      expect(error.sid).toBe(0x22);
    });

    it('sets NRC', () => {
      const error = new UDSError('Test', 0x22, 0x31);
      expect(error.nrc).toBe(0x31);
    });

    it('stores SID and NRC in details', () => {
      const error = new UDSError('Test', 0x22, 0x31);
      expect(error.details.sid).toBe(0x22);
      expect(error.details.nrc).toBe(0x31);
    });
  });

  describe('getNRCDescription', () => {
    it('returns "No NRC" for null', () => {
      const error = new UDSError('Test', 0x22);
      expect(error.getNRCDescription()).toBe('No NRC');
    });

    it('returns description for known NRCs', () => {
      const nrcTests = [
        [0x11, 'Service Not Supported'],
        [0x12, 'Subfunction Not Supported'],
        [0x13, 'Incorrect Message Length'],
        [0x22, 'Conditions Not Correct'],
        [0x24, 'Request Sequence Error'],
        [0x31, 'Request Out of Range'],
        [0x33, 'Security Access Denied'],
        [0x72, 'General Programming Failure'],
        [0x73, 'Wrong Block Sequence Counter'],
        [0x78, 'Response Pending']
      ];

      for (const [nrc, expected] of nrcTests) {
        const error = new UDSError('Test', 0x22, nrc);
        expect(error.getNRCDescription()).toContain(expected);
      }
    });

    it('returns Unknown for unknown NRC', () => {
      const error = new UDSError('Test', 0x22, 0x99);
      expect(error.getNRCDescription()).toContain('Unknown');
      expect(error.getNRCDescription()).toContain('0x99');
    });
  });
});

describe('TimeoutError', () => {
  it('extends ProtocolError', () => {
    const error = new TimeoutError('Request timed out', 'UDS');
    expect(error).toBeInstanceOf(ProtocolError);
  });

  it('sets layer from parameter', () => {
    const error = new TimeoutError('Test', 'ISO-TP');
    expect(error.layer).toBe('ISO-TP');
  });

  it('sets name to TimeoutError', () => {
    const error = new TimeoutError('Test', 'UDS');
    expect(error.name).toBe('TimeoutError');
  });

  it('preserves operation in details', () => {
    const error = new TimeoutError('Test', 'UDS', { operation: 'ReadDID' });
    expect(error.details.operation).toBe('ReadDID');
  });
});

describe('ValidationError', () => {
  it('extends ProtocolError', () => {
    const error = new ValidationError('Invalid value', 'DiveCAN');
    expect(error).toBeInstanceOf(ProtocolError);
  });

  it('sets layer from parameter', () => {
    const error = new ValidationError('Test', 'DiveCAN');
    expect(error.layer).toBe('DiveCAN');
  });

  it('sets name to ValidationError', () => {
    const error = new ValidationError('Test', 'UDS');
    expect(error.name).toBe('ValidationError');
  });

  it('preserves field and value in details', () => {
    const error = new ValidationError('Invalid', 'DiveCAN', { field: 'address', value: -1 });
    expect(error.details.field).toBe('address');
    expect(error.details.value).toBe(-1);
  });
});

describe('error hierarchy', () => {
  it('all errors are instanceof Error', () => {
    const errors = [
      new ProtocolError('Test', 'TEST'),
      new BLEError('Test'),
      new SLIPError('Test'),
      new DiveCANError('Test'),
      new ISOTPError('Test'),
      new UDSError('Test', 0x22),
      new TimeoutError('Test', 'UDS'),
      new ValidationError('Test', 'UDS')
    ];

    for (const error of errors) {
      expect(error).toBeInstanceOf(Error);
    }
  });

  it('can catch by ProtocolError', () => {
    try {
      throw new UDSError('Test', 0x22, 0x31);
    } catch (e) {
      if (e instanceof ProtocolError) {
        expect(e.layer).toBe('UDS');
      } else {
        expect.fail('Should be ProtocolError');
      }
    }
  });

  it('can catch by specific type', () => {
    try {
      throw new UDSError('Test', 0x22, 0x31);
    } catch (e) {
      if (e instanceof UDSError) {
        expect(e.nrc).toBe(0x31);
      } else {
        expect.fail('Should be UDSError');
      }
    }
  });
});
