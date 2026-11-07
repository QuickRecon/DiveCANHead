import { beforeEach, afterEach, describe, expect, it, vi } from 'vitest';
import * as udsConstants from '../src/uds/constants.js';
import html from '../examples/diagnostics.html?raw';

const pageScript = html.match(/<script type="module">([\s\S]*?)<\/script>/)[1]
  .replace(/^\s*import .+;$/gm, '');

// Run the real page handlers and markup, replacing only their module imports.
// No Bluetooth device, serial port, Chart.js CDN, or hardware writes are used.
const $ = id => document.getElementById(id);
const flush = async () => { for (let i = 0; i < 20; i++) await Promise.resolve(); };
let page;
let stacks;
let scan;
let connect;
let didTargets;
let windowListeners;

class MockStack {
  constructor(options = {}, type = 'usb') {
    this.options = options;
    this.targetAddress = options.targetAddress;
    this.isConnected = false;
    this.events = {};
    this._uds = { owner: this };
    this.uds = this._uds;
    if (type === 'ble') this.ble = {};
    else this.canable = { port: {}, options: options.canable };
    this.setTargetAddress = vi.fn(address => { this.targetAddress = address; });
    stacks.push(this);
  }
  on(event, handler) { (this.events[event] ??= []).push(handler); }
  async emit(event, ...args) {
    await Promise.all((this.events[event] ?? []).map(handler => handler(...args)));
  }
  async connect(argument) {
    await connect(this, argument);
    this.isConnected = true;
    await this.emit('connected');
  }
  async disconnect() {
    this.isConnected = false;
    await this.emit('disconnected');
  }
}

class MockDataStore {
  constructor(options = {}) { this.udsClient = options.udsClient; }
  async initialize() { didTargets.push(this.udsClient.owner.targetAddress); }
  getAllDIDValues() { return {}; }
  stopPolling() {}
  async pausePolling() {}
  resumePolling() {}
}

beforeEach(() => {
  vi.useFakeTimers();
  localStorage.clear();
  document.documentElement.innerHTML = html;
  stacks = [];
  didTargets = [];
  scan = vi.fn().mockResolvedValue([{ name: 'Petrel', device: { name: 'Petrel' } }]);
  connect = vi.fn().mockResolvedValue(undefined);
  vi.stubGlobal('matchMedia', () => ({ matches: false }));
  vi.stubGlobal('navigator', { serial: {}, bluetooth: {} });
  vi.spyOn(console, 'error').mockImplementation(() => {});
  windowListeners = [];
  const addListener = window.addEventListener.bind(window);
  vi.spyOn(window, 'addEventListener').mockImplementation((...args) => {
    windowListeners.push(args);
    addListener(...args);
  });
  const imports = {
    ...udsConstants,
    DeviceManager: class {
      scan() { return scan(); }
      createStack(_device, options) { return new MockStack(options, 'ble'); }
    },
    CanableProtocolStack: MockStack,
    DataStore: MockDataStore,
    PlotManager: class { init() {} setDataStore() {} start() {} },
    CellUIAdapter: class { initPlotButtons() {} },
    Chart: {},
    Logger: { setGlobalLevel() {}, setSink() {} },
    OPFSLogDownloadStore: { list: async () => [], isSupported: () => true },
    parseMcubootImage() {}, formatVersion() {}, parseLogStream() {}, decodeRecord() {},
    logToJSON() {}, logToCSV() {}, logToRawBin() {}, triggerDownload() {}
  };
  page = new Function(...Object.keys(imports), `${pageScript}
    return { fwReconnect, fwEnterTransfer, fwExitTransfer };`)(...Object.values(imports));
});

afterEach(() => {
  for (const args of windowListeners) window.removeEventListener(...args);
  vi.clearAllTimers();
  vi.useRealTimers();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  for (const name of ['plotToLeftAxis', 'plotToRightAxis', 'clearLeftAxis',
    'clearRightAxis', 'setPlotWindow', 'clearPlot', 'togglePlotDock']) delete globalThis[name];
});

