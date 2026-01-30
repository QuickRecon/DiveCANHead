/**
 * DiveCAN protocol constants
 */

// Device addresses
export const BT_CLIENT_ADDRESS = 0xFF;
export const CONTROLLER_ADDRESS = 0x80;  // Petrel dive computer
export const SOLO_ADDRESS = 0x04;        // SOLO board
export const HUD_ADDRESS = 0x02;         // HUD display
export const NERD_ADDRESS = 0x01;        // NERD 2

// Maximum datagram size
export const MAX_DATAGRAM_SIZE = 256;

// Message types
export const MSG_TYPE_UDS = 0x00;
