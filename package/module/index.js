// XER Native Node.js Bindings
// This is a JavaScript wrapper around the native C++ bindings

const path = require('path');
const native = require(path.join(__dirname, 'build', 'Release', 'xer_native.node'));

// Helper module
const helper = {
  buffer: {
    fromString: native.helper.bufferFromString,
    toString: native.helper.bufferToString,
    hexEncode: native.helper.hexEncode,
    hexDecode: native.helper.hexDecode
  }
};

// Cache module
const cache = {
  instance: native.cache.instance,
  writePersistentText: native.cache.writePersistentText,
  readPersistentText: native.cache.readPersistentText
};

// Audio module
const audio = {
  Engine: class {
    constructor() {
      this._engine = native.audio.createEngine();
    }

    initialize(sampleRate, bufferSize, channelCount) {
      return native.audio.engineInitialize(this._engine, sampleRate, bufferSize, channelCount);
    }

    start() {
      return native.audio.engineStart(this._engine);
    }

    stop() {
      return native.audio.engineStop(this._engine);
    }

    get sampleRate() {
      return native.audio.engineGetSampleRate(this._engine);
    }

    get bufferSize() {
      return native.audio.engineGetBufferSize(this._engine);
    }

    get channelCount() {
      return native.audio.engineGetChannelCount(this._engine);
    }

    get cpuLoad() {
      return native.audio.engineGetCPULoad(this._engine);
    }
  }
};

