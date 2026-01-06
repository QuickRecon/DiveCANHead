/**
 * DiveCAN Protocol Stack for Browser
 * Main export file
 */

// Main classes
export { DiveCANProtocolStack } from './DiveCANProtocolStack.js';
export { DeviceManager } from './DeviceManager.js';

// Individual layers (for advanced usage)
export { BLEConnection } from './ble/BLEConnection.js';
export { SLIPCodec } from './slip/SLIPCodec.js';
export { DiveCANFramer } from './divecan/DiveCANFramer.js';
export { ISOTPTransport } from './isotp/ISOTPTransport.js';
export { UDSClient } from './uds/UDSClient.js';

// Utilities
export { ByteUtils } from './utils/ByteUtils.js';
export { Logger } from './utils/Logger.js';
export { Timeout, TimeoutManager } from './utils/Timeout.js';

// Errors
export * from './errors/ProtocolErrors.js';

// Constants
export * as DiveCANConstants from './divecan/constants.js';
export * as ISOTPConstants from './isotp/constants.js';
export * as UDSConstants from './uds/constants.js';

// Version
export const VERSION = '1.0.0';
