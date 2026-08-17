import { describe, it, expect } from 'vitest';
import {
  MemoryLogDownloadStore, OPFSLogDownloadStore, OPFS_WRITE_BUFFER_BYTES
} from './LogDownloadStore.js';

class FakeWritable {
  constructor(handle, keepExistingData) {
    this.handle = handle;
    this.bytes = keepExistingData ? handle.bytes.slice() : new Uint8Array(0);
    this.position = 0;
  }
  async seek(position) { this.position = position; }
  async write(value) {
    const bytes = typeof value === 'string' ? new TextEncoder().encode(value) : new Uint8Array(value);
    const required = this.position + bytes.length;
    if (required > this.bytes.length) {
      const grown = new Uint8Array(required);
      grown.set(this.bytes);
      this.bytes = grown;
    }
    this.bytes.set(bytes, this.position);
    this.position += bytes.length;
    this.handle.writeSizes.push(bytes.length);
  }
  async close() { this.handle.bytes = this.bytes; }
}

class FakeFileHandle {
  constructor() { this.bytes = new Uint8Array(0); this.writeSizes = []; }
  async getFile() {
    const bytes = this.bytes.slice();
    return {
      size: bytes.length,
      async text() { return new TextDecoder().decode(bytes); },
      slice(start, end) {
        const sliced = bytes.slice(start, end);
        return { async arrayBuffer() { return sliced.buffer; } };
      }
    };
  }
  async createWritable(options = {}) { return new FakeWritable(this, options.keepExistingData); }
}

class FakeDirectory {
  constructor() { this.files = new Map(); }
  async getFileHandle(name, options = {}) {
    if (!this.files.has(name)) {
      if (!options.create) throw new Error('not found');
      this.files.set(name, new FakeFileHandle());
    }
    return this.files.get(name);
  }
}

describe('MemoryLogDownloadStore', () => {
  it('appends, reads ranges, and preserves partial metadata', async () => {
    const store = new MemoryLogDownloadStore({ resumeKey: 'telemetry:all' });
    await store.beginAttempt();
    await store.append(new Uint8Array([1, 2, 3]));
    await store.append(new Uint8Array([4, 5]));
    await store.finishAttempt({ complete: false, lastError: { message: 'reset' } });

    expect(await store.size()).toBe(5);
    expect(Array.from(await store.read(1, 3))).toEqual([2, 3, 4]);
    expect(Array.from(await store.getBytes())).toEqual([1, 2, 3, 4, 5]);
    expect(await store.getMetadata()).toMatchObject({
      resumeKey: 'telemetry:all', complete: false, bytes: 5,
      lastError: { message: 'reset' }
    });
  });

  it('returns a binary Blob/File suitable for partial export', async () => {
    const store = new MemoryLogDownloadStore();
    await store.append(new Uint8Array([0x44, 0x4C, 0x43, 0x47]));
    const file = await store.getFile();
    expect(file.size).toBe(4);
    expect(file.type).toBe('application/octet-stream');
  });
});

describe('OPFSLogDownloadStore', () => {
  it('coalesces CAN blocks into bounded disk writes and flushes the partial on finish', async () => {
    const directory = new FakeDirectory();
    const store = new OPFSLogDownloadStore(directory, 'buffer-test');
    await store._openHandles(true);
    await store.setMetadata({ complete: false });
    await store.beginAttempt();

    const half = OPFS_WRITE_BUFFER_BYTES / 2;
    await store.append(new Uint8Array(half).fill(1));
    await store.append(new Uint8Array(half).fill(2));
    await store.append(new Uint8Array([3, 4, 5]));
    expect(await store.size()).toBe(OPFS_WRITE_BUFFER_BYTES + 3);

    await store.finishAttempt({ complete: false, lastError: { message: 'head reset' } });
    const dataHandle = directory.files.get('buffer-test.bin');
    expect(dataHandle.writeSizes).toEqual([OPFS_WRITE_BUFFER_BYTES, 3]);
    expect((await store.getFile()).size).toBe(OPFS_WRITE_BUFFER_BYTES + 3);
    expect(await store.getMetadata()).toMatchObject({
      complete: false, bytes: OPFS_WRITE_BUFFER_BYTES + 3,
      lastError: { message: 'head reset' }
    });
  });
});
