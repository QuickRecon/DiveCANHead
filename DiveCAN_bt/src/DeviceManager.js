/**
 * Device Manager
 * Handles BLE device discovery and management
 */

import { BLEConnection, PETREL_SERVICE_UUID } from './ble/BLEConnection.js';
import { DiveCANProtocolStack } from './DiveCANProtocolStack.js';
import { Logger } from './utils/Logger.js';

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
 * Device Manager
 */
export class DeviceManager extends EventEmitter {
  constructor() {
    super();
    this.logger = new Logger('DeviceManager', 'debug');
    this.discoveredDevices = [];
  }

  /**
   * Scan for BLE devices
   * @param {number} timeout - Scan timeout in ms (default: 10000)
   * @returns {Promise<Array>} Array of discovered devices
   */
  async scan(timeout = 10000) {
    this.logger.info('Starting device scan...');

    if (!BLEConnection.isAvailable()) {
      throw new Error('Web Bluetooth not available');
    }

    try {
      const filters = {
        filters: [{ services: [PETREL_SERVICE_UUID] }]
      };

      const device = await Promise.race([
        navigator.bluetooth.requestDevice(filters),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('Scan timeout')), timeout)
        )
      ]);

      if (device) {
        const deviceInfo = DeviceManager.getDeviceInfo(device);
        this.discoveredDevices = [deviceInfo];
        this.emit('deviceDiscovered', deviceInfo);
        this.logger.info(`Found device: ${deviceInfo.name}`);
      }

      this.emit('scanComplete', this.discoveredDevices);
      return this.discoveredDevices;

    } catch (error) {
      if (error.name === 'NotFoundError') {
        this.logger.info('Scan canceled by user');
        this.emit('scanComplete', []);
        return [];
      }
      this.logger.error('Scan failed', error);
      throw error;
    }
  }

  /**
   * Get list of discovered devices
   * @returns {Array} Discovered devices
   */
  getDevices() {
    return this.discoveredDevices;
  }

  /**
   * Create protocol stack for device
   * @param {BluetoothDevice|Object} device - Device or device info
   * @param {Object} options - Stack options
   * @returns {DiveCANProtocolStack} Protocol stack
   */
  createStack(device, options = {}) {
    this.logger.info(`Creating protocol stack for ${device.name || device.id}`);
    return new DiveCANProtocolStack(options);
  }

  /**
   * Check if device is a Petrel 3 (or compatible)
   * @param {BluetoothDevice|Object} device - Device to check
   * @returns {boolean} True if Petrel 3
   */
  static isPetrel3(device) {
    if (!device) return false;

    const name = device.name || '';

    // Check for Petrel in the name
    return name.toLowerCase().includes('petrel');
  }

  /**
   * Get device information
   * @param {BluetoothDevice} device - BLE device
   * @returns {Object} Device info
   */
  static getDeviceInfo(device) {
    return {
      id: device.id,
      name: device.name || 'Unknown',
      isPetrel3: DeviceManager.isPetrel3(device),
      device: device  // Store original device object
    };
  }
}
