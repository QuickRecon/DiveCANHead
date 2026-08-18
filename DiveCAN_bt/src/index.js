/**
 * DiveCAN Protocol Stack for Browser
 * Main export file
 */

// Main classes
export { DiveCANProtocolStack } from './DiveCANProtocolStack.js';
export { CanableProtocolStack } from './CanableProtocolStack.js';
export { CanableConnection } from './can/CanableConnection.js';
export { IsoTpCanTransport } from './transport/IsoTpCanTransport.js';
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
  SWAP_TYPE_NAMES, POST_PASS_BITS, POST_STATE_NAMES
} from './firmware/McubootStatus.js';
export {
  OP_ERRORS, OP_ERROR_COUNT, decodeErrorHistogram, summarizeErrorHistogram
} from './errors/ErrorHistogram.js';
export {
  LogDownloader,
  LogDownloadIncompleteError,
  LogResumeMismatchError,
  LOG_DOWNLOAD_MAX_BYTES,
  LOG_PROGRESS_INTERVAL_MS,
  LOG_PROGRESS_MIN_BYTES,
  LOG_RETRY_DEFAULTS,
  LOG_TIMEOUTS
} from './logs/LogDownloader.js';
export { MemoryLogDownloadStore, OPFSLogDownloadStore } from './logs/LogDownloadStore.js';
export {
  parseLogStream, parseDclgHeader, decodeRecord, makeRecordCounter,
  decodeBootMarker, decodeDiveMarker, decodeCanFrame, decodeLogText, decodeConsensus,
  decodePidSnapshot, decodeSolenoidFire, decodeSolenoidCurrent,
  decodeCellDiveO2, decodeCellO2S, decodeCellAnalog,
  decodeErrorEvent, decodeDropMarker,
  unpackConsensusStatus, consensusStatusArray, consensusIncludeArray,
  PPO2_CBAR_PER_BAR, MILLIVOLT_LSB_PER_MV, DIVEO2_TEMP_LSB_PER_DEGC,
  DIVEO2_PRESSURE_LSB_PER_MBAR, DIVEO2_HUMIDITY_LSB_PER_PCT, MBAR_PER_METRE
} from './logs/LogParser.js';
export { toJSON as logToJSON, toCSV as logToCSV, toRawBin as logToRawBin, triggerDownload } from './logs/LogExport.js';

// Telemetry viewer: channel model, decode-to-typed-arrays, decimation, overlay
export { AXES, TABLES, CELL_COUNT, SERIES_COLOURS, formatElapsed } from './telemetry/TelemetryModel.js';
export {
  buildTelemetry, buildDiveWindows, applySurfaceReference, transferablesOf, EPOCH_GAP_S
} from './telemetry/TelemetryBuilder.js';
export {
  buildDrawData, decimateChannel, buildGrid, annotateIntervals,
  medianInterval, lowerBound, nearestIndex, bucketCountFor
} from './telemetry/TelemetrySeries.js';
export {
  eventOverlayPlugin, hitTest, hitToleranceS, errorColour, errorName,
  errorDescription, markerLabel, pxRatioOf
} from './telemetry/EventOverlay.js';
export { csvToStream } from './telemetry/CsvSource.js';
export { TelemetryViewer } from './telemetry/TelemetryViewer.js';

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
