/**
 * Decoders for the MCUBoot / OTA introspection DIDs.
 *   0xF270 MCUBoot status (16 bytes)
 *   0xF271 POST status (4 bytes)
 *   0xF272/73/74 slot0 / slot1 / factory sem_ver (8 bytes)
 *
 * Mirrors dut.read_mcuboot_status / read_post_status / _semver.
 */

/** MCUBoot swap-type names (BOOT_SWAP_TYPE_*). */
export const SWAP_TYPE_NAMES = ['None', 'Test', 'Permanent', 'Revert', 'Fail'];

/** POST pass_mask bit meanings (0xF271). */
export const POST_PASS_BITS = ['cells', 'consensus', 'ppo2_tx', 'handset', 'solenoid'];

/**
 * POST state enum names (0xF271 byte 0), in PostState_t order
 * (firmware `src/firmware_confirm.h`). Index == raw enum value.
 */
export const POST_STATE_NAMES = [
  'WAITING_CELLS',
  'WAITING_CONSENSUS',
  'WAITING_PPO2_TX',
  'WAITING_HANDSET',
  'WAITING_SOLENOID',
  'CONFIRMED',
  'FAILED_TIMEOUT',
  'FAILED_CELL',
  'FAILED_CONSENSUS',
  'FAILED_NO_PPO2_TX',
  'FAILED_NO_HANDSET',
  'FAILED_SOLENOID'
];

/**
 * Decode a 4-byte packed version (major, minor, revision u16 LE) as embedded in
 * the MCUBoot status DID. All-0xFF means "no valid image in this bank".
 * @param {Uint8Array|Array} d - 4 bytes
 * @returns {{major:number, minor:number, revision:number}|null}
 */
export function decodeVer4(d) {
  if (!d || d.length < 4) return null;
  if (d[0] === 0xFF && d[1] === 0xFF && d[2] === 0xFF && d[3] === 0xFF) return null;
  return { major: d[0], minor: d[1], revision: (d[2] | (d[3] << 8)) >>> 0 };
}

/**
 * Decode an 8-byte sem_ver (major u8, minor u8, revision u16 LE, build u32 LE).
 * All-0xFF means absent/invalid.
 * @param {Uint8Array|Array} d - 8 bytes
 * @returns {{major:number, minor:number, revision:number, build:number}|null}
 */
export function decodeSemVer8(d) {
  if (!d || d.length < 8) return null;
  let allFF = true;
  for (let i = 0; i < 8; i++) {
    if (d[i] !== 0xFF) { allFF = false; break; }
  }
  if (allFF) return null;
  return {
    major: d[0],
    minor: d[1],
    revision: (d[2] | (d[3] << 8)) >>> 0,
    build: (d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)) >>> 0
  };
}

/**
 * Decode the 16-byte MCUBoot status DID (0xF270).
 * @param {Uint8Array|Array} d - 16 bytes
 * @returns {Object|null}
 */
export function decodeMcubootStatus(d) {
  if (!d || d.length < 16) return null;
  return {
    swapType: d[0],
    swapTypeName: SWAP_TYPE_NAMES[d[0]] || `Unknown(${d[0]})`,
    confirmed: d[1] !== 0,
    runningSlot: d[2],
    factoryCaptured: (d[3] & 0x01) !== 0,
    slot0Version: decodeVer4(d.slice(4, 8)),
    slot1Version: decodeVer4(d.slice(8, 12)),
    factoryVersion: decodeVer4(d.slice(12, 16))
  };
}

/**
 * Decode the 4-byte POST status DID (0xF271).
 * @param {Uint8Array|Array} d - 4 bytes
 * @returns {{state:number, stateName:string, passMask:number, passed:string[]}|null}
 */
export function decodePostStatus(d) {
  if (!d || d.length < 2) return null;
  const passMask = d[1];
  const passed = POST_PASS_BITS.filter((_, i) => (passMask & (1 << i)) !== 0);
  return {
    state: d[0],
    stateName: POST_STATE_NAMES[d[0]] || `Unknown(${d[0]})`,
    passMask,
    passed
  };
}
