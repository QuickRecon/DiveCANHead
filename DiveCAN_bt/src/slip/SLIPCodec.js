/**
 * SLIP (Serial Line Internet Protocol) Codec - RFC 1055
 * Handles encoding and decoding of SLIP frames
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { SLIPError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';

// SLIP special bytes
const SLIP_END = 0xC0;      // Frame delimiter
const SLIP_ESC = 0xDB;      // Escape byte
const SLIP_ESC_END = 0xDC;  // Escaped END
const SLIP_ESC_ESC = 0xDD;  // Escaped ESC

/**
 * SLIP encoder/decoder
 */
export class SLIPCodec {
  constructor() {
    this.logger = new Logger('SLIP', 'debug');
    this.reset();
  }

  /**
   * Reset decoder state
   */
  reset() {
    this.buffer = [];
    this.inEscape = false;
    this.packets = [];
  }

  /**
   * Encode data with SLIP framing
   * @param {Uint8Array|Array} data - Data to encode
   * @returns {Uint8Array} SLIP-encoded data (including END byte)
   */
  encode(data) {
    const encoded = [];

    for (const byte of data) {
      if (byte === SLIP_END) {
        // Escape END byte
        encoded.push(SLIP_ESC, SLIP_ESC_END);
      } else if (byte === SLIP_ESC) {
        // Escape ESC byte
        encoded.push(SLIP_ESC, SLIP_ESC_ESC);
      } else {
        // Normal byte
        encoded.push(byte);
      }
    }

    // Add END byte
    encoded.push(SLIP_END);

    const result = new Uint8Array(encoded);
    this.logger.debug(`SLIP encode: ${data.length} bytes -> ${result.length} bytes`, {
      input: ByteUtils.toHexString(data),
      output: ByteUtils.toHexString(result)
    });

    return result;
  }

  /**
   * Decode SLIP data stream (stateful)
   * Can handle partial packets and multiple packets in one buffer
   * @param {Uint8Array|Array} data - Received data
   * @returns {Uint8Array[]} Array of decoded packets (may be empty)
   */
  decode(data) {
    const packets = [];

    for (let i = 0; i < data.length; i++) {
      const byte = data[i];

      if (byte === SLIP_END) {
        // End of packet
        if (this.buffer.length > 0) {
          const packet = new Uint8Array(this.buffer);
          packets.push(packet);
          this.logger.debug(`SLIP decode: packet complete (${packet.length} bytes)`, {
            packet: ByteUtils.toHexString(packet)
          });
          this.buffer = [];
        }
        this.inEscape = false;
      } else if (byte === SLIP_ESC) {
        // Escape sequence start
        this.inEscape = true;
      } else if (this.inEscape) {
        // Process escaped byte
        if (byte === SLIP_ESC_END) {
          this.buffer.push(SLIP_END);
        } else if (byte === SLIP_ESC_ESC) {
          this.buffer.push(SLIP_ESC);
        } else {
          // Invalid escape sequence
          this.logger.warn(`Invalid SLIP escape sequence: 0x${byte.toString(16)}`);
          throw new SLIPError('Invalid SLIP escape sequence', {
            escapeByte: byte,
            position: i
          });
        }
        this.inEscape = false;
      } else {
        // Normal data byte
        this.buffer.push(byte);
      }
    }

    return packets;
  }

  /**
   * Check if decoder has a partial packet buffered
   * @returns {boolean} True if partial packet exists
   */
  get hasPartialPacket() {
    return this.buffer.length > 0;
  }

  /**
   * Get current buffer size
   * @returns {number} Buffer size in bytes
   */
  get bufferSize() {
    return this.buffer.length;
  }
}

// Export constants for external use
export { SLIP_END, SLIP_ESC, SLIP_ESC_END, SLIP_ESC_ESC };
