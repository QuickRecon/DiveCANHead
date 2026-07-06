/**
 * MCUBoot image parser / validator.
 *
 * Parses the leading `image_header` of an imgtool-signed MCUBoot image so the
 * client can reject non-images before uploading and display the staged version.
 *
 * image_header layout (bootutil/image.h, all little-endian):
 *   u32 ih_magic          @0   (0x96F3B83D)
 *   u32 ih_load_addr      @4
 *   u16 ih_hdr_size       @8
 *   u16 ih_protect_tlv    @10
 *   u32 ih_img_size       @12
 *   u32 ih_flags          @16
 *   image_version ih_ver  @20  (u8 major, u8 minor, u16 revision LE, u32 build LE)
 */

import { MCUBOOT_IMAGE_MAGIC } from '../uds/constants.js';

const HEADER_MIN_LEN = 28;

/**
 * Parse and validate an MCUBoot image.
 * @param {Uint8Array|ArrayBuffer|Array} input - Raw image bytes
 * @returns {{
 *   valid: boolean,
 *   magic: number,
 *   headerSize: number,
 *   imageSize: number,
 *   byteLength: number,
 *   hashedLength: number,
 *   version: {major: number, minor: number, revision: number, build: number}|null,
 *   reason: string|null
 * }}
 */
export function parseMcubootImage(input) {
  let bytes;
  if (input instanceof Uint8Array) {
    bytes = input;
  } else if (input instanceof ArrayBuffer) {
    bytes = new Uint8Array(input);
  } else {
    bytes = new Uint8Array(input);
  }

  const result = {
    valid: false,
    magic: 0,
    headerSize: 0,
    imageSize: 0,
    byteLength: bytes.length,
    hashedLength: 0,
    version: null,
    reason: null
  };

  if (bytes.length < HEADER_MIN_LEN) {
    result.reason = `File too small (${bytes.length} bytes) to be an MCUBoot image`;
    return result;
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  result.magic = view.getUint32(0, true);

  if (result.magic !== MCUBOOT_IMAGE_MAGIC) {
    result.reason = `Bad MCUBoot magic 0x${result.magic.toString(16).padStart(8, '0')} `
      + `(expected 0x${MCUBOOT_IMAGE_MAGIC.toString(16)})`;
    return result;
  }

  result.headerSize = view.getUint16(8, true);
  result.imageSize = view.getUint32(12, true);
  result.hashedLength = result.headerSize + result.imageSize;
  result.version = {
    major: view.getUint8(20),
    minor: view.getUint8(21),
    revision: view.getUint16(22, true),
    build: view.getUint32(24, true)
  };
  result.valid = true;
  return result;
}

/**
 * Format an MCUBoot / sem_ver version object as "major.minor.revision+build".
 * @param {{major:number,minor:number,revision:number,build:number}|null} v
 * @returns {string}
 */
export function formatVersion(v) {
  if (!v) return 'n/a';
  const base = `${v.major}.${v.minor}.${v.revision}`;
  return v.build ? `${base}+${v.build}` : base;
}
