/**
 * ISO-TP (ISO 15765-2) protocol constants
 */

// PCI (Protocol Control Information) types
export const PCI_SF = 0x00;  // Single frame
export const PCI_FF = 0x10;  // First frame
export const PCI_CF = 0x20;  // Consecutive frame
export const PCI_FC = 0x30;  // Flow control

// Flow control status values
export const FC_CTS = 0x00;    // Continue to send
export const FC_WAIT = 0x01;   // Wait
export const FC_OVFLW = 0x02;  // Overflow

// Payload sizes (with extended addressing)
export const SF_MAX_PAYLOAD = 6;      // Single frame max payload
export const FF_FIRST_PAYLOAD = 5;    // First frame first payload
export const CF_PAYLOAD = 6;          // Consecutive frame payload

// Timeouts (in milliseconds)
export const TIMEOUT_N_BS = 1000;  // Waiting for FC after FF
export const TIMEOUT_N_CR = 1000;  // Waiting for CF

// Default flow control parameters
export const DEFAULT_BLOCK_SIZE = 0;   // 0 = infinite
export const DEFAULT_STMIN = 0;        // Minimum separation time (ms)

// Maximum payload size
export const MAX_PAYLOAD = 128;

// State machine states
export const States = {
  IDLE: 'IDLE',
  RECEIVING: 'RECEIVING',
  TRANSMITTING: 'TRANSMITTING',
  WAIT_FC: 'WAIT_FC'
};
