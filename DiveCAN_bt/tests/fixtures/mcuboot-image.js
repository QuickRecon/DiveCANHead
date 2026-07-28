/**
 * MCUBoot image test fixtures.
 * Builds a minimal but valid image_header (bootutil/image.h layout).
 */

import { MCUBOOT_IMAGE_MAGIC } from '../../src/uds/constants.js';

function u16le(v, buf, off) { buf[off] = v & 0xFF; buf[off + 1] = (v >> 8) & 0xFF; }
function u32le(v, buf, off) {
  buf[off] = v & 0xFF; buf[off + 1] = (v >> 8) & 0xFF;
  buf[off + 2] = (v >> 16) & 0xFF; buf[off + 3] = (v >> 24) & 0xFF;
}

/**
 * Build a signed-image-like byte array: a valid header + `imageSize` body bytes.
 * @param {Object} [opts]
 * @param {{major,minor,revision,build}} [opts.version]
 * @param {number} [opts.headerSize=32]
 * @param {number} [opts.imageSize=64]
 * @param {number} [opts.trailerSize=48] - extra bytes after header+image (TLVs)
 * @returns {Uint8Array}
 */
export function buildMcubootImage(opts = {}) {
  const version = opts.version || { major: 1, minor: 2, revision: 3, build: 0 };
  const headerSize = opts.headerSize ?? 32;
  const imageSize = opts.imageSize ?? 64;
  const trailerSize = opts.trailerSize ?? 48;
  const total = headerSize + imageSize + trailerSize;

  const buf = new Uint8Array(total);
  u32le(MCUBOOT_IMAGE_MAGIC, buf, 0);   // ih_magic
  u32le(0, buf, 4);                     // ih_load_addr
  u16le(headerSize, buf, 8);            // ih_hdr_size
  u16le(0, buf, 10);                    // ih_protect_tlv_size
  u32le(imageSize, buf, 12);            // ih_img_size
  u32le(0, buf, 16);                    // ih_flags
  buf[20] = version.major;
  buf[21] = version.minor;
  u16le(version.revision, buf, 22);
  u32le(version.build, buf, 24);
  // Fill body with a recognisable pattern
  for (let i = headerSize; i < total; i++) buf[i] = i & 0xFF;
  return buf;
}
