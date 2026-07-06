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
export { DirectTransport } from './transport/DirectTransport.js';
export { UDSClient } from './uds/UDSClient.js';

// Firmware update (OTA) + flash-log download
export { OTAManager, OTA_TIMEOUTS } from './firmware/OTAManager.js';
export { parseMcubootImage, formatVersion } from './firmware/McubootImage.js';
export {
  decodeMcubootStatus, decodePostStatus, decodeSemVer8, decodeVer4,
  SWAP_TYPE_NAMES, POST_PASS_BITS
} from './firmware/McubootStatus.js';
export { LogDownloader, LOG_TIMEOUTS } from './logs/LogDownloader.js';
export {
  parseLogStream, parseDclgHeader, decodeRecord, makeRecordCounter,
  decodeBootMarker, decodeDiveMarker, decodeCanFrame, decodeLogText, decodeConsensus
} from './logs/LogParser.js';
export { toJSON as logToJSON, toCSV as logToCSV, toRawBin as logToRawBin, triggerDownload } from './logs/LogExport.js';

// Utilities
export { ByteUtils } from './utils/ByteUtils.js';
export { Logger } from './utils/Logger.js';
export { Timeout, TimeoutManager } from './utils/Timeout.js';

// Errors
export * from './errors/ProtocolErrors.js';

// Constants
export * as DiveCANConstants from './divecan/constants.js';
export * as UDSConstants from './uds/constants.js';

// Version
export const VERSION = '1.0.0';
