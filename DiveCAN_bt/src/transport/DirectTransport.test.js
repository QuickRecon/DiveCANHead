/**
 * DirectTransport unit tests
 */
import { describe, it, expect, beforeEach, vi } from 'vitest';
import { DirectTransport } from './DirectTransport.js';

describe('DirectTransport', () => {
  let transport;

  beforeEach(() => {
    transport = new DirectTransport();
  });

  describe('constructor', () => {
    it('uses default addresses', () => {
      expect(transport.sourceAddress).toBe(0xFF);
      expect(transport.targetAddress).toBe(0x80);
    });

    it('accepts custom addresses', () => {
      const custom = new DirectTransport(0x01, 0x04);
      expect(custom.sourceAddress).toBe(0x01);
      expect(custom.targetAddress).toBe(0x04);
    });

    it('accepts options', () => {
      const custom = new DirectTransport(0xFF, 0x80, { debug: true });
      expect(custom.options.debug).toBe(true);
    });
  });

  describe('event handling', () => {
    it('registers and calls event handlers', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      transport.emit('message', new Uint8Array([0x01]));

      expect(handler).toHaveBeenCalledWith(new Uint8Array([0x01]));
    });

    it('supports multiple handlers', () => {
      const handler1 = vi.fn();
      const handler2 = vi.fn();
      transport.on('message', handler1);
      transport.on('message', handler2);

      transport.emit('message', new Uint8Array([0x01]));

      expect(handler1).toHaveBeenCalled();
      expect(handler2).toHaveBeenCalled();
    });

    it('removes handler with off()', () => {
      const handler = vi.fn();
      transport.on('message', handler);
      transport.off('message', handler);

      transport.emit('message', new Uint8Array([0x01]));

      expect(handler).not.toHaveBeenCalled();
    });

    it('removes all handlers with removeAllListeners()', () => {
      const handler = vi.fn();
      transport.on('message', handler);
      transport.removeAllListeners('message');

      transport.emit('message', new Uint8Array([0x01]));

      expect(handler).not.toHaveBeenCalled();
    });

    it('returns this for chaining', () => {
      const result = transport.on('message', () => {});
      expect(result).toBe(transport);
    });
  });

  describe('send', () => {
    it('emits frame event with data', async () => {
      const handler = vi.fn();
      transport.on('frame', handler);

      await transport.send([0x22, 0xF2, 0x00]);

      expect(handler).toHaveBeenCalledWith(expect.any(Uint8Array));
      expect(Array.from(handler.mock.calls[0][0])).toEqual([0x22, 0xF2, 0x00]);
    });

    it('accepts array input', async () => {
      const handler = vi.fn();
      transport.on('frame', handler);

      await transport.send([0x01, 0x02]);

      expect(handler).toHaveBeenCalled();
    });

    it('accepts Uint8Array input', async () => {
      const handler = vi.fn();
      transport.on('frame', handler);

      await transport.send(new Uint8Array([0x01, 0x02]));

      expect(handler).toHaveBeenCalled();
    });
  });

  describe('processFrame', () => {
    it('emits message event with payload', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      transport.processFrame([0x62, 0xF2, 0x00, 0x01, 0x02]);

      expect(handler).toHaveBeenCalled();
      expect(Array.from(handler.mock.calls[0][0])).toEqual([0x62, 0xF2, 0x00, 0x01, 0x02]);
    });

    it('strips ISO-TP padding byte', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      // Payload with 0x00 padding followed by valid SID 0x62
      transport.processFrame([0x00, 0x62, 0xF2, 0x00, 0x01]);

      // Should strip leading 0x00
      expect(Array.from(handler.mock.calls[0][0])).toEqual([0x62, 0xF2, 0x00, 0x01]);
    });

    it('does not strip non-padding byte', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      // First byte is 0x62 (valid SID), not padding
      transport.processFrame([0x62, 0xF2, 0x00]);

      expect(Array.from(handler.mock.calls[0][0])).toEqual([0x62, 0xF2, 0x00]);
    });

    it('strips padding for response SIDs (0x50-0x7F)', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      // Padding + 0x62 (positive response)
      transport.processFrame([0x00, 0x62, 0xF2]);

      expect(handler.mock.calls[0][0][0]).toBe(0x62);
    });

    it('strips padding for negative response (0x7F)', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      // Padding + negative response
      transport.processFrame([0x00, 0x7F, 0x22, 0x31]);

      expect(Array.from(handler.mock.calls[0][0])).toEqual([0x7F, 0x22, 0x31]);
    });

    it('strips padding for request SIDs (0x10-0x3E)', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      // Padding + 0x22 (RDBI request)
      transport.processFrame([0x00, 0x22, 0xF2, 0x00]);

      expect(handler.mock.calls[0][0][0]).toBe(0x22);
    });

    it('does not strip 0x00 when followed by invalid SID', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      // 0x00 followed by 0x01 (not a valid SID)
      transport.processFrame([0x00, 0x01, 0x02]);

      // Should keep the 0x00
      expect(Array.from(handler.mock.calls[0][0])).toEqual([0x00, 0x01, 0x02]);
    });

    it('accepts Uint8Array input', () => {
      const handler = vi.fn();
      transport.on('message', handler);

      transport.processFrame(new Uint8Array([0x62, 0xF2, 0x00]));

      expect(handler).toHaveBeenCalled();
    });
  });

  describe('state properties', () => {
    it('state is always IDLE', () => {
      expect(transport.state).toBe('IDLE');
    });

    it('isIdle is always true', () => {
      expect(transport.isIdle).toBe(true);
    });
  });

  describe('reset', () => {
    it('does not throw', () => {
      expect(() => transport.reset()).not.toThrow();
    });
  });
});
