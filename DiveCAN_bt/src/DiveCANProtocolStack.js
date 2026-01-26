/**
 * DiveCAN Protocol Stack
 * Coordinates all protocol layers: BLE -> SLIP -> DiveCAN -> UDS
 *
 * Note: The Petrel 3 BLE bridge handles ISO-TP internally.
 * We send raw UDS payloads wrapped in DiveCAN datagrams.
 */

import { BLEConnection } from './ble/BLEConnection.js';
import { SLIPCodec } from './slip/SLIPCodec.js';
import { DiveCANFramer, TESTER_ADDRESS, CONTROLLER_ADDRESS } from './divecan/DiveCANFramer.js';
import { DirectTransport } from './transport/DirectTransport.js';
import { UDSClient } from './uds/UDSClient.js';
import { Logger } from './utils/Logger.js';
import { ByteUtils } from './utils/ByteUtils.js';

/**
 * Simple EventEmitter
 */
class EventEmitter {
  constructor() {
    this.events = {};
  }
  on(event, callback) {
    if (!this.events[event]) this.events[event] = [];
    this.events[event].push(callback);
    return this;
  }
  off(event, callback) {
    if (!this.events[event]) return this;
    this.events[event] = this.events[event].filter(cb => cb !== callback);
    return this;
  }
  emit(event, ...args) {
    if (!this.events[event]) return;
    this.events[event].forEach(callback => {
      try {
        callback(...args);
      } catch (error) {
        console.error(`Error in event handler for ${event}:`, error);
      }
    });
  }
  removeAllListeners(event) {
    if (event) delete this.events[event];
    else this.events = {};
    return this;
  }
}

/**
 * DiveCAN Protocol Stack
 */
export class DiveCANProtocolStack extends EventEmitter {
  /**
   * Create protocol stack
   * @param {Object} options - Configuration options
   */
  constructor(options = {}) {
    super();
    this.logger = new Logger('ProtocolStack', 'debug');
    this.options = options;

    // Create layers
    this._ble = new BLEConnection(options.ble);
    this._slip = new SLIPCodec();
    this._divecan = new DiveCANFramer(
      options.sourceAddress || TESTER_ADDRESS,
      options.targetAddress || CONTROLLER_ADDRESS
    );
    // Use DirectTransport - Petrel handles ISO-TP
    this._transport = new DirectTransport(
      options.sourceAddress || TESTER_ADDRESS,
      options.targetAddress || CONTROLLER_ADDRESS,
      options.transport
    );
    this._uds = new UDSClient(this._transport, options.uds);

    this._wireUpLayers();
  }

  /**
   * Wire up data flow between layers
   * @private
   */
  _wireUpLayers() {
    // BLE -> SLIP -> DiveCAN -> Transport -> UDS
    this._ble.on('data', (data) => {
      try {
        this.logger.debug(`BLE RX: ${ByteUtils.toHexString(data)}`);

        // SLIP decode
        const packets = this._slip.decode(data);

        for (const packet of packets) {
          this.logger.debug(`SLIP decoded: ${ByteUtils.toHexString(packet)}`);

          // DiveCAN parse
          const datagram = this._divecan.parse(packet);
          this.logger.debug(`DiveCAN parsed: src=0x${datagram.source.toString(16)}, dst=0x${datagram.target.toString(16)}, payload=${ByteUtils.toHexString(datagram.payload)}`);

          // Transport process (passes to UDS)
          this._transport.processFrame(datagram.payload);

          // Emit raw data event
          this.emit('data', { layer: 'BLE', data: packet });
        }
      } catch (error) {
        this.logger.error('Error processing received data', error);
        this.emit('error', error);
      }
    });

    // Transport -> DiveCAN -> SLIP -> BLE
    this._transport.on('frame', async (udsPayload) => {
      try {
        this.logger.debug(`Transport TX: ${ByteUtils.toHexString(udsPayload)}`);

        // DiveCAN frame (wraps UDS payload)
        const datagram = this._divecan.frame(udsPayload);
        this.logger.debug(`DiveCAN framed: ${ByteUtils.toHexString(datagram)}`);

        // SLIP encode
        const slipData = this._slip.encode(datagram);
        this.logger.debug(`SLIP encoded: ${ByteUtils.toHexString(slipData)}`);

        // BLE write
        await this._ble.write(slipData);

        // Emit frame event
        this.emit('frame', { layer: 'Transport', frame: udsPayload });
      } catch (error) {
        this.logger.error('Error sending frame', error);
        this.emit('error', error);
      }
    });

    // Forward connection events
    this._ble.on('connected', () => {
      this.logger.info('Protocol stack connected');
      this.emit('connected');
    });

    this._ble.on('disconnected', () => {
      this.logger.info('Protocol stack disconnected');
      this.emit('disconnected');

      // Reset layers
      this._slip.reset();
      this._transport.reset();
    });

    // Forward errors from all layers
    this._ble.on('error', (error) => this.emit('error', error));
    this._transport.on('error', (error) => this.emit('error', error));
    this._uds.on('error', (error) => this.emit('error', error));

    // Forward UDS events
    this._uds.on('response', (response) => this.emit('udsResponse', response));
    this._uds.on('negativeResponse', (nrc) => this.emit('udsNegativeResponse', nrc));
  }

