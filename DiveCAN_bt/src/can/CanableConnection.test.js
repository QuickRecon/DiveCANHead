import { describe, it, expect, vi } from 'vitest';
import { CanableConnection } from './CanableConnection.js';

describe('CanableConnection', () => {
  it('encodes extended slcan frames', async () => {
    const c = new CanableConnection(); c.writer = { write: vi.fn() };
    await c.sendFrame(0x0D0A04FF, new Uint8Array([2, 0, 0x10]));
    expect(new TextDecoder().decode(c.writer.write.mock.calls[0][0])).toBe('T0D0A04FF3020010\r');
  });
  it('parses extended slcan frames', () => {
    const c = new CanableConnection(), frames = []; c.on('frame', f => frames.push(f));
    c._consume('T0D0AFF04430000000\r');
    expect(frames[0]).toMatchObject({ id: 0x0D0AFF04, extended: true });
    expect([...frames[0].data]).toEqual([0x30, 0, 0, 0]);
  });
});