describe('diagnostics head addressing', () => {
  it.each(['scanButton', 'usbConnectButton'])('keeps SOLO as the default for %s', async button => {
    expect($('headTargetSelect').value).toBe('4');
    $(button).click();
    await flush();
    expect(stacks[0].targetAddress).toBe(0x04);
    expect(didTargets).toEqual([0x04]);
    expect($('deviceInfo').textContent).toContain('Head: SOLO (0x04)');
  });

  it.each(['scanButton', 'usbConnectButton'])('applies OBOE before the first DID read for %s', async button => {
    $('headTargetSelect').value = '2';
    $(button).click();
    await flush();
    expect(stacks[0].options.targetAddress).toBe(0x02);
    expect(stacks[0].targetAddress).toBe(0x02);
    expect(didTargets).toEqual([0x02]);
    expect($('deviceInfo').textContent).toContain('Head: OBOE (0x02)');
    expect($('logMessages').textContent).toContain('Head: OBOE (0x02)');
    expect($('headTargetSelect').disabled).toBe(true);
  });

  it('keeps USB dialog/log client addresses independent of the head selection', async () => {
    $('headTargetSelect').value = '2';
    $('usbConnectButton').click();
    await flush();
    expect(stacks[0].options).toMatchObject({ sourceAddress: 0xFE, logAddress: 0xFF, targetAddress: 0x02 });
    await stacks[0].emit('handsetEmulation', { active: true });
    expect($('deviceInfo').textContent).toContain('Head: OBOE (0x02)');
  });

  it('locks both connect buttons and the selection while the BLE chooser is open', async () => {
    let finishScan;
    scan.mockImplementation(() => new Promise(resolve => { finishScan = resolve; }));
    $('scanButton').click();
    expect($('headTargetSelect').disabled).toBe(true);
    expect($('scanButton').disabled).toBe(true);
    expect($('usbConnectButton').disabled).toBe(true);
    $('usbConnectButton').click();
    $('scanButton').click();
    expect(scan).toHaveBeenCalledTimes(1);
    expect(stacks).toHaveLength(0);
    finishScan([]); // User cancels the device picker.
    await flush();
    expect($('headTargetSelect').disabled).toBe(false);
    expect($('scanButton').disabled).toBe(false);
    expect($('usbConnectButton').disabled).toBe(false);
  });

  it('releases the selection after USB cancellation/failure and retries the selected head', async () => {
    let failConnect;
    connect.mockImplementationOnce(() => new Promise((_, reject) => { failConnect = reject; }));
    $('headTargetSelect').value = '2';
    $('usbConnectButton').click();
    expect($('headTargetSelect').disabled).toBe(true);
    expect($('scanButton').disabled).toBe(true);
    failConnect(new Error('No port selected'));
    await flush();
    expect($('headTargetSelect').disabled).toBe(false);
    expect($('headTargetSelect').value).toBe('2');
    $('usbConnectButton').click();
    await flush();
    expect(stacks[1].targetAddress).toBe(0x02);
    expect(didTargets).toEqual([0x02]);
  });

  it.each(['scanButton', 'usbConnectButton'])('preserves OBOE during OTA disconnect/reconnect for %s', async button => {
    $('headTargetSelect').value = '2';
    $(button).click();
    await flush();
    const stack = stacks[0];
    await page.fwEnterTransfer();
    await stack.disconnect();
    expect($('headTargetSelect').disabled).toBe(true);
    expect($('scanButton').disabled).toBe(true);
    expect($('usbConnectButton').disabled).toBe(true);
    // Even an unexpected DOM change must not retarget an in-flight update.
    $('headTargetSelect').value = '4';
    await page.fwReconnect();
    expect(stacks).toHaveLength(1);
    expect(stack.targetAddress).toBe(0x02);
    expect(stack.setTargetAddress).toHaveBeenCalledExactlyOnceWith(0x02);
    expect(didTargets).toEqual([0x02]); // Recovery must not restart DID polling.
    expect($('deviceInfo').textContent).toContain('Head: OBOE (0x02)');
    page.fwExitTransfer();
    expect($('headTargetSelect').disabled).toBe(true);
  });

  it('unlocks after failed OTA recovery finishes, but not during it', async () => {
    $('usbConnectButton').click();
    await flush();
    await page.fwEnterTransfer();
    await stacks[0].disconnect();
    connect.mockRejectedValueOnce(new Error('Port unavailable'));
    await expect(page.fwReconnect()).rejects.toThrow('Port unavailable');
    expect($('headTargetSelect').disabled).toBe(true);
    page.fwExitTransfer();
    expect($('headTargetSelect').disabled).toBe(false);
  });

  it('allows choosing another head after disconnect', async () => {
    $('usbConnectButton').click();
    await flush();
    $('disconnectButton').click();
    await flush();
    expect($('headTargetSelect').disabled).toBe(false);
    $('headTargetSelect').value = '2';
    $('scanButton').click();
    await flush();
    expect(stacks[1].targetAddress).toBe(0x02);
    expect(didTargets).toEqual([0x04, 0x02]);
  });

  it('does not open a transport for an invalid destination', async () => {
    $('headTargetSelect').value = '255';
    $('usbConnectButton').click();
    await flush();
    expect(stacks).toHaveLength(0);
    expect(connect).not.toHaveBeenCalled();
    expect($('headTargetSelect').disabled).toBe(false);
  });
});