  /**
   * Discover BLE devices
   * @param {Object} filters - Optional BLE filters
   * @param {number} timeout - Scan timeout in ms
   * @returns {Promise<Array>} Array of devices
   */
  async discoverDevices(filters = {}, timeout = 10000) {
    this.logger.info('Discovering devices...');
    return await this._ble.scan(filters, timeout);
  }

  /**
   * Connect to device
   * @param {BluetoothDevice} device - Device to connect to
   * @returns {Promise<void>}
   */
  async connect(device) {
    this.logger.info('Connecting protocol stack...');

    try {
      await this._ble.connect(device);
      this.logger.info('Protocol stack connected successfully');
    } catch (error) {
      this.logger.error('Protocol stack connection failed', error);
      throw error;
    }
  }

  /**
   * Disconnect from device
   * @returns {Promise<void>}
   */
  async disconnect() {
    this.logger.info('Disconnecting protocol stack...');
    await this._ble.disconnect();
  }

  /**
   * Set target device address for communication
   * @param {number} address - Target device address (e.g., 0x80 for Controller, 0x04 for SOLO)
   */
  setTargetAddress(address) {
    this._divecan.targetAddress = address;
    this.logger.info(`Target address changed to 0x${address.toString(16).padStart(2, '0')}`);
  }

  /**
   * Get current target address
   * @returns {number} Current target address
   */
  get targetAddress() {
    return this._divecan.targetAddress;
  }

  /**
   * Convenience method: Read serial number
   * @returns {Promise<string>} Serial number
   */
  async readSerialNumber() {
    return await this._uds.readSerialNumber();
  }

  /**
   * Convenience method: Read model name
   * @returns {Promise<string>} Model name
   */
  async readModel() {
    return await this._uds.readModel();
  }

  /**
   * Convenience method: Enumerate devices on the DiveCAN bus
   * @returns {Promise<Array<number>>} Array of device IDs
   */
  async enumerateBusDevices() {
    return await this._uds.enumerateBusDevices();
  }

  /**
   * Convenience method: Get device name by ID
   * @param {number} deviceId - Device ID
   * @returns {Promise<string>} Device name
   */
  async getDeviceName(deviceId) {
    return await this._uds.getDeviceName(deviceId);
  }

  /**
   * Convenience method: Read hardware version
   * @returns {Promise<number>} Hardware version
   */
  async readHardwareVersion() {
    return await this._uds.readHardwareVersion();
  }

  /**
   * Convenience method: Read configuration
   * @returns {Promise<Uint8Array>} Configuration data
   */
  async readConfiguration() {
    return await this._uds.readConfiguration();
  }

  /**
   * Convenience method: Write configuration
   * @param {Uint8Array|Array} config - Configuration data
   * @returns {Promise<void>}
   */
  async writeConfiguration(config) {
    await this._uds.writeConfiguration(config);
  }

  /**
   * Convenience method: Upload memory
   * @param {number} address - Memory address
   * @param {number} length - Number of bytes
   * @param {Function} progressCallback - Progress callback
   * @returns {Promise<Uint8Array>} Memory data
   */
  async uploadMemory(address, length, progressCallback = null) {
    const wrappedCallback = progressCallback ? (current, total) => {
      this.emit('progress', { current, total, percent: (current / total) * 100 });
      progressCallback(current, total);
    } : null;

    return await this._uds.uploadMemory(address, length, wrappedCallback);
  }

  /**
   * Convenience method: Download memory
   * @param {number} address - Memory address
   * @param {Uint8Array|Array} data - Data to write
   * @param {Function} progressCallback - Progress callback
   * @returns {Promise<void>}
   */
  async downloadMemory(address, data, progressCallback = null) {
    const wrappedCallback = progressCallback ? (current, total) => {
      this.emit('progress', { current, total, percent: (current / total) * 100 });
      progressCallback(current, total);
    } : null;

    await this._uds.downloadMemory(address, data, wrappedCallback);
  }

  // Layer accessors
  get ble() { return this._ble; }
  get slip() { return this._slip; }
  get divecan() { return this._divecan; }
  get transport() { return this._transport; }
  get uds() { return this._uds; }

  /**
   * Check if connected
   * @returns {boolean} True if connected
   */
  get isConnected() {
    return this._ble.isConnected;
  }

  /**
   * Get connection info
   * @returns {Object|null} Connection information
   */
  get connectionInfo() {
    if (!this._ble.isConnected) return null;

    return {
      device: this._ble.deviceInfo,
      transportState: this._transport.state
    };
  }
}
