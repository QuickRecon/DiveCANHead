/**
 * SLIPCodec unit tests
 */
import { describe, it, expect, beforeEach } from 'vitest';
import { SLIPCodec, SLIP_END, SLIP_ESC, SLIP_ESC_END, SLIP_ESC_ESC } from './SLIPCodec.js';
import {
  ENCODE_VECTORS,
  DECODE_VECTORS,
  PARTIAL_DECODE_VECTORS,
  INVALID_DECODE_VECTORS
} from '../../tests/fixtures/slip-test-vectors.js';

describe('SLIPCodec', () => {
  let codec;

  beforeEach(() => {
    codec = new SLIPCodec();
  });

  describe('encode', () => {
    for (const vector of ENCODE_VECTORS) {
      it(vector.description, () => {
        const result = codec.encode(new Uint8Array(vector.input));
        expect(Array.from(result)).toEqual(vector.encoded);
      });
    }

    it('returns Uint8Array', () => {
      const result = codec.encode([0x01, 0x02]);
      expect(result).toBeInstanceOf(Uint8Array);
    });

    it('accepts regular array input', () => {
      const result = codec.encode([0x01, 0x02]);
      expect(Array.from(result)).toEqual([0x01, 0x02, SLIP_END]);
    });
  });

  describe('decode - single packets', () => {
    for (const vector of DECODE_VECTORS) {
      it(vector.description, () => {
        const packets = codec.decode(new Uint8Array(vector.encoded));
        expect(packets.length).toBe(vector.packets.length);
        for (let i = 0; i < packets.length; i++) {
          expect(Array.from(packets[i])).toEqual(vector.packets[i]);
        }
      });
    }

    it('returns empty array when no END byte', () => {
      const packets = codec.decode([0x01, 0x02, 0x03]);
      expect(packets.length).toBe(0);
    });

    it('returns Uint8Array packets', () => {
      const packets = codec.decode([0x01, 0x02, SLIP_END]);
      expect(packets[0]).toBeInstanceOf(Uint8Array);
    });
  });

  describe('decode - partial packets (stateful)', () => {
    for (const vector of PARTIAL_DECODE_VECTORS) {
      it(vector.description, () => {
        for (let i = 0; i < vector.chunks.length; i++) {
          const packets = codec.decode(new Uint8Array(vector.chunks[i]));
          expect(packets.length).toBe(vector.packets[i].length);
          for (let j = 0; j < packets.length; j++) {
            expect(Array.from(packets[j])).toEqual(vector.packets[i][j]);
          }
        }
      });
    }

    it('tracks partial packet state', () => {
      codec.decode([0x01, 0x02]);
      expect(codec.hasPartialPacket).toBe(true);
      expect(codec.bufferSize).toBe(2);
    });

    it('clears state after packet complete', () => {
      codec.decode([0x01, 0x02, SLIP_END]);
      expect(codec.hasPartialPacket).toBe(false);
      expect(codec.bufferSize).toBe(0);
    });
  });

  describe('decode - invalid sequences', () => {
    for (const vector of INVALID_DECODE_VECTORS) {
      it(vector.description, () => {
        if (vector.shouldThrow) {
          expect(() => codec.decode(new Uint8Array(vector.encoded))).toThrow();
        }
      });
    }
  });

  describe('encode/decode round-trip', () => {
    it('preserves simple data', () => {
      const original = new Uint8Array([0x01, 0x02, 0x03, 0x04]);
      const encoded = codec.encode(original);
      const decoded = codec.decode(encoded);
      expect(Array.from(decoded[0])).toEqual(Array.from(original));
    });

    it('preserves data containing special bytes', () => {
      const original = new Uint8Array([SLIP_END, SLIP_ESC, 0x01, SLIP_END]);
      const encoded = codec.encode(original);
      const decoded = codec.decode(encoded);
      expect(Array.from(decoded[0])).toEqual(Array.from(original));
    });

    it('preserves all byte values', () => {
      // Test all 256 byte values
      const original = new Uint8Array(256);
      for (let i = 0; i < 256; i++) {
        original[i] = i;
      }
      const encoded = codec.encode(original);
      const decoded = codec.decode(encoded);
      expect(Array.from(decoded[0])).toEqual(Array.from(original));
    });
  });

  describe('reset', () => {
    it('clears partial packet buffer', () => {
      codec.decode([0x01, 0x02]);
      expect(codec.hasPartialPacket).toBe(true);

      codec.reset();
      expect(codec.hasPartialPacket).toBe(false);
      expect(codec.bufferSize).toBe(0);
    });

    it('clears escape state', () => {
      codec.decode([SLIP_ESC]);  // Leave in escape state
      codec.reset();

      // After reset, normal bytes should work normally
      const packets = codec.decode([0x01, SLIP_END]);
      expect(Array.from(packets[0])).toEqual([0x01]);
    });
  });

  describe('edge cases', () => {
    it('handles empty input', () => {
      const packets = codec.decode([]);
      expect(packets.length).toBe(0);
    });

    it('handles only END byte', () => {
      const packets = codec.decode([SLIP_END]);
      expect(packets.length).toBe(0);  // Empty packet ignored
    });

    it('handles large packet', () => {
      const data = new Uint8Array(1000);
      for (let i = 0; i < 1000; i++) {
        data[i] = i & 0xFF;
      }
      const encoded = codec.encode(data);
      const decoded = codec.decode(encoded);
      expect(decoded[0].length).toBe(1000);
    });

    it('handles packet with only special bytes', () => {
      const original = new Uint8Array([SLIP_END, SLIP_ESC, SLIP_END, SLIP_ESC]);
      const encoded = codec.encode(original);
      // Should be: ESC ESC_END, ESC ESC_ESC, ESC ESC_END, ESC ESC_ESC, END
      expect(encoded.length).toBe(9);
      const decoded = codec.decode(encoded);
      expect(Array.from(decoded[0])).toEqual(Array.from(original));
    });
  });
});
