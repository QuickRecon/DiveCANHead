/**
 * DiveCANFramer unit tests
 */
import { describe, it, expect, beforeEach } from 'vitest';
import {
  DiveCANFramer,
  BT_CLIENT_ADDRESS,
  CONTROLLER_ADDRESS,
  MAX_DATAGRAM_SIZE
} from './DiveCANFramer.js';

describe('DiveCANFramer', () => {
  let framer;

  beforeEach(() => {
    framer = new DiveCANFramer();
  });

  describe('constructor', () => {
    it('uses default addresses', () => {
      expect(framer.sourceAddress).toBe(BT_CLIENT_ADDRESS);
      expect(framer.targetAddress).toBe(CONTROLLER_ADDRESS);
    });

    it('accepts custom addresses', () => {
      const custom = new DiveCANFramer(0x01, 0x02);
      expect(custom.sourceAddress).toBe(0x01);
      expect(custom.targetAddress).toBe(0x02);
    });
  });

  describe('sourceAddress setter', () => {
    it('accepts valid address', () => {
      framer.sourceAddress = 0x42;
      expect(framer.sourceAddress).toBe(0x42);
    });

    it('throws for invalid address', () => {
      expect(() => { framer.sourceAddress = -1; }).toThrow();
      expect(() => { framer.sourceAddress = 0x100; }).toThrow();
      expect(() => { framer.sourceAddress = 'invalid'; }).toThrow();
    });
  });

  describe('targetAddress setter', () => {
    it('accepts valid address', () => {
      framer.targetAddress = 0x42;
      expect(framer.targetAddress).toBe(0x42);
    });

    it('throws for invalid address', () => {
      expect(() => { framer.targetAddress = -1; }).toThrow();
      expect(() => { framer.targetAddress = 0x100; }).toThrow();
    });
  });

  describe('frame', () => {
    it('creates datagram with correct header', () => {
      const datagram = framer.frame([0x22, 0xF2, 0x00]);

      expect(datagram[0]).toBe(BT_CLIENT_ADDRESS);  // source
      expect(datagram[1]).toBe(CONTROLLER_ADDRESS);  // target
      expect(datagram[2]).toBe(4);  // length low (payload + 1)
      expect(datagram[3]).toBe(0);  // length high
    });

    it('includes payload after header', () => {
      const datagram = framer.frame([0x22, 0xF2, 0x00]);

      expect(datagram[4]).toBe(0x22);
      expect(datagram[5]).toBe(0xF2);
      expect(datagram[6]).toBe(0x00);
    });

    it('calculates length correctly', () => {
      const payload = new Uint8Array(10);
      const datagram = framer.frame(payload);

      // Length = payload.length + 1 (message type byte)
      expect(datagram[2]).toBe(11);
      expect(datagram[3]).toBe(0);
    });

    it('handles large payloads', () => {
      const payload = new Uint8Array(250);
      const datagram = framer.frame(payload);

      // 251 as 16-bit LE = [251, 0]
      expect(datagram[2]).toBe(251);
      expect(datagram[3]).toBe(0);
    });

    it('throws for empty payload', () => {
      expect(() => framer.frame([])).toThrow();
      expect(() => framer.frame(new Uint8Array(0))).toThrow();
    });

    it('throws for payload exceeding max size', () => {
      const tooLarge = new Uint8Array(MAX_DATAGRAM_SIZE + 1);
      expect(() => framer.frame(tooLarge)).toThrow();
    });

    it('returns Uint8Array', () => {
      const datagram = framer.frame([0x01]);
      expect(datagram).toBeInstanceOf(Uint8Array);
    });

    it('uses custom addresses', () => {
      framer.sourceAddress = 0x01;
      framer.targetAddress = 0x04;
      const datagram = framer.frame([0x22]);

      expect(datagram[0]).toBe(0x01);
      expect(datagram[1]).toBe(0x04);
    });
  });

  describe('parse', () => {
    it('parses valid datagram', () => {
      const datagram = new Uint8Array([
        0x80,  // source
        0xFF,  // target
        0x04,  // length low
        0x00,  // length high
        0x62, 0xF2, 0x00  // payload
      ]);

      const result = framer.parse(datagram);

      expect(result.source).toBe(0x80);
      expect(result.target).toBe(0xFF);
      expect(result.length).toBe(4);
      expect(Array.from(result.payload)).toEqual([0x62, 0xF2, 0x00]);
    });

    it('throws for datagram too short', () => {
      expect(() => framer.parse([0x01, 0x02, 0x03])).toThrow();
      expect(() => framer.parse([])).toThrow();
      expect(() => framer.parse(null)).toThrow();
    });

    it('strips Petrel bridge header [0x01, 0x00]', () => {
      const datagram = new Uint8Array([
        0x01, 0x00,  // Bridge header (outbound)
        0x80, 0xFF,  // source, target
        0x03, 0x00,  // length
        0x62, 0xF2   // payload
      ]);

      const result = framer.parse(datagram);

      expect(result.source).toBe(0x80);
      expect(result.target).toBe(0xFF);
      expect(Array.from(result.payload)).toEqual([0x62, 0xF2]);
    });

    it('strips Petrel bridge header [0x02, 0x00]', () => {
      const datagram = new Uint8Array([
        0x02, 0x00,  // Bridge header (inbound)
        0x80, 0xFF,  // source, target
        0x03, 0x00,  // length
        0x62, 0xF2   // payload
      ]);

      const result = framer.parse(datagram);

      expect(result.source).toBe(0x80);
      expect(result.target).toBe(0xFF);
    });

    it('does not strip non-bridge header', () => {
      const datagram = new Uint8Array([
        0x80, 0xFF,  // source, target (not bridge header)
        0x03, 0x00,  // length
        0x62, 0xF2   // payload
      ]);

      const result = framer.parse(datagram);

      expect(result.source).toBe(0x80);
      expect(result.target).toBe(0xFF);
    });

    it('returns chunk from length high byte', () => {
      const datagram = new Uint8Array([
        0x80, 0xFF,
        0x03, 0x02,  // length low, chunk/length high
        0x62
      ]);

      const result = framer.parse(datagram);
      expect(result.chunk).toBe(0x02);
    });

    it('handles length mismatch gracefully', () => {
      // Header claims 100 bytes but only 3 in payload
      const datagram = new Uint8Array([
        0x80, 0xFF,
        0x64, 0x00,  // length = 100
        0x62, 0xF2, 0x00  // only 3 bytes
      ]);

      const result = framer.parse(datagram);
      // Should use actual payload length
      expect(result.payload.length).toBe(3);
    });
  });

  describe('frame/parse round-trip', () => {
    it('preserves payload', () => {
      const original = new Uint8Array([0x22, 0xF2, 0x00, 0x01, 0x02, 0x03]);
      const datagram = framer.frame(original);
      const parsed = framer.parse(datagram);

      expect(Array.from(parsed.payload)).toEqual(Array.from(original));
    });

    it('preserves addresses', () => {
      framer.sourceAddress = 0x42;
      framer.targetAddress = 0x24;

      const datagram = framer.frame([0x01]);
      const parsed = framer.parse(datagram);

      expect(parsed.source).toBe(0x42);
      expect(parsed.target).toBe(0x24);
    });
  });

  describe('validateAddress (static)', () => {
    it('accepts valid addresses', () => {
      expect(() => DiveCANFramer.validateAddress(0x00)).not.toThrow();
      expect(() => DiveCANFramer.validateAddress(0x80)).not.toThrow();
      expect(() => DiveCANFramer.validateAddress(0xFF)).not.toThrow();
    });

    it('throws for invalid addresses', () => {
      expect(() => DiveCANFramer.validateAddress(-1)).toThrow();
      expect(() => DiveCANFramer.validateAddress(0x100)).toThrow();
      expect(() => DiveCANFramer.validateAddress(null)).toThrow();
      expect(() => DiveCANFramer.validateAddress('0x80')).toThrow();
    });
  });
});
