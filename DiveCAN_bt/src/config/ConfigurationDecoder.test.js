/**
 * ConfigurationDecoder unit tests
 */
import { describe, it, expect, beforeEach } from 'vitest';
import { ConfigurationDecoder, CONFIG_FIELDS } from './ConfigurationDecoder.js';
import { CONFIG_VECTORS } from '../../tests/fixtures/did-test-vectors.js';

describe('ConfigurationDecoder', () => {
  let decoder;

  beforeEach(() => {
    decoder = new ConfigurationDecoder();
  });

  describe('constructor', () => {
    it('initializes with zeros', () => {
      expect(decoder.toBytes()).toEqual([0, 0, 0, 0]);
    });
  });

  describe('fromBytes', () => {
    it('loads 4-byte array', () => {
      decoder.fromBytes([0x01, 0x02, 0x03, 0x04]);
      expect(decoder.toBytes()).toEqual([0x01, 0x02, 0x03, 0x04]);
    });

    it('returns this for chaining', () => {
      const result = decoder.fromBytes([0, 0, 0, 0]);
      expect(result).toBe(decoder);
    });

    it('throws for wrong length array', () => {
      expect(() => decoder.fromBytes([1, 2, 3])).toThrow();
      expect(() => decoder.fromBytes([1, 2, 3, 4, 5])).toThrow();
    });

    it('throws for non-array', () => {
      expect(() => decoder.fromBytes(null)).toThrow();
      expect(() => decoder.fromBytes('1234')).toThrow();
    });

    it('masks bytes to 0xFF', () => {
      decoder.fromBytes([0x100, 0x1FF, -1, 256]);
      // Should mask to lower 8 bits
      expect(decoder.toBytes()).toEqual([0x00, 0xFF, 0xFF, 0x00]);
    });
  });

  describe('toBytes', () => {
    it('returns copy of bytes', () => {
      decoder.fromBytes([1, 2, 3, 4]);
      const bytes = decoder.toBytes();
      bytes[0] = 99;  // Modify copy
      expect(decoder.toBytes()[0]).toBe(1);  // Original unchanged
    });
  });

  describe('toUint32', () => {
    it('returns little-endian 32-bit value', () => {
      decoder.fromBytes([0x78, 0x56, 0x34, 0x12]);
      expect(decoder.toUint32()).toBe(0x12345678);
    });

    it('returns 0 for zero bytes', () => {
      expect(decoder.toUint32()).toBe(0);
    });
  });

  describe('toHexString', () => {
    it('returns big-endian hex string', () => {
      decoder.fromBytes([0x03, 0x02, 0x01, 0x00]);
      expect(decoder.toHexString()).toBe('0x00010203');
    });

    it('pads with zeros', () => {
      decoder.fromBytes([0x00, 0x00, 0x00, 0x00]);
      expect(decoder.toHexString()).toBe('0x00000000');
    });
  });

  describe('getField', () => {
    it('extracts firmwareVersion (byte 0, bits 0-7)', () => {
      decoder.fromBytes([0x05, 0x00, 0x00, 0x00]);
      expect(decoder.getField('firmwareVersion')).toBe(5);
    });

    it('extracts cell1 type (byte 1, bits 0-1)', () => {
      decoder.fromBytes([0x00, 0x01, 0x00, 0x00]);  // 0b01
      expect(decoder.getField('cell1')).toBe(1);

      decoder.fromBytes([0x00, 0x02, 0x00, 0x00]);  // 0b10
      expect(decoder.getField('cell1')).toBe(2);
    });

    it('extracts cell2 type (byte 1, bits 2-3)', () => {
      decoder.fromBytes([0x00, 0x04, 0x00, 0x00]);  // 0b0100
      expect(decoder.getField('cell2')).toBe(1);

      decoder.fromBytes([0x00, 0x08, 0x00, 0x00]);  // 0b1000
      expect(decoder.getField('cell2')).toBe(2);
    });

    it('extracts cell3 type (byte 1, bits 4-5)', () => {
      decoder.fromBytes([0x00, 0x10, 0x00, 0x00]);  // 0b00010000
      expect(decoder.getField('cell3')).toBe(1);
    });

    it('extracts powerMode (byte 1, bits 6-7)', () => {
      decoder.fromBytes([0x00, 0x40, 0x00, 0x00]);  // 0b01000000
      expect(decoder.getField('powerMode')).toBe(1);

      decoder.fromBytes([0x00, 0xC0, 0x00, 0x00]);  // 0b11000000
      expect(decoder.getField('powerMode')).toBe(3);
    });

    it('extracts enableUartPrinting (byte 2, bit 3)', () => {
      decoder.fromBytes([0x00, 0x00, 0x08, 0x00]);
      expect(decoder.getField('enableUartPrinting')).toBe(1);

      decoder.fromBytes([0x00, 0x00, 0x00, 0x00]);
      expect(decoder.getField('enableUartPrinting')).toBe(0);
    });

    it('extracts extendedMessages (byte 3, bit 0)', () => {
      decoder.fromBytes([0x00, 0x00, 0x00, 0x01]);
      expect(decoder.getField('extendedMessages')).toBe(1);
    });

    it('returns undefined for unknown field', () => {
      expect(decoder.getField('unknownField')).toBeUndefined();
    });
  });

  describe('setField', () => {
    it('sets firmwareVersion', () => {
      // firmwareVersion is not editable, should return false
      const result = decoder.setField('firmwareVersion', 10);
      expect(result).toBe(false);
    });

    it('sets cell1 type', () => {
      const result = decoder.setField('cell1', 2);
      expect(result).toBe(true);
      expect(decoder.getField('cell1')).toBe(2);
    });

    it('preserves other bits when setting field', () => {
      decoder.fromBytes([0x00, 0xFF, 0x00, 0x00]);
      decoder.setField('cell1', 0);
      // Should clear bits 0-1, keep other bits
      expect(decoder.toBytes()[1]).toBe(0xFC);
    });

    it('returns false for unknown field', () => {
      const result = decoder.setField('unknownField', 1);
      expect(result).toBe(false);
    });

    it('returns false for out-of-range value', () => {
      // cell1 is 2 bits, max value is 3
      const result = decoder.setField('cell1', 4);
      expect(result).toBe(false);
    });

    it('returns false for negative value', () => {
      const result = decoder.setField('cell1', -1);
      expect(result).toBe(false);
    });

    it('returns false for non-editable field', () => {
      const result = decoder.setField('firmwareVersion', 1);
      expect(result).toBe(false);
    });
  });

  describe('getAllFields', () => {
    it('returns all field values with metadata', () => {
      decoder.fromBytes([0x05, 0x15, 0x00, 0x00]);
      const fields = decoder.getAllFields();

      expect(fields.firmwareVersion.value).toBe(5);
      expect(fields.firmwareVersion.editable).toBe(false);
      expect(fields.cell1.value).toBe(1);
      expect(fields.cell1.editable).toBe(true);
    });

    it('includes all defined fields', () => {
      const fields = decoder.getAllFields();
      for (const fieldName of Object.keys(CONFIG_FIELDS)) {
        expect(fields[fieldName]).toBeDefined();
      }
    });
  });

  describe('getFieldLabel', () => {
    it('returns label for enum field', () => {
      decoder.fromBytes([0x00, 0x01, 0x00, 0x00]);
      expect(decoder.getFieldLabel('cell1')).toBe('Analog');

      decoder.fromBytes([0x00, 0x02, 0x00, 0x00]);
      expect(decoder.getFieldLabel('cell1')).toBe('O2S');
    });

    it('returns Enabled/Disabled for bool field', () => {
      decoder.fromBytes([0x00, 0x00, 0x08, 0x00]);
      expect(decoder.getFieldLabel('enableUartPrinting')).toBe('Enabled');

      decoder.fromBytes([0x00, 0x00, 0x00, 0x00]);
      expect(decoder.getFieldLabel('enableUartPrinting')).toBe('Disabled');
    });

    it('returns string value for number field', () => {
      decoder.fromBytes([0x05, 0x00, 0x00, 0x00]);
      expect(decoder.getFieldLabel('firmwareVersion')).toBe('5');
    });

    it('returns Unknown for invalid enum value', () => {
      decoder.fromBytes([0x00, 0x03, 0x00, 0x00]);
      // cell1 = 3 is not a valid option
      expect(decoder.getFieldLabel('cell1')).toContain('Unknown');
    });

    it('returns empty string for unknown field', () => {
      expect(decoder.getFieldLabel('unknownField')).toBe('');
    });
  });

  describe('clone', () => {
    it('creates independent copy', () => {
      decoder.fromBytes([1, 2, 3, 4]);
      const copy = decoder.clone();

      copy.fromBytes([5, 6, 7, 8]);
      expect(decoder.toBytes()).toEqual([1, 2, 3, 4]);
      expect(copy.toBytes()).toEqual([5, 6, 7, 8]);
    });

    it('returns ConfigurationDecoder instance', () => {
      const copy = decoder.clone();
      expect(copy).toBeInstanceOf(ConfigurationDecoder);
    });
  });

  describe('test vectors', () => {
    for (const vector of CONFIG_VECTORS) {
      it(vector.description, () => {
        decoder.fromBytes(vector.bytes);
        for (const [field, expectedValue] of Object.entries(vector.fields)) {
          expect(decoder.getField(field)).toBe(expectedValue);
        }
      });
    }
  });

  describe('round-trip', () => {
    it('preserves all field values through set/get', () => {
      decoder.fromBytes([0, 0, 0, 0]);

      // Set all editable fields
      decoder.setField('cell1', 2);
      decoder.setField('cell2', 1);
      decoder.setField('cell3', 2);
      decoder.setField('powerMode', 3);
      decoder.setField('calibrationMode', 2);
      decoder.setField('enableUartPrinting', 1);
      decoder.setField('extendedMessages', 1);

      // Verify
      expect(decoder.getField('cell1')).toBe(2);
      expect(decoder.getField('cell2')).toBe(1);
      expect(decoder.getField('cell3')).toBe(2);
      expect(decoder.getField('powerMode')).toBe(3);
      expect(decoder.getField('calibrationMode')).toBe(2);
      expect(decoder.getField('enableUartPrinting')).toBe(1);
      expect(decoder.getField('extendedMessages')).toBe(1);
    });
  });

  describe('CONFIG_FIELDS structure', () => {
    it('all fields have required properties', () => {
      for (const [name, field] of Object.entries(CONFIG_FIELDS)) {
        expect(field.byte).toBeTypeOf('number');
        expect(field.bitStart).toBeTypeOf('number');
        expect(field.bitWidth).toBeTypeOf('number');
        expect(field.label).toBeTypeOf('string');
        expect(field.editable).toBeTypeOf('boolean');
        expect(field.type).toBeTypeOf('string');
      }
    });

    it('enum fields have options', () => {
      for (const [name, field] of Object.entries(CONFIG_FIELDS)) {
        if (field.type === 'enum') {
          expect(field.options).toBeInstanceOf(Array);
          expect(field.options.length).toBeGreaterThan(0);
          for (const option of field.options) {
            expect(option.value).toBeTypeOf('number');
            expect(option.label).toBeTypeOf('string');
          }
        }
      }
    });

    it('bit ranges do not exceed byte boundaries', () => {
      for (const [name, field] of Object.entries(CONFIG_FIELDS)) {
        expect(field.bitStart + field.bitWidth).toBeLessThanOrEqual(8);
      }
    });

    it('bytes are in valid range', () => {
      for (const [name, field] of Object.entries(CONFIG_FIELDS)) {
        expect(field.byte).toBeGreaterThanOrEqual(0);
        expect(field.byte).toBeLessThan(4);
      }
    });
  });
});
