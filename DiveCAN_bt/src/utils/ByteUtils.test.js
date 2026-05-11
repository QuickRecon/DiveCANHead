/**
 * ByteUtils unit tests
 */
import { describe, it, expect } from 'vitest';
import { ByteUtils } from './ByteUtils.js';

describe('ByteUtils', () => {
  describe('toHexString', () => {
    it('converts empty array to empty string', () => {
      expect(ByteUtils.toHexString([])).toBe('');
      expect(ByteUtils.toHexString(new Uint8Array(0))).toBe('');
    });

    it('converts bytes to space-separated hex', () => {
      expect(ByteUtils.toHexString([0x01, 0x02, 0x03])).toBe('01 02 03');
      expect(ByteUtils.toHexString([0xAB, 0xCD, 0xEF])).toBe('ab cd ef');
    });

    it('pads single digit hex values with zero', () => {
      expect(ByteUtils.toHexString([0x00, 0x0F])).toBe('00 0f');
    });

    it('uses custom separator', () => {
      expect(ByteUtils.toHexString([0x01, 0x02], '')).toBe('0102');
      expect(ByteUtils.toHexString([0x01, 0x02], ':')).toBe('01:02');
    });

    it('handles null/undefined', () => {
      expect(ByteUtils.toHexString(null)).toBe('');
      expect(ByteUtils.toHexString(undefined)).toBe('');
    });

    it('works with Uint8Array', () => {
      expect(ByteUtils.toHexString(new Uint8Array([0xFF, 0x00]))).toBe('ff 00');
    });
  });

  describe('fromHexString', () => {
    it('converts hex string to bytes', () => {
      const result = ByteUtils.fromHexString('0102ab');
      expect(Array.from(result)).toEqual([0x01, 0x02, 0xAB]);
    });

    it('handles spaces in hex string', () => {
      const result = ByteUtils.fromHexString('01 02 ab');
      expect(Array.from(result)).toEqual([0x01, 0x02, 0xAB]);
    });

    it('handles colons and other separators', () => {
      const result = ByteUtils.fromHexString('01:02:ab');
      expect(Array.from(result)).toEqual([0x01, 0x02, 0xAB]);
    });

    it('handles mixed case', () => {
      const result = ByteUtils.fromHexString('aAbBcCdD');
      expect(Array.from(result)).toEqual([0xAA, 0xBB, 0xCC, 0xDD]);
    });

    it('returns empty array for empty string', () => {
      const result = ByteUtils.fromHexString('');
      expect(result.length).toBe(0);
    });
  });

  describe('toHexString/fromHexString round-trip', () => {
    it('preserves data through round-trip', () => {
      const original = [0x00, 0x7F, 0x80, 0xFF];
      const hex = ByteUtils.toHexString(original, '');
      const result = ByteUtils.fromHexString(hex);
      expect(Array.from(result)).toEqual(original);
    });
  });

  describe('concat', () => {
    it('concatenates empty arrays', () => {
      const result = ByteUtils.concat([], []);
      expect(result.length).toBe(0);
    });

    it('concatenates single array', () => {
      const result = ByteUtils.concat([1, 2, 3]);
      expect(Array.from(result)).toEqual([1, 2, 3]);
    });

    it('concatenates multiple arrays', () => {
      const result = ByteUtils.concat([1, 2], [3, 4], [5]);
      expect(Array.from(result)).toEqual([1, 2, 3, 4, 5]);
    });

    it('works with Uint8Array', () => {
      const result = ByteUtils.concat(
        new Uint8Array([1, 2]),
        new Uint8Array([3, 4])
      );
      expect(Array.from(result)).toEqual([1, 2, 3, 4]);
    });

    it('returns Uint8Array', () => {
      const result = ByteUtils.concat([1], [2]);
      expect(result).toBeInstanceOf(Uint8Array);
    });
  });

  describe('equals', () => {
    it('returns true for equal arrays', () => {
      expect(ByteUtils.equals([1, 2, 3], [1, 2, 3])).toBe(true);
    });

    it('returns false for different arrays', () => {
      expect(ByteUtils.equals([1, 2, 3], [1, 2, 4])).toBe(false);
    });

    it('returns false for different lengths', () => {
      expect(ByteUtils.equals([1, 2], [1, 2, 3])).toBe(false);
    });

    it('handles null/undefined', () => {
      expect(ByteUtils.equals(null, null)).toBe(true);
      expect(ByteUtils.equals(undefined, undefined)).toBe(true);
      expect(ByteUtils.equals([1], null)).toBe(false);
      expect(ByteUtils.equals(null, [1])).toBe(false);
    });

    it('works with Uint8Array', () => {
      expect(ByteUtils.equals(
        new Uint8Array([1, 2]),
        new Uint8Array([1, 2])
      )).toBe(true);
    });
  });

  describe('slice', () => {
    it('slices array correctly', () => {
      const result = ByteUtils.slice([1, 2, 3, 4], 1, 3);
      expect(Array.from(result)).toEqual([2, 3]);
    });

    it('returns Uint8Array', () => {
      const result = ByteUtils.slice([1, 2, 3], 0, 2);
      expect(result).toBeInstanceOf(Uint8Array);
    });

    it('works with Uint8Array input', () => {
      const result = ByteUtils.slice(new Uint8Array([1, 2, 3, 4]), 1, 3);
      expect(Array.from(result)).toEqual([2, 3]);
    });
  });

  describe('toUint8Array', () => {
    it('returns same Uint8Array if already Uint8Array', () => {
      const input = new Uint8Array([1, 2, 3]);
      const result = ByteUtils.toUint8Array(input);
      expect(result).toBe(input);
    });

    it('converts regular array to Uint8Array', () => {
      const result = ByteUtils.toUint8Array([1, 2, 3]);
      expect(result).toBeInstanceOf(Uint8Array);
      expect(Array.from(result)).toEqual([1, 2, 3]);
    });

    it('converts single number to single-element Uint8Array', () => {
      const result = ByteUtils.toUint8Array(42);
      expect(Array.from(result)).toEqual([42]);
    });

    it('throws for invalid input', () => {
      expect(() => ByteUtils.toUint8Array('invalid')).toThrow();
    });
  });

  describe('extractBits', () => {
    it('extracts bits from byte', () => {
      // 0b11010110 = 0xD6
      expect(ByteUtils.extractBits(0xD6, 0, 1)).toBe(0);  // bit 0
      expect(ByteUtils.extractBits(0xD6, 1, 1)).toBe(1);  // bit 1
      expect(ByteUtils.extractBits(0xD6, 0, 4)).toBe(6);  // bits 0-3 = 0110
      expect(ByteUtils.extractBits(0xD6, 4, 4)).toBe(13); // bits 4-7 = 1101
    });

    it('extracts multi-bit fields', () => {
      expect(ByteUtils.extractBits(0xFF, 0, 8)).toBe(255);
      expect(ByteUtils.extractBits(0xAA, 0, 2)).toBe(2);  // 0b10
      expect(ByteUtils.extractBits(0xAA, 2, 2)).toBe(2);  // 0b10
    });
  });

  describe('uint16ToBE', () => {
    it('converts 16-bit value to big-endian bytes', () => {
      expect(Array.from(ByteUtils.uint16ToBE(0x1234))).toEqual([0x12, 0x34]);
      expect(Array.from(ByteUtils.uint16ToBE(0x0001))).toEqual([0x00, 0x01]);
      expect(Array.from(ByteUtils.uint16ToBE(0xFFFF))).toEqual([0xFF, 0xFF]);
    });

    it('handles zero', () => {
      expect(Array.from(ByteUtils.uint16ToBE(0))).toEqual([0x00, 0x00]);
    });
  });

  describe('uint32ToBE', () => {
    it('converts 32-bit value to big-endian bytes', () => {
      expect(Array.from(ByteUtils.uint32ToBE(0x12345678))).toEqual([0x12, 0x34, 0x56, 0x78]);
      expect(Array.from(ByteUtils.uint32ToBE(0x00000001))).toEqual([0x00, 0x00, 0x00, 0x01]);
    });

    it('handles max value', () => {
      expect(Array.from(ByteUtils.uint32ToBE(0xFFFFFFFF))).toEqual([0xFF, 0xFF, 0xFF, 0xFF]);
    });
  });

  describe('beToUint16', () => {
    it('converts big-endian bytes to 16-bit value', () => {
      expect(ByteUtils.beToUint16([0x12, 0x34])).toBe(0x1234);
      expect(ByteUtils.beToUint16([0x00, 0x01])).toBe(0x0001);
      expect(ByteUtils.beToUint16([0xFF, 0xFF])).toBe(0xFFFF);
    });

    it('handles zero', () => {
      expect(ByteUtils.beToUint16([0x00, 0x00])).toBe(0);
    });
  });

  describe('beToUint32', () => {
    it('converts big-endian bytes to 32-bit value', () => {
      expect(ByteUtils.beToUint32([0x12, 0x34, 0x56, 0x78])).toBe(0x12345678);
      expect(ByteUtils.beToUint32([0x00, 0x00, 0x00, 0x01])).toBe(0x00000001);
    });

    it('handles max value', () => {
      // Note: JavaScript bitwise ops return signed 32-bit, so use >>> 0 for unsigned
      expect(ByteUtils.beToUint32([0xFF, 0xFF, 0xFF, 0xFF]) >>> 0).toBe(0xFFFFFFFF);
    });
  });

  describe('uint16ToBE/beToUint16 round-trip', () => {
    it('preserves values through round-trip', () => {
      const values = [0, 1, 255, 256, 0x1234, 0xFFFF];
      for (const value of values) {
        const bytes = ByteUtils.uint16ToBE(value);
        const result = ByteUtils.beToUint16(bytes);
        expect(result).toBe(value);
      }
    });
  });

  describe('beToUint64', () => {
    it('converts 8 bytes to BigInt', () => {
      const bytes = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01];
      expect(ByteUtils.beToUint64(bytes)).toBe(1n);
    });

    it('handles larger values', () => {
      const bytes = [0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00];
      expect(ByteUtils.beToUint64(bytes)).toBe(0x100000000n);
    });

    it('handles max value', () => {
      const bytes = [0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF];
      expect(ByteUtils.beToUint64(bytes)).toBe(0xFFFFFFFFFFFFFFFFn);
    });

    it('handles shorter arrays (pads with zeros)', () => {
      const bytes = [0x01, 0x02];
      // Should be padded: [0x01, 0x02, 0, 0, 0, 0, 0, 0]
      expect(ByteUtils.beToUint64(bytes)).toBe(0x0102000000000000n);
    });
  });

  describe('uint64ToBE', () => {
    it('converts BigInt to 8 bytes', () => {
      const result = ByteUtils.uint64ToBE(1n);
      expect(Array.from(result)).toEqual([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]);
    });

    it('converts number to 8 bytes', () => {
      const result = ByteUtils.uint64ToBE(256);
      expect(Array.from(result)).toEqual([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00]);
    });

    it('handles large values', () => {
      const result = ByteUtils.uint64ToBE(0x123456789ABCDEFn);
      expect(Array.from(result)).toEqual([0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF]);
    });
  });

  describe('uint64ToBE/beToUint64 round-trip', () => {
    it('preserves values through round-trip', () => {
      const values = [0n, 1n, 0xFFFFFFFFn, 0x123456789ABCDEFn, 0xFFFFFFFFFFFFFFFFn];
      for (const value of values) {
        const bytes = ByteUtils.uint64ToBE(value);
        const result = ByteUtils.beToUint64(bytes);
        expect(result).toBe(value);
      }
    });
  });
});