// Async IO module
const asyncIO = {
  // FileReader class
  FileReader: class {
    constructor() {
      this._reader = native.asyncIO.fileReader.create();
    }

    openFile(filepath) {
      return native.asyncIO.fileReader.openFile(this._reader, filepath);
    }

    closeFile() {
      return native.asyncIO.fileReader.closeFile(this._reader);
    }

    isFileOpen() {
      return native.asyncIO.fileReader.isFileOpen(this._reader);
    }

    readAsync(bytesToRead, callback) {
      return native.asyncIO.fileReader.readAsync(this._reader, bytesToRead, callback);
    }

    readSync(bytesToRead) {
      return native.asyncIO.fileReader.readSync(this._reader, bytesToRead);
    }

    readAsyncAt(offset, bytesToRead, callback) {
      return native.asyncIO.fileReader.readAsyncAt(this._reader, offset, bytesToRead, callback);
    }

    seek(offset) {
      return native.asyncIO.fileReader.seek(this._reader, offset);
    }

    get position() {
      return native.asyncIO.fileReader.getPosition(this._reader);
    }

    get fileSize() {
      return native.asyncIO.fileReader.getFileSize(this._reader);
    }

    isReading() {
      return native.asyncIO.fileReader.isReading(this._reader);
    }

    get pendingOperations() {
      return native.asyncIO.fileReader.getPendingOperations(this._reader);
    }

    get lastError() {
      return native.asyncIO.fileReader.getLastError(this._reader);
    }
  },

  // FileWriter class
  FileWriter: class {
    constructor() {
      this._writer = native.asyncIO.fileWriter.create();
    }

    createFile(filepath) {
      return native.asyncIO.fileWriter.createFile(this._writer, filepath);
    }

    closeFile() {
      return native.asyncIO.fileWriter.closeFile(this._writer);
    }

    writeAsync(data, callback) {
      return native.asyncIO.fileWriter.writeAsync(this._writer, data, callback);
    }

    writeSync(data) {
      return native.asyncIO.fileWriter.writeSync(this._writer, data);
    }

    flushBuffer() {
      return native.asyncIO.fileWriter.flushBuffer(this._writer);
    }

    get bufferedBytes() {
      return native.asyncIO.fileWriter.getBufferedBytes(this._writer);
    }

    isWriting() {
      return native.asyncIO.fileWriter.isWriting(this._writer);
    }

    get pendingOperations() {
      return native.asyncIO.fileWriter.getPendingOperations(this._writer);
    }

    get bytesWritten() {
      return native.asyncIO.fileWriter.getBytesWritten(this._writer);
    }

    get lastError() {
      return native.asyncIO.fileWriter.getLastError(this._writer);
    }
  },

  // BufferPool class
  BufferPool: class {
    constructor() {
      this._pool = native.asyncIO.bufferPool.create();
    }

    acquireBuffer() {
      return native.asyncIO.bufferPool.acquireBuffer(this._pool);
    }

    releaseBuffer(buffer) {
      return native.asyncIO.bufferPool.releaseBuffer(this._pool, buffer);
    }

    get blockSize() {
      return native.asyncIO.bufferPool.getBlockSize(this._pool);
    }

    get availableBlocks() {
      return native.asyncIO.bufferPool.getAvailableBlocks(this._pool);
    }

    get totalBlocks() {
      return native.asyncIO.bufferPool.getTotalBlocks(this._pool);
    }

    get usedBlocks() {
      return native.asyncIO.bufferPool.getUsedBlocks(this._pool);
    }

    resize(size) {
      return native.asyncIO.bufferPool.resize(this._pool, size);
    }

    reset() {
      return native.asyncIO.bufferPool.reset(this._pool);
    }

    trim() {
      return native.asyncIO.bufferPool.trim(this._pool);
    }
  },

  // TaskQueue class
  TaskQueue: class {
    constructor() {
      this._queue = native.asyncIO.taskQueue.create();
    }

    enqueueTask(task, priority) {
      return native.asyncIO.taskQueue.enqueueTask(this._queue, task, priority);
    }

    tryEnqueueTask(task, priority) {
      return native.asyncIO.taskQueue.tryEnqueueTask(this._queue, task, priority);
    }

    dequeueTask() {
      return native.asyncIO.taskQueue.dequeueTask(this._queue);
    }

    waitAndDequeue(timeout) {
      return native.asyncIO.taskQueue.waitAndDequeue(this._queue, timeout);
    }

    get queueSize() {
      return native.asyncIO.taskQueue.getQueueSize(this._queue);
    }

    isEmpty() {
      return native.asyncIO.taskQueue.isEmpty(this._queue);
    }

    clear() {
      return native.asyncIO.taskQueue.clear(this._queue);
    }

    shutdown() {
      return native.asyncIO.taskQueue.shutdown(this._queue);
    }

    isRunning() {
      return native.asyncIO.taskQueue.isRunning(this._queue);
    }

    cancelTask(taskId) {
      return native.asyncIO.taskQueue.cancelTask(this._queue, taskId);
    }

    get cancelledCount() {
      return native.asyncIO.taskQueue.getCancelledCount(this._queue);
    }

    pruneCancelled() {
      return native.asyncIO.taskQueue.pruneCancelled(this._queue);
    }

    get totalTasksProcessed() {
      return native.asyncIO.taskQueue.getTotalTasksProcessed(this._queue);
    }

    get maxQueueSize() {
      return native.asyncIO.taskQueue.getMaxQueueSize(this._queue);
    }
  },

  // ThreadPool (singleton)
  ThreadPool: {
    getSharedInstance() {
      return native.asyncIO.threadPool.getSharedInstance();
    },

    start(instance, threadCount) {
      return native.asyncIO.threadPool.start(instance, threadCount);
    },

    stop(instance) {
      return native.asyncIO.threadPool.stop(instance);
    },

    enqueue(instance, task) {
      return native.asyncIO.threadPool.enqueue(instance, task);
    },

    tryEnqueue(instance, task) {
      return native.asyncIO.threadPool.tryEnqueue(instance, task);
    },

    waitAll(instance) {
      return native.asyncIO.threadPool.waitAll(instance);
    },

    getThreadCount(instance) {
      return native.asyncIO.threadPool.getThreadCount(instance);
    },

    getPendingTaskCount(instance) {
      return native.asyncIO.threadPool.getPendingTaskCount(instance);
    },

    isRunning(instance) {
      return native.asyncIO.threadPool.isRunning(instance);
    }
  },

  // IOBufferPool (singleton)
  IOBufferPool: {
    instance() {
      return native.asyncIO.ioBufferPool.instance();
    },

    acquire(instance) {
      return native.asyncIO.ioBufferPool.acquire(instance);
    },

    release(instance, buffer) {
      return native.asyncIO.ioBufferPool.release(instance, buffer);
    },

    getMinBucketSize(instance) {
      return native.asyncIO.ioBufferPool.getMinBucketSize(instance);
    },

    getMaxBucketSize(instance) {
      return native.asyncIO.ioBufferPool.getMaxBucketSize(instance);
    },

    clear(instance) {
      return native.asyncIO.ioBufferPool.clear(instance);
    }
  },

  // CacheManager class
  CacheManager: class {
    constructor() {
      this._manager = native.asyncIO.cacheManager.create();
    }

    put(key, value) {
      return native.asyncIO.cacheManager.put(this._manager, key, value);
    }

    get(key) {
      return native.asyncIO.cacheManager.get(this._manager, key);
    }

    clear() {
      return native.asyncIO.cacheManager.clear(this._manager);
    }

    removeByPrefix(prefix) {
      return native.asyncIO.cacheManager.removeByPrefix(this._manager, prefix);
    }

    get entryCount() {
      return native.asyncIO.cacheManager.getEntryCount(this._manager);
    }

    get sizeBytes() {
      return native.asyncIO.cacheManager.getSizeBytes(this._manager);
    }
  },

  // FileMap class
  FileMap: class {
    constructor() {
      this._map = native.asyncIO.fileMap.create();
    }

    mapRegion(filepath, offset, length) {
      return native.asyncIO.fileMap.mapRegion(this._map, filepath, offset, length);
    }

    getRegion(filepath, offset) {
      return native.asyncIO.fileMap.getRegion(this._map, filepath, offset);
    }

    unmap(filepath) {
      return native.asyncIO.fileMap.unmap(this._map, filepath);
    }

    getMappedRegionCount(filepath) {
      return native.asyncIO.fileMap.getMappedRegionCount(this._map, filepath);
    }

    get totalMappedRegionCount() {
      return native.asyncIO.fileMap.getTotalMappedRegionCount(this._map);
    }
  },

  // PrefetchManager class
  PrefetchManager: class {
    constructor() {
      this._manager = native.asyncIO.prefetchManager.create();
    }

    prefetch(filepath, offset, length) {
      return native.asyncIO.prefetchManager.prefetch(this._manager, filepath, offset, length);
    }

    prefetchAsync(filepath, offset, length, callback) {
      return native.asyncIO.prefetchManager.prefetchAsync(this._manager, filepath, offset, length, callback);
    }

    tryGet(filepath, offset, length) {
      return native.asyncIO.prefetchManager.tryGet(this._manager, filepath, offset, length);
    }

    invalidatePath(filepath) {
      return native.asyncIO.prefetchManager.invalidatePath(this._manager, filepath);
    }

    isPrefetchInProgress(filepath) {
      return native.asyncIO.prefetchManager.isPrefetchInProgress(this._manager, filepath);
    }

    get inProgressCount() {
      return native.asyncIO.prefetchManager.getInProgressCount(this._manager);
    }

    get cachedEntryCount() {
      return native.asyncIO.prefetchManager.getCachedEntryCount(this._manager);
    }

    get cacheSizeBytes() {
      return native.asyncIO.prefetchManager.getCacheSizeBytes(this._manager);
    }

    get mappedRegionCount() {
      return native.asyncIO.prefetchManager.getMappedRegionCount(this._manager);
    }
  },

  // PriorityQueue class
  PriorityQueue: class {
    constructor() {
      this._queue = native.asyncIO.priorityQueue.create();
    }

    push(item, priority) {
      return native.asyncIO.priorityQueue.push(this._queue, item, priority);
    }

    tryPop() {
      return native.asyncIO.priorityQueue.tryPop(this._queue);
    }

    waitPop() {
      return native.asyncIO.priorityQueue.waitPop(this._queue);
    }

    empty() {
      return native.asyncIO.priorityQueue.empty(this._queue);
    }
  },

  // StreamReader class
  StreamReader: class {
    constructor() {
      this._reader = native.asyncIO.streamReader.create();
    }

    open(filepath) {
      return native.asyncIO.streamReader.open(this._reader, filepath);
    }

    close() {
      return native.asyncIO.streamReader.close(this._reader);
    }

    readAsync(bytesToRead, callback) {
      return native.asyncIO.streamReader.readAsync(this._reader, bytesToRead, callback);
    }
  },

  // StreamWriter class
  StreamWriter: class {
    constructor() {
      this._writer = native.asyncIO.streamWriter.create();
    }

    open(filepath) {
      return native.asyncIO.streamWriter.open(this._writer, filepath);
    }

    close() {
      return native.asyncIO.streamWriter.close(this._writer);
    }

    writeAsync(data, callback) {
      return native.asyncIO.streamWriter.writeAsync(this._writer, data, callback);
    }
  }
};

// Flux module
const flux = {
  Core: class {
    constructor() {
      this._core = native.flux.createCore();
    }

    initialize() {
      return native.flux.coreInitialize(this._core);
    }

    shutdown() {
      return native.flux.coreShutdown(this._core);
    }

    get isRunning() {
      return native.flux.coreIsRunning(this._core);
    }
  },

  terminal: {
    defaultSize: native.flux.terminalDefaultSize,
    detectSize: native.flux.terminalDetectSize
  }
};

module.exports = {
  helper,
  cache,
  audio,
  asyncIO,
  flux
};
