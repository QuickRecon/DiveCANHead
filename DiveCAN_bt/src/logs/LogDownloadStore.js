/**
 * Disk-backed storage for long flash-log downloads.
 *
 * Chromium's Origin Private File System (OPFS) keeps partial downloads out of
 * JavaScript heap memory and makes a failed attempt available for inspection
 * or a later verified-prefix resume. The in-memory implementation mirrors the
 * same interface for tests and non-browser callers.
 */

const STORE_DIR = 'divecan-log-downloads';
const META_SUFFIX = '.meta.json';
const DATA_SUFFIX = '.bin';
export const OPFS_WRITE_BUFFER_BYTES = 64 * 1024;

function asBytes(value) {
  return value instanceof Uint8Array ? value : new Uint8Array(value);
}

function safeId(id) {
  if (!/^[a-zA-Z0-9._-]+$/.test(id)) throw new Error('Invalid log-download store id');
  return id;
}

function makeId() {
  const stamp = new Date().toISOString().replaceAll(/[:.]/g, '-');
  const random = globalThis.crypto?.randomUUID?.().slice(0, 8)
    ?? Math.random().toString(16).slice(2, 10);
  return `divecan-log-${stamp}-${random}`;
}

/** Test/non-browser implementation of the resumable store contract. */
export class MemoryLogDownloadStore {
  constructor(metadata = {}) {
    this.id = metadata.id || makeId();
    this._metadata = { id: this.id, complete: false, bytes: 0, ...metadata };
    this._bytes = new Uint8Array(0);
  }

  async beginAttempt() {}

  async append(value) {
    const bytes = asBytes(value);
    const grown = new Uint8Array(this._bytes.length + bytes.length);
    grown.set(this._bytes);
    grown.set(bytes, this._bytes.length);
    this._bytes = grown;
    this._metadata.bytes = grown.length;
  }

  async read(offset, length) {
    return this._bytes.slice(offset, offset + length);
  }

  async size() { return this._bytes.length; }

  async finishAttempt(patch = {}) {
    this._metadata = {
      ...this._metadata,
      ...patch,
      bytes: this._bytes.length,
      updatedAt: new Date().toISOString()
    };
  }

  async setMetadata(patch = {}) {
    this._metadata = { ...this._metadata, ...patch };
  }

  async getMetadata() { return { ...this._metadata }; }
  async getBytes() { return this._bytes.slice(); }
  async getFile() {
    const options = { type: 'application/octet-stream' };
    return typeof File === 'function'
      ? new File([this._bytes], `${this.id}${DATA_SUFFIX}`, options)
      : new Blob([this._bytes], options);
  }
}

/** OPFS implementation used by the diagnostics page. */
export class OPFSLogDownloadStore {
  constructor(directory, id) {
    this.directory = directory;
    this.id = safeId(id);
    this.dataHandle = null;
    this.metaHandle = null;
    this.writer = null;
    this.currentSize = 0;
    this.pendingChunks = [];
    this.pendingBytes = 0;
  }

  static isSupported() {
    return typeof navigator !== 'undefined' && Boolean(navigator.storage?.getDirectory);
  }

  static async _directory() {
    if (!this.isSupported()) throw new Error('Origin-private file storage is unavailable');
    const root = await navigator.storage.getDirectory();
    return root.getDirectoryHandle(STORE_DIR, { create: true });
  }

  static async create(metadata = {}) {
    const store = new OPFSLogDownloadStore(await this._directory(), metadata.id || makeId());
    await store._openHandles(true);
    await store.setMetadata({
      id: store.id,
      complete: false,
      bytes: 0,
      createdAt: new Date().toISOString(),
      ...metadata
    });
    return store;
  }

  static async open(id) {
    const store = new OPFSLogDownloadStore(await this._directory(), id);
    await store._openHandles(false);
    return store;
  }

  static async list() {
    if (!this.isSupported()) return [];
    const directory = await this._directory();
    const stores = [];
    for await (const [name, handle] of directory.entries()) {
      if (handle.kind !== 'file' || !name.endsWith(META_SUFFIX)) continue;
      const id = name.slice(0, -META_SUFFIX.length);
      try {
        const store = new OPFSLogDownloadStore(directory, id);
        await store._openHandles(false);
        const metadata = await store.getMetadata();
        stores.push({ store, metadata });
      } catch {
        // A missing/corrupt sidecar must not hide other recoverable downloads.
      }
    }
    stores.sort((a, b) => String(b.metadata.updatedAt || b.metadata.createdAt || '')
      .localeCompare(String(a.metadata.updatedAt || a.metadata.createdAt || '')));
    return stores;
  }

  async _openHandles(create) {
    this.dataHandle = await this.directory.getFileHandle(`${this.id}${DATA_SUFFIX}`, { create });
    this.metaHandle = await this.directory.getFileHandle(`${this.id}${META_SUFFIX}`, { create });
    this.currentSize = (await this.dataHandle.getFile()).size;
  }

  async beginAttempt() {
    if (this.writer) throw new Error('Log-download store attempt is already open');
    this.currentSize = (await this.dataHandle.getFile()).size;
    this.writer = await this.dataHandle.createWritable({ keepExistingData: true });
    await this.writer.seek(this.currentSize);
    this.pendingChunks = [];
    this.pendingBytes = 0;
  }

  async append(value) {
    if (!this.writer) throw new Error('Log-download store attempt is not open');
    const bytes = asBytes(value);
    // A full telemetry ring contains roughly 200,000 253-byte blocks. Do not
    // turn each one into a filesystem transaction; retain a bounded window and
    // flush it as one sequential write. Copy because response buffers are owned
    // by the transport and must not be retained by reference.
    this.pendingChunks.push(bytes.slice());
    this.pendingBytes += bytes.length;
    if (this.pendingBytes >= OPFS_WRITE_BUFFER_BYTES) await this._flushPending();
  }

  async _flushPending() {
    if (!this.writer || this.pendingBytes === 0) return;
    const output = new Uint8Array(this.pendingBytes);
    let offset = 0;
    for (const chunk of this.pendingChunks) {
      output.set(chunk, offset);
      offset += chunk.length;
    }
    await this.writer.write(output);
    this.currentSize += output.length;
    this.pendingChunks = [];
    this.pendingBytes = 0;
  }

  async read(offset, length) {
    const file = await this.dataHandle.getFile();
    return new Uint8Array(await file.slice(offset, offset + length).arrayBuffer());
  }

  async size() {
    return this.writer
      ? this.currentSize + this.pendingBytes
      : (await this.dataHandle.getFile()).size;
  }

  async finishAttempt(patch = {}) {
    if (this.writer) {
      await this._flushPending();
      const writer = this.writer;
      this.writer = null;
      await writer.close();
    }
    await this.setMetadata({
      ...patch,
      bytes: await this.size(),
      updatedAt: new Date().toISOString()
    });
  }

  async setMetadata(patch = {}) {
    let existing = {};
    try { existing = await this.getMetadata(); } catch { /* new/corrupt sidecar */ }
    const writer = await this.metaHandle.createWritable();
    await writer.write(JSON.stringify({ ...existing, ...patch, id: this.id }, null, 2));
    await writer.close();
  }

  async getMetadata() {
    const text = await (await this.metaHandle.getFile()).text();
    return text ? JSON.parse(text) : { id: this.id };
  }

  async getBytes() {
    return new Uint8Array(await (await this.dataHandle.getFile()).arrayBuffer());
  }

  async getFile() { return this.dataHandle.getFile(); }
}
