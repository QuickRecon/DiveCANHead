/**
 * MockBLE - Mock Web Bluetooth API for testing BLEConnection
 */
export class MockBluetoothDevice {
  constructor(name = 'MockDevice', id = 'mock-device-id') {
    this.name = name;
    this.id = id;
    this.gatt = new MockBluetoothRemoteGATTServer(this);
    this.events = {};
  }

  addEventListener(event, callback) {
    if (!this.events[event]) this.events[event] = [];
    this.events[event].push(callback);
  }

  removeEventListener(event, callback) {
    if (!this.events[event]) return;
    this.events[event] = this.events[event].filter(cb => cb !== callback);
  }

  dispatchEvent(eventName, eventData = {}) {
    if (!this.events[eventName]) return;
    this.events[eventName].forEach(cb => cb(eventData));
  }
}

export class MockBluetoothRemoteGATTServer {
  constructor(device) {
    this.device = device;
    this.connected = false;
    this.services = new Map();
  }

  async connect() {
    this.connected = true;
    return this;
  }

  disconnect() {
    this.connected = false;
    this.device.dispatchEvent('gattserverdisconnected');
  }

  async getPrimaryService(uuid) {
    if (!this.services.has(uuid)) {
      this.services.set(uuid, new MockBluetoothRemoteGATTService(this, uuid));
    }
    return this.services.get(uuid);
  }
}

export class MockBluetoothRemoteGATTService {
  constructor(server, uuid) {
    this.server = server;
    this.uuid = uuid;
    this.characteristics = new Map();
  }

  async getCharacteristic(uuid) {
    if (!this.characteristics.has(uuid)) {
      this.characteristics.set(uuid, new MockBluetoothRemoteGATTCharacteristic(this, uuid));
    }
    return this.characteristics.get(uuid);
  }
}

export class MockBluetoothRemoteGATTCharacteristic {
  constructor(service, uuid) {
    this.service = service;
    this.uuid = uuid;
    this.value = null;
    this.events = {};
    this.notificationsStarted = false;
    this.writeQueue = [];
  }

  addEventListener(event, callback) {
    if (!this.events[event]) this.events[event] = [];
    this.events[event].push(callback);
  }

  removeEventListener(event, callback) {
    if (!this.events[event]) return;
    this.events[event] = this.events[event].filter(cb => cb !== callback);
  }

  async startNotifications() {
    this.notificationsStarted = true;
    return this;
  }

  async stopNotifications() {
    this.notificationsStarted = false;
    return this;
  }

  async writeValue(value) {
    this.writeQueue.push(value);
    return;
  }

  async writeValueWithResponse(value) {
    this.writeQueue.push(value);
    return;
  }

  async writeValueWithoutResponse(value) {
    this.writeQueue.push(value);
    return;
  }

  async readValue() {
    return this.value;
  }

  /**
   * Simulate receiving data (triggers characteristicvaluechanged event)
   */
  simulateNotification(data) {
    const buffer = data instanceof ArrayBuffer ? data : data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    this.value = new DataView(buffer);

    const event = {
      target: {
        value: this.value
      }
    };

    if (this.events['characteristicvaluechanged']) {
      this.events['characteristicvaluechanged'].forEach(cb => cb(event));
    }
  }

  getLastWrite() {
    return this.writeQueue.length > 0 ? this.writeQueue[this.writeQueue.length - 1] : null;
  }

  getAllWrites() {
    return [...this.writeQueue];
  }
}

/**
 * Mock navigator.bluetooth for tests
 */
export class MockBluetooth {
  constructor() {
    this.requestedDevice = null;
    this.filters = null;
    this.optionalServices = null;
  }

  async requestDevice(options) {
    this.filters = options.filters;
    this.optionalServices = options.optionalServices;

    if (!this.requestedDevice) {
      this.requestedDevice = new MockBluetoothDevice();
    }
    return this.requestedDevice;
  }

  setMockDevice(device) {
    this.requestedDevice = device;
  }
}

/**
 * Install mock Bluetooth API globally
 */
export function installMockBluetooth() {
  const mockBluetooth = new MockBluetooth();
  globalThis.navigator = globalThis.navigator || {};
  globalThis.navigator.bluetooth = mockBluetooth;
  return mockBluetooth;
}

/**
 * Remove mock Bluetooth API
 */
export function uninstallMockBluetooth() {
  if (globalThis.navigator) {
    delete globalThis.navigator.bluetooth;
  }
}
