/**
 * MockTransport - Mock transport layer for testing UDSClient
 *
 * Provides controlled responses and tracks sent data for verification.
 */
export class MockTransport {
  constructor() {
    this.events = {};
    this.sentData = [];
    this.responseQueue = [];
    this.responder = null;
    this.sourceAddress = 0xFF;
    this.targetAddress = 0x80;
  }

  /**
   * Install a scripted responder for multi-step sequences (OTA, log download).
   * The function receives the sent request (Uint8Array) and its 0-based index,
   * and returns the response bytes to emit (or null/undefined to stay silent,
   * e.g. to simulate a lost reply). Takes precedence over the response queue.
   * @param {(request: Uint8Array, index: number) => (Uint8Array|Array|null)} fn
   */
  setResponder(fn) {
    this.responder = fn;
  }

  /**
   * Register event handler
   */
  on(event, callback) {
    if (!this.events[event]) this.events[event] = [];
    this.events[event].push(callback);
    return this;
  }

  /**
   * Remove event handler
   */
  off(event, callback) {
    if (!this.events[event]) return this;
    this.events[event] = this.events[event].filter(cb => cb !== callback);
    return this;
  }

  /**
   * Emit event
   */
  emit(event, ...args) {
    if (!this.events[event]) return;
    this.events[event].forEach(callback => callback(...args));
  }

  /**
   * Remove all listeners
   */
  removeAllListeners(event) {
    if (event) delete this.events[event];
    else this.events = {};
    return this;
  }

  /**
   * Send data (stores for verification and optionally triggers queued response)
   */
  async send(data) {
    const dataArray = data instanceof Uint8Array ? data : new Uint8Array(data);
    const index = this.sentData.length;
    this.sentData.push(dataArray);

    // A scripted responder takes precedence and can vary by request/index.
    if (this.responder) {
      const response = this.responder(dataArray, index);
      if (response !== null && response !== undefined) {
        const respArray = response instanceof Uint8Array ? response : new Uint8Array(response);
        setTimeout(() => this.emit('message', respArray), 0);
      }
      return;
    }

    // Auto-respond if responses are queued
    if (this.responseQueue.length > 0) {
      const response = this.responseQueue.shift();
      // Defer to next tick to simulate async behavior
      setTimeout(() => this.emit('message', response), 0);
    }
  }

  /**
   * Queue an auto-response for the next send()
   * @param {Uint8Array|Array} data - Response data
   */
  queueResponse(data) {
    const dataArray = data instanceof Uint8Array ? data : new Uint8Array(data);
    this.responseQueue.push(dataArray);
  }

  /**
   * Get last sent data
   * @returns {Uint8Array|null}
   */
  getLastSent() {
    return this.sentData.length > 0 ? this.sentData[this.sentData.length - 1] : null;
  }

  /**
   * Get all sent data
   * @returns {Uint8Array[]}
   */
  getAllSent() {
    return [...this.sentData];
  }

  /**
   * Simulate receiving a message (inject response)
   * @param {Uint8Array|Array} data - Response data
   */
  injectMessage(data) {
    const dataArray = data instanceof Uint8Array ? data : new Uint8Array(data);
    this.emit('message', dataArray);
  }

  /**
   * Simulate transport error
   * @param {Error} error - Error to emit
   */
  injectError(error) {
    this.emit('error', error);
  }

  /**
   * Clear sent data history
   */
  clearSentData() {
    this.sentData = [];
  }

  /**
   * Clear response queue
   */
  clearResponseQueue() {
    this.responseQueue = [];
  }

  /**
   * Reset all state
   */
  reset() {
    this.sentData = [];
    this.responseQueue = [];
    this.responder = null;
    this.events = {};
  }

  // DirectTransport interface compatibility
  get state() {
    return 'IDLE';
  }

  get isIdle() {
    return true;
  }

  processFrame(payload) {
    this.emit('message', payload instanceof Uint8Array ? payload : new Uint8Array(payload));
  }
}
