import { describe, it, expect } from 'vitest';
import {
  decodeMcubootStatus, decodePostStatus, decodeSemVer8, decodeVer4
} from './McubootStatus.js';

describe('McubootStatus', () => {
  it('decodes a 16-byte MCUBoot status', () => {
    const d = new Uint8Array([
      1,          // swap_type = TEST
      0,          // confirmed = false
      0,          // running_slot = 0
      0x01,       // factory_captured bit0
      1, 2, 3, 0, // slot0 ver4 = 1.2.3
      0xFF, 0xFF, 0xFF, 0xFF, // slot1 = none
      2, 0, 0, 0  // factory ver4 = 2.0.0
    ]);
    const s = decodeMcubootStatus(d);
    expect(s.swapType).toBe(1);
    expect(s.swapTypeName).toBe('Test');
    expect(s.confirmed).toBe(false);
    expect(s.factoryCaptured).toBe(true);
    expect(s.slot0Version).toEqual({ major: 1, minor: 2, revision: 3 });
    expect(s.slot1Version).toBeNull();
    expect(s.factoryVersion).toEqual({ major: 2, minor: 0, revision: 0 });
  });

  it('decodes an 8-byte sem_ver (revision u16 LE, build u32 LE)', () => {
    // 1.2, revision=0x0102=258, build=0x00000005=5
    const d = new Uint8Array([1, 2, 0x02, 0x01, 0x05, 0, 0, 0]);
    expect(decodeSemVer8(d)).toEqual({ major: 1, minor: 2, revision: 258, build: 5 });
  });

  it('returns null for an all-0xFF sem_ver', () => {
    expect(decodeSemVer8(new Uint8Array(8).fill(0xFF))).toBeNull();
    expect(decodeVer4(new Uint8Array(4).fill(0xFF))).toBeNull();
  });

  it('decodes POST status pass mask', () => {
    // pass_mask 0b10101 = cells + ppo2_tx + solenoid
    const p = decodePostStatus(new Uint8Array([3, 0b10101, 0, 0]));
    expect(p.state).toBe(3);
    expect(p.passed).toEqual(['cells', 'ppo2_tx', 'solenoid']);
  });
});
