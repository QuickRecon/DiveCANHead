/**
 * BLE Communication Layer using Web Bluetooth API
 * Manages connection to Petrel 3 Bluetooth bridge
 */

import { ByteUtils } from '../utils/ByteUtils.js';
import { BLEError } from '../errors/ProtocolErrors.js';
import { Logger } from '../utils/Logger.js';

// Petrel 3 service and characteristic UUIDs
const PETREL_SERVICE_UUID = 'fe25c237-0ece-443c-b0aa-e02033e7029d';
const PETREL_CHAR_UUID = '27b7570b-359e-45a3-91bb-cf7e70049bd2';
const MODEL_NBR_CHAR_UUID = '00002a24-0000-1000-8000-00805f9b34fb';

// BLE packet header (prepended to all packets)
const BLE_PACKET_HEADER = new Uint8Array([0x01, 0x00]);

// Default MTU (Maximum Transmission Unit)
const DEFAULT_MTU = 20;

/**
 * Simple EventEmitter implementation for browser
 */
class EventEmitter {
  constructor() {
    this.events = {};
  }

  on(event, callback) {
    if (!this.events[event]) {
      this.events[event] = [];
    }
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
    if (event) {
      delete this.events[event];
    } else {
      this.events = {};
    }
    return this;
  }
}

/**
 * BLE Connection manager
 */
export class BLEConnection extends EventEmitter {
  /**
   * Create a BLE connection
   * @param {Object} options - Configuration options
   * @param {number} options.mtu - Maximum transmission unit (default: 20)
   * @param {boolean} options.autoReconnect - Auto-reconnect on disconnect (default: false)
   */
  constructor(options = {}) {
    super();
    this.logger = new Logger('BLE', 'debug');
    this.options = {
      mtu: DEFAULT_MTU,
      autoReconnect: false,
      ...options
    };

    this.device = null;
    this.server = null;
    this.service = null;
    this.characteristic = null;
    this._isConnected = false;
  }

  /**
   * Check if Web Bluetooth is available
   * @returns {boolean} True if available
   */
  static isAvailable() {
    return typeof navigator !== 'undefined' && navigator.bluetooth !== undefined;
  }

