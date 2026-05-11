/**
 * SLIP encoding/decoding test vectors
 *
 * Format: { name, input, encoded, description }
 * - input: raw bytes before encoding
 * - encoded: bytes after SLIP encoding (with END byte)
 */

// SLIP special bytes
export const SLIP_END = 0xC0;
export const SLIP_ESC = 0xDB;
export const SLIP_ESC_END = 0xDC;
export const SLIP_ESC_ESC = 0xDD;

export const ENCODE_VECTORS = [
  {
    name: 'simple',
    input: [0x01, 0x02, 0x03],
    encoded: [0x01, 0x02, 0x03, SLIP_END],
    description: 'Simple data without special bytes'
  },
  {
    name: 'empty',
    input: [],
    encoded: [SLIP_END],
    description: 'Empty data produces only END byte'
  },
  {
    name: 'escape_end',
    input: [0x01, SLIP_END, 0x02],
    encoded: [0x01, SLIP_ESC, SLIP_ESC_END, 0x02, SLIP_END],
    description: 'END byte in data is escaped'
  },
  {
    name: 'escape_esc',
    input: [0x01, SLIP_ESC, 0x02],
    encoded: [0x01, SLIP_ESC, SLIP_ESC_ESC, 0x02, SLIP_END],
    description: 'ESC byte in data is escaped'
  },
  {
    name: 'multiple_escapes',
    input: [SLIP_END, SLIP_ESC, SLIP_END, SLIP_ESC],
    encoded: [
      SLIP_ESC, SLIP_ESC_END,
      SLIP_ESC, SLIP_ESC_ESC,
      SLIP_ESC, SLIP_ESC_END,
      SLIP_ESC, SLIP_ESC_ESC,
      SLIP_END
    ],
    description: 'Multiple special bytes are all escaped'
  },
  {
    name: 'consecutive_ends',
    input: [SLIP_END, SLIP_END],
    encoded: [SLIP_ESC, SLIP_ESC_END, SLIP_ESC, SLIP_ESC_END, SLIP_END],
    description: 'Consecutive END bytes'
  },
  {
    name: 'only_end',
    input: [SLIP_END],
    encoded: [SLIP_ESC, SLIP_ESC_END, SLIP_END],
    description: 'Single END byte as data'
  },
  {
    name: 'only_esc',
    input: [SLIP_ESC],
    encoded: [SLIP_ESC, SLIP_ESC_ESC, SLIP_END],
    description: 'Single ESC byte as data'
  }
];

export const DECODE_VECTORS = [
  {
    name: 'simple',
    encoded: [0x01, 0x02, 0x03, SLIP_END],
    packets: [[0x01, 0x02, 0x03]],
    description: 'Simple single packet'
  },
  {
    name: 'escaped_end',
    encoded: [0x01, SLIP_ESC, SLIP_ESC_END, 0x02, SLIP_END],
    packets: [[0x01, SLIP_END, 0x02]],
    description: 'Escaped END byte'
  },
  {
    name: 'escaped_esc',
    encoded: [0x01, SLIP_ESC, SLIP_ESC_ESC, 0x02, SLIP_END],
    packets: [[0x01, SLIP_ESC, 0x02]],
    description: 'Escaped ESC byte'
  },
  {
    name: 'multiple_packets',
    encoded: [0x01, 0x02, SLIP_END, 0x03, 0x04, SLIP_END],
    packets: [[0x01, 0x02], [0x03, 0x04]],
    description: 'Multiple packets in one buffer'
  },
  {
    name: 'empty_packet_skip',
    encoded: [SLIP_END, 0x01, 0x02, SLIP_END],
    packets: [[0x01, 0x02]],
    description: 'Leading END byte is skipped (empty packet ignored)'
  },
  {
    name: 'consecutive_ends',
    encoded: [0x01, SLIP_END, SLIP_END, 0x02, SLIP_END],
    packets: [[0x01], [0x02]],
    description: 'Consecutive END bytes produce separate packets'
  }
];

export const PARTIAL_DECODE_VECTORS = [
  {
    name: 'split_packet',
    chunks: [
      [0x01, 0x02],
      [0x03, SLIP_END]
    ],
    packets: [
      [],  // First chunk: no complete packet yet
      [[0x01, 0x02, 0x03]]  // Second chunk: packet complete
    ],
    description: 'Packet split across two chunks'
  },
  {
    name: 'split_escape',
    chunks: [
      [0x01, SLIP_ESC],  // ESC at end
      [SLIP_ESC_END, 0x02, SLIP_END]  // Escaped byte at start
    ],
    packets: [
      [],
      [[0x01, SLIP_END, 0x02]]
    ],
    description: 'Escape sequence split across chunks'
  },
  {
    name: 'multi_chunk_multi_packet',
    chunks: [
      [0x01, SLIP_END, 0x02],
      [0x03, SLIP_END, 0x04],
      [SLIP_END]
    ],
    packets: [
      [[0x01]],
      [[0x02, 0x03]],
      [[0x04]]
    ],
    description: 'Multiple packets across multiple chunks'
  }
];

export const INVALID_DECODE_VECTORS = [
  {
    name: 'invalid_escape',
    encoded: [0x01, SLIP_ESC, 0x00, SLIP_END],
    description: 'Invalid escape sequence (ESC followed by non-escape byte)',
    shouldThrow: true
  },
  {
    name: 'invalid_escape_normal',
    encoded: [SLIP_ESC, 0x41, SLIP_END],
    description: 'ESC followed by normal byte',
    shouldThrow: true
  }
];
