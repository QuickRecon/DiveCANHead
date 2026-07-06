import { describe, it, expect } from 'vitest';
import { parseMcubootImage, formatVersion } from './McubootImage.js';
import { buildMcubootImage } from '../../tests/fixtures/mcuboot-image.js';

describe('McubootImage', () => {
  it('parses a valid image header', () => {
    const img = buildMcubootImage({
      version: { major: 1, minor: 2, revision: 3, build: 7 },
      headerSize: 32, imageSize: 64, trailerSize: 48
    });
    const parsed = parseMcubootImage(img);
    expect(parsed.valid).toBe(true);
    expect(parsed.magic).toBe(0x96F3B83D);
    expect(parsed.headerSize).toBe(32);
    expect(parsed.imageSize).toBe(64);
    expect(parsed.byteLength).toBe(32 + 64 + 48);
    expect(parsed.hashedLength).toBe(32 + 64);
    expect(parsed.version).toEqual({ major: 1, minor: 2, revision: 3, build: 7 });
  });

  it('rejects a bad magic', () => {
    const img = buildMcubootImage();
    img[0] = 0x00; // corrupt magic
    const parsed = parseMcubootImage(img);
    expect(parsed.valid).toBe(false);
    expect(parsed.reason).toMatch(/magic/i);
  });

  it('rejects a too-small file', () => {
    const parsed = parseMcubootImage(new Uint8Array([1, 2, 3]));
    expect(parsed.valid).toBe(false);
    expect(parsed.reason).toMatch(/too small/i);
  });

  it('formats versions with and without build', () => {
    expect(formatVersion({ major: 1, minor: 2, revision: 3, build: 0 })).toBe('1.2.3');
    expect(formatVersion({ major: 1, minor: 2, revision: 3, build: 9 })).toBe('1.2.3+9');
    expect(formatVersion(null)).toBe('n/a');
  });
});