  /**
   * Scan for BLE devices
   * @param {Object} filters - Optional filters
   * @param {number} timeout - Scan timeout in ms (default: 10000)
   * @returns {Promise<Array>} Array of discovered devices
   */
  async scan(filters = {}, timeout = 10000) {
    if (!BLEConnection.isAvailable()) {
      throw new BLEError('Web Bluetooth not available', {
        hint: 'Use Chrome, Edge, or Opera browser'
      });
    }

    this.logger.info('Starting BLE scan...');

    try {
      // Default filter: Petrel service UUID
      const defaultFilters = {
        filters: [{ services: [PETREL_SERVICE_UUID] }]
      };

      const scanOptions = { ...defaultFilters, ...filters };

      // Request device with timeout
      const device = await Promise.race([
        navigator.bluetooth.requestDevice(scanOptions),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('Scan timeout')), timeout)
        )
      ]);

      this.logger.info(`Found device: ${device.name || 'Unknown'}`);
      return [device];

    } catch (error) {
      if (error.name === 'NotFoundError') {
        this.logger.warn('No device selected');
        return [];
      }
      throw new BLEError('Scan failed', {
        cause: error,
        message: error.message
      });
    }
  }

  /**
   * Connect to a BLE device
   * @param {BluetoothDevice} device - Device to connect to
   * @returns {Promise<void>}
   */
  async connect(device) {
    if (this._isConnected) {
      this.logger.warn('Already connected');
      return;
    }

    this.logger.info(`Connecting to ${device.name || device.id}...`);

    try {
      this.device = device;

      // Set up disconnect handler
      device.addEventListener('gattserverdisconnected', () => {
        this._handleDisconnect();
      });

      // Connect to GATT server
      this.server = await device.gatt.connect();
      this.logger.debug('GATT server connected');

      // Get primary service
      this.service = await this.server.getPrimaryService(PETREL_SERVICE_UUID);
      this.logger.debug('Got primary service');

      // Get characteristic
      this.characteristic = await this.service.getCharacteristic(PETREL_CHAR_UUID);
      this.logger.debug('Got characteristic');

      // Start notifications
      await this.characteristic.startNotifications();
      this.characteristic.addEventListener('characteristicvaluechanged', (event) => {
        this._handleData(event.target.value);
      });
      this.logger.debug('Notifications started');

      // Try to read model number (optional)
      try {
        const modelChar = await this.server.getPrimaryService('0000180a-0000-1000-8000-00805f9b34fb')
          .then(service => service.getCharacteristic(MODEL_NBR_CHAR_UUID));
        const modelValue = await modelChar.readValue();
        const modelNumber = new TextDecoder().decode(modelValue);
        this.logger.info(`Connected to: ${modelNumber}`);
      } catch (e) {
        // Intentionally ignored: model number is optional, not all devices support it
        this.logger.debug(`Could not read model number: ${e.message}`);
      }

      this._isConnected = true;
      this.emit('connected');
      this.logger.info('Connected successfully');

    } catch (error) {
      this._isConnected = false;
      this.device = null;
      this.server = null;
      this.service = null;
      this.characteristic = null;

      throw new BLEError('Connection failed', {
        cause: error,
        deviceName: device.name || device.id
      });
    }
  }

  /**
   * Disconnect from device
   * @returns {Promise<void>}
   */
  async disconnect() {
    if (!this._isConnected) {
      return;
    }

    this.logger.info('Disconnecting...');

    try {
      if (this.server?.connected) {
        this.server.disconnect();
      }
    } catch (error) {
      this.logger.error('Error during disconnect', error);
    }

    this._handleDisconnect();
  }

  /**
   * Write data to BLE characteristic
   * Automatically prepends BLE packet header [0x01, 0x00]
   * @param {Uint8Array|Array} data - Data to write
   * @returns {Promise<void>}
   */
  async write(data) {
    if (!this._isConnected) {
      throw new BLEError('Not connected');
    }

    // Convert to Uint8Array if needed
    const dataArray = ByteUtils.toUint8Array(data);

    // Prepend header: [0x01, 0x00] + data
    const packet = ByteUtils.concat(BLE_PACKET_HEADER, dataArray);

    // Check MTU
    if (packet.length > this.options.mtu) {
      throw new BLEError(`Packet too large: ${packet.length} bytes (MTU: ${this.options.mtu})`, {
        packetLength: packet.length,
        mtu: this.options.mtu
      });
    }

    this.logger.debug(`BLE write: ${packet.length} bytes`, {
      data: ByteUtils.toHexString(packet)
    });

    try {
      await this.characteristic.writeValueWithoutResponse(packet);
    } catch (error) {
      throw new BLEError('Write failed', {
        cause: error,
        data: ByteUtils.toHexString(packet)
      });
    }
  }

  /**
   * Handle incoming data from BLE characteristic
   * Strips BLE packet header [0x01, 0x00] before emitting
   * @private
   */
  _handleData(value) {
    const data = new Uint8Array(value.buffer);

    this.logger.debug(`BLE received: ${data.length} bytes`, {
      data: ByteUtils.toHexString(data)
    });

    // Strip header [0x01, 0x00] if present
    let payload = data;
    if (data.length >= 2 && data[0] === 0x01 && data[1] === 0x00) {
      payload = data.slice(2);
    }

    this.emit('data', payload);
  }

  /**
   * Handle disconnect event
   * @private
   */
  _handleDisconnect() {
    const wasConnected = this._isConnected;

    this._isConnected = false;
    this.characteristic = null;
    this.service = null;
    this.server = null;

    if (wasConnected) {
      this.logger.info('Disconnected');
      this.emit('disconnected');

      if (this.options.autoReconnect && this.device) {
        this.logger.info('Auto-reconnecting...');
        setTimeout(() => {
          this.connect(this.device).catch(error => {
            this.logger.error('Auto-reconnect failed', error);
          });
        }, 1000);
      }
    }
  }

  /**
   * Check if connected
   * @returns {boolean} True if connected
   */
  get isConnected() {
    return this._isConnected && this.server?.connected;
  }

  /**
   * Get device info
   * @returns {Object|null} Device information
   */
  get deviceInfo() {
    if (!this.device) return null;
    return {
      id: this.device.id,
      name: this.device.name,
      connected: this._isConnected
    };
  }

  /**
   * Get MTU
   * @returns {number} MTU in bytes
   */
  get mtu() {
    return this.options.mtu;
  }
}

// Export constants
export { PETREL_SERVICE_UUID, PETREL_CHAR_UUID, BLE_PACKET_HEADER, DEFAULT_MTU };
