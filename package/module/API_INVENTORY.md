# XER API Inventory for Node.js Bindings

This document provides a comprehensive inventory of all public APIs across the XER subsystems that need to be wrapped for Node.js integration.

## Executive Summary

**Total Statistics:**
- **Total Classes:** 82
- **Total Structs:** 35
- **Total Free Functions:** 88
- **Total Enums:** 18
- **Total Public Methods:** ~450+
- **Total API Surface:** ~583 items

---

## 1. Helper Subsystem (`src/helper`)

### 1.1 Buffer Utilities (Class: `Buffer`)

**Static Methods:**
- `FromString(string)` - String to bytes
- `ToString(bytes)` - Bytes to string
- `NormalizeUtf8(string)` - Normalize UTF8
- `AppendString(buffer, string)` - Append string
- `AppendBytes(buffer, bytes)` - Append bytes
- `HexEncode(bytes)` - Hex encode
- `HexDecode(hex)` - Hex decode
- `Copy(bytes)` - Copy bytes
- `Concat(bytes1, bytes2)` - Concat bytes
- `Compare(bytes1, bytes2)` - Compare bytes
- `Equals(bytes1, bytes2)` - Check equality
- `IndexOf(bytes, byte)` - Find index
- `LastIndexOf(bytes, byte)` - Find last index
- `Fill(bytes, value)` - Fill bytes
- `Slice(bytes, start, end)` - Slice bytes
- `Swap16(bytes)` - Swap 16-bit
- `Swap32(bytes)` - Swap 32-bit
- `Swap64(bytes)` - Swap 64-bit

### 1.2 Dict Operations (Class: `Dict`)

**Methods:**
- `GetBool(key)` - Get bool value
- `GetInt(key)` - Get int value
- `GetDouble(key)` - Get double value
- `GetString(key)` - Get string value
- `GetStringArray(key)` - Get string array
- `SetBool(key, value)` - Set bool value
- `SetInt(key, value)` - Set int value
- `SetDouble(key, value)` - Set double value
- `SetString(key, value)` - Set string value
- `SetStringArray(key, value)` - Set string array
- `GetOrCreateDict(key)` - Get or create nested dict
- `GetDict(key)` - Get nested dict
- `HasDict(key)` - Check if has dict
- `HasKey(key)` - Check if has key
- `Remove(key)` - Remove key
- `Clear()` - Clear dict
- `Size()` - Get size
- `Keys()` - Get keys
- `ToJson()` - Convert to JSON
- `FromJson(json)` - Load from JSON
- `ToJsonString()` - Convert to JSON string
- `FromJsonString(string)` - Load from JSON string
- `SaveToFile(path)` - Save to file
- `LoadFromFile(path)` - Load from file
- `SaveToCache(scope, key, name)` - Save to cache
- `LoadFromCache(scope, key, name)` - Load from cache
- `AsyncSaveToFile(path, callback)` - Async save to file
- `AsyncLoadFromFile(path, callback)` - Async load from file
- `Merge(other)` - Merge dict
- `MergeJson(json)` - Merge JSON
- `FindKeysMatching(pattern)` - Find keys matching pattern
- `GetNestedPath(path)` - Get nested path

**Static Methods:**
- `Create()` - Create dict
- `FromJsonFile(path)` - Create from JSON file

### 1.3 JSON Helper (Class: `JsonCppHelper`)

**Static Methods:**
- `Parse(string)` - Parse JSON string
- `ParseFile(path)` - Parse JSON file
- `WriteToFile(obj, path)` - Write to file
- `ToJsonString(obj)` - Convert to JSON string
- `GetTopLevelKeys(obj)` - Get top-level keys
- `GetNestedValues(obj, path)` - Get nested values
- `SetNestedValue(obj, path, value)` - Set nested value
- `GetBool(obj, key)` - Get bool
- `GetInt(obj, key)` - Get int
- `GetDouble(obj, key)` - Get double
- `GetString(obj, key)` - Get string
- `ContainsKey(obj, key)` - Check if contains key
- `IsObject(obj)` - Check if object
- `IsArray(obj)` - Check if array
- `IsString(obj)` - Check if string
- `IsNumber(obj)` - Check if number

### 1.4 Compression (Namespace: `Engine::Common::CompressionCodec`)

**Static Methods:**
- `Compress(text)` - Compress text
- `DecompressToString(data)` - Decompress to string

### 1.5 File Operations (Class: `ToolTo`)

**Static Methods:**
- `CopyBinary(src, dst)` - Copy binary
- `ReadTextFile(path)` - Read text file
- `WriteTextFile(path, content)` - Write text file

### 1.6 Error Reporting (Class: `StackError`)

**Static Methods:**
- `BuildV8Report(tryCatch)` - Build V8 error report

### 1.7 String Utilities (Namespace: `Helper::String`)

**Free Functions:**
- `NormalizeUtf8(string)` - Normalize UTF8
- `IsBlank(string)` - Check if blank
- `TrimAsciiWhitespaceCopy(string)` - Trim whitespace
- `CanonicalizeToken(string)` - Canonicalize token
- `DefaultString(value, default)` - Default string
- `BuildKeyValueLine(key, value)` - Build key-value line

### 1.8 V8 Integration Helpers

**Async Bridge:**
- `PostAsync<R>(isolate, promise, callback)` - Post async operation
- `PostAsyncVoid(isolate, promise, callback)` - Post async void operation
- `DispatchInterrupt(isolate)` - Dispatch interrupt

**Class Builder:**
- `MakeClass<T>(className, constructor)` - Make V8 class template
- `ExportClass<T>(context, className, classTemplate)` - Export class to context
- `WrapPointer<T>(obj, ptr)` - Wrap C++ pointer
- `UnwrapPointer<T>(obj)` - Unwrap C++ pointer
- `RegisterWeakCleanup<T>(obj, ptr)` - Register weak cleanup
- `ThrowTypeError(message)` - Throw type error
- `ThrowRangeError(message)` - Throw range error
- `ThrowError(message)` - Throw error
- `ToV8Str(string)` - Convert to V8 string
- `FromV8Str(v8str)` - Convert from V8 string

**Module Builder:**
- `SetMethod(isolate, context, object, name, callback)` - Set method on object
- `ExportGlobalModule(isolate, context, name, module)` - Export global module

---

## 2. Async IO Subsystem (`src/async_io`)

### 2.1 Core Async IO (Namespace: `AsyncIO::IO::Async`)

#### AsyncBufferPool

**Methods:**
- `AcquireBuffer()` - Acquire a buffer from pool
- `ReleaseBuffer(buffer)` - Release buffer back to pool
- `GetBlockSize()` - Get block size
- `GetAvailableBlocks()` - Get available block count
- `GetTotalBlocks()` - Get total block count
- `GetUsedBlocks()` - Get used block count
- `Resize(size)` - Resize pool
- `Reset()` - Reset pool
- `Trim()` - Trim unused blocks

#### AsyncFileReader

**Methods:**
- `OpenFile(filepath)` - Open file for reading
- `IsFileOpen()` - Check if file is open
- `CloseFile()` - Close file
- `ReadAsync(bytesToRead, callback)` - Async read at current position
- `ReadSync(bytesToRead, outputBuffer)` - Sync read
- `ReadAsyncAt(offset, bytesToRead, callback)` - Async read at offset
- `Seek(offset)` - Seek to position
- `GetPosition()` - Get current position
- `GetFileSize()` - Get file size
- `IsReading()` - Check if reading
- `GetPendingOperations()` - Get pending operation count
- `GetLastError()` - Get last error message

#### AsyncFileWriter

**Methods:**
- `CreateFile(filepath)` - Create file for writing
- `IsFileOpen()` - Check if file is open
- `CloseFile()` - Close file
- `WriteAsync(data, callback)` - Async write
- `WriteSync(data)` - Sync write
- `FlushBuffer()` - Flush buffer
- `GetBufferedBytes()` - Get buffered byte count
- `IsWriting()` - Check if writing
- `GetPendingOperations()` - Get pending operation count
- `GetBytesWritten()` - Get total bytes written
- `GetLastError()` - Get last error message

#### AsyncTaskQueue

**Methods:**
- `EnqueueTask(task, priority)` - Enqueue task with priority
- `TryEnqueueTask(task, priority)` - Try to enqueue task
- `DequeueTask()` - Dequeue task
- `WaitAndDequeue(timeout)` - Blocking dequeue with timeout
- `GetQueueSize()` - Get queue size
- `IsEmpty()` - Check if empty
- `Clear()` - Clear queue
- `Shutdown()` - Shutdown queue
- `IsRunning()` - Check if running
- `CancelTask(taskId)` - Cancel task by ID
- `GetCancelledCount()` - Get cancelled count
- `PruneCancelled()` - Prune cancelled tasks
- `GetTotalTasksProcessed()` - Get total processed count
- `GetMaxQueueSize()` - Get max queue size

#### IOBufferPool

**Static Methods:**
- `Instance()` - Get singleton instance

**Methods:**
- `Acquire()` - Acquire buffer
- `Release(buffer)` - Release buffer
- `GetMinBucketSize()` - Get min bucket size
- `GetMaxBucketSize()` - Get max bucket size
- `Clear()` - Clear pool

#### IOCacheManager

**Methods:**
- `Put(key, value)` - Put entry in cache
- `Get(key)` - Get entry from cache
- `Clear()` - Clear cache
- `RemoveByPrefix(prefix)` - Remove entries by prefix
- `GetEntryCount()` - Get entry count
- `GetSizeBytes()` - Get size in bytes

#### IOFileMap

**Methods:**
- `MapRegion(filepath, offset, length)` - Map file region
- `GetRegion(filepath, offset)` - Get mapped region
- `Unmap(filepath)` - Unmap file
- `GetMappedRegionCount(filepath)` - Get mapped region count for path
- `GetTotalMappedRegionCount()` - Get total mapped region count

#### IOPrefetchManager

**Methods:**
- `Prefetch(filepath, offset, length)` - Prefetch region
- `PrefetchAsync(filepath, offset, length, callback)` - Async prefetch
- `TryGet(filepath, offset, length)` - Try to get cached data
- `InvalidatePath(filepath)` - Invalidate path
- `IsPrefetchInProgress(filepath)` - Check if prefetch in progress
- `GetInProgressCount()` - Get in-progress count
- `GetCachedEntryCount()` - Get cached entry count
- `GetCacheSizeBytes()` - Get cache size
- `GetMappedRegionCount()` - Get mapped region count

#### IOPriorityQueue

**Methods:**
- `Push(item, priority)` - Push item
- `TryPop()` - Try to pop item
- `WaitPop()` - Blocking pop
- `Empty()` - Check if empty

#### IOStreamReader

**Methods:**
- `Open(filepath)` - Open stream
- `Close()` - Close stream
- `ReadAsync(bytesToRead, callback)` - Async read

#### IOStreamWriter

**Methods:**
- `Open(filepath)` - Open stream
- `Close()` - Close stream
- `WriteAsync(data, callback)` - Async write

#### IOThreadPool

**Static Methods:**
- `GetSharedInstance()` - Get shared instance

**Methods:**
- `Start(threadCount)` - Start thread pool
- `Stop()` - Stop thread pool
- `Enqueue(task)` - Enqueue task
- `TryEnqueue(task)` - Try to enqueue with backpressure
- `WaitAll()` - Wait for all tasks
- `GetThreadCount()` - Get thread count
- `GetPendingTaskCount()` - Get pending task count
- `IsRunning()` - Check if running

### 2.2 Hardware Abstraction (Namespace: `AsyncIO::IO::Hardware`)

#### BatteryInfoProvider

**Methods:**
- `Initialize()` - Initialize provider
- `IsInitialized()` - Check initialization
- `GetBatteries()` - Get battery info
- `IsOnACPower()` - Check AC power status

#### CPUInfoProvider

**Methods:**
- `Initialize()` - Initialize provider
- `IsInitialized()` - Check initialization
- `GetInfo()` - Get CPU info
- `GetCores()` - Get core info
- `GetTotalMemoryMB()` - Get total memory
- `GetAvailableMemoryMB()` - Get available memory
- `SetThreadAffinity(threadId, coreMask)` - Set thread affinity
- `GetCurrentCore()` - Get current core
- `GetCPUUsagePercent()` - Get CPU usage
- `GetCoreUsagePercent(coreId)` - Get core usage
- `RefreshFrequencies()` - Refresh frequencies
- `GetCPUDescription()` - Get CPU description

#### DiskInfoProvider

**Methods:**
- `Initialize()` - Initialize provider
- `IsInitialized()` - Check initialization
- `GetDisks()` - Get disk info
- `GetIoStats(device)` - Get I/O stats

#### GPUInfoProvider

**Methods:**
- `Initialize()` - Initialize provider
- `IsInitialized()` - Check initialization
- `GetDeviceCount()` - Get device count
- `GetDevices()` - Get device list
- `GetDeviceName(deviceId)` - Get device name
- `GetTotalMemory(deviceId)` - Get total memory
- `GetFreeMemory(deviceId)` - Get free memory
- `SetCurrentDevice(deviceId)` - Set current device
- `GetCurrentDevice()` - Get current device
- `SupportsCUDA(deviceId)` - Check CUDA support
- `SupportsOpenCL(deviceId)` - Check OpenCL support
- `SupportsHIP(deviceId)` - Check HIP support
- `SupportsVulkan(deviceId)` - Check Vulkan support
- `GetDeviceDescription(deviceId)` - Get device description

#### HwCapabilityScanner

**Static Methods:**
- `Instance()` - Get singleton

**Methods:**
- `Scan()` - Scan capabilities

#### DirectIOManager

**Methods:**
- `Initialize()` - Initialize manager
- `IsInitialized()` - Check initialization
- `AllocateBuffer(size)` - Allocate buffer
- `ReleaseBuffer(buffer)` - Release buffer
- `OpenFileForDIO(filepath)` - Open file for direct I/O
- `ReadDirect(buffer, offset, size)` - Direct read
- `WriteDirect(buffer, offset, size)` - Direct write
- `CloseFile(filepath)` - Close file
- `GetOptimalAlignment()` - Get optimal alignment
- `GetMinimumAlignment()` - Get minimum alignment
- `VerifyBufferAlignment(buffer)` - Verify alignment
- `GetAverageIOLatencyMS()` - Get average latency
- `GetTotalBytesTransferred()` - Get total bytes transferred

#### HwPowerMonitor

**Methods:**
- `Start()` - Start monitoring
- `Stop()` - Stop monitoring
- `GetStats()` - Get power stats
- `IsRunning()` - Check if running

#### TemperatureSensor

**Methods:**
- `Initialize()` - Initialize sensor
- `IsInitialized()` - Check initialization
- `GetSensorCount()` - Get sensor count
- `GetAllReadings()` - Get all readings
- `GetReading(sensorName)` - Get specific reading
- `GetMaxTemperature()` - Get max temperature
- `GetAverageTemperature()` - Get average temperature
- `GetCPUTemperature()` - Get CPU temperature
- `GetCPUCriticalTemperature()` - Get CPU critical temperature
- `GetGPUTemperature()` - Get GPU temperature
- `GetGPUCriticalTemperature()` - Get GPU critical temperature
- `StartMonitoring()` - Start monitoring
- `StopMonitoring()` - Stop monitoring
- `IsMonitoring()` - Check if monitoring
- `Refresh()` - Force refresh
- `SetTemperatureAlert(threshold)` - Set alert threshold
- `NeedsAlert()` - Check if alert needed
- `SetAlertCallback(callback)` - Set alert callback

#### HardwareTopologyMap

**Methods:**
- `Initialize()` - Initialize topology
- `IsInitialized()` - Check initialization
- `GetTopology(level)` - Get topology by level
- `GetObject(objectId)` - Get object by ID
- `GetCoreCount()` - Get core count
- `GetThreadCount()` - Get thread count
- `GetSocketCount()` - Get socket count
- `GetNUMANodeCount()` - Get NUMA node count
- `GetParent(objectId)` - Get parent object
- `GetChildren(objectId)` - Get children
- `GetCacheHierarchy(objectId)` - Get cache hierarchy
- `GetCacheSize(objectId)` - Get cache size
- `GetNUMANode(processorId)` - Get NUMA node for processor
- `GetProcsOnNUMA(nodeId)` - Get processors on NUMA
- `GetTopologyString()` - Get topology string

#### MemoryMonitor

**Methods:**
- `StartMonitoring()` - Start monitoring
- `StopMonitoring()` - Stop monitoring
- `IsMonitoring()` - Check if monitoring
- `GetMemoryStats()` - Get memory stats
- `GetGPUMemoryStats()` - Get GPU memory stats
- `GetPeakMemoryUsage()` - Get peak memory
- `ResetPeakMemory()` - Reset peak memory
- `SetLowMemoryThreshold(threshold)` - Set low memory threshold
- `IsLowMemory()` - Check if low memory
- `RecordAllocation(size, tag)` - Record allocation
- `RecordDeallocation(size, tag)` - Record deallocation
- `GetTrackedAllocationMB()` - Get tracked allocation

#### NetworkInfoProvider

**Methods:**
- `Initialize()` - Initialize provider
- `IsInitialized()` - Check initialization
- `GetAdapters()` - Get network adapters
- `GetTotalBytesReceived()` - Get total bytes received
- `GetTotalBytesSent()` - Get total bytes sent

#### SystemInfoProvider

**Methods:**
- `Initialize()` - Initialize all providers
- `IsInitialized()` - Check initialization
- `Refresh()` - Refresh snapshot
- `RefreshAsync(callback)` - Async refresh
- `GetLastSnapshot()` - Get last snapshot
- `ToJson()` - Serialize to JSON
- `Cpu()` - Get CPU provider
- `Gpu()` - Get GPU provider
- `Memory()` - Get memory monitor
- `Temp()` - Get temperature sensor
- `Power()` - Get power monitor
- `Topology()` - Get topology map
- `Disks()` - Get disk provider
- `Network()` - Get network provider
- `Battery()` - Get battery provider

### 2.3 Synchronization Primitives (Namespace: `AsyncIO::IO::Sync`)

#### Barrier

**Methods:**
- `Wait()` - Wait at barrier
- `Reset()` - Reset barrier
- `GetName()` - Get name
- `GetThreadCount()` - Get thread count
- `GetWaitingCount()` - Get waiting count

#### ConditionVariableWrapper

**Methods:**
- `Notify()` - Notify one
- `NotifyAll()` - Notify all
- `Wait(mutex)` - Wait
- `WaitFor(mutex, timeout)` - Wait with timeout
- `GetName()` - Get name
- `GetNotifyCount()` - Get notify count
- `GetWaitCount()` - Get wait count

#### MutexWrapper

**Methods:**
- `Lock()` - Lock mutex
- `TryLock()` - Try to lock
- `TryLockFor(timeout)` - Try to lock with timeout
- `Unlock()` - Unlock
- `GetName()` - Get name
- `GetLockCount()` - Get lock count
- `GetContentionCount()` - Get contention count

**Nested Class: ScopedLock** (RAII lock)

#### ReadWriteLock

**Methods:**
- `LockRead()` - Lock for reading
- `TryLockRead()` - Try to lock for reading
- `UnlockRead()` - Unlock read
- `LockWrite()` - Lock for writing
- `TryLockWrite()` - Try to lock for writing
- `UnlockWrite()` - Unlock write
- `GetName()` - Get name
- `GetReadLockCount()` - Get read lock count
- `GetWriteLockCount()` - Get write lock count

**Nested Classes: ReadLock, WriteLock** (RAII locks)

#### SyncBarrier

**Methods:**
- `Arrive()` - Arrive at barrier
- `WaitPhase()` - Wait for phase
- `Reset()` - Reset barrier
- `GetName()` - Get name
- `GetCount()` - Get count
- `GetWaiting()` - Get waiting count
- `GetPhase()` - Get phase

#### SyncEventListener

**Methods:**
- `Wait()` - Wait for event
- `WaitFor(timeout)` - Wait with timeout
- `Signal()` - Signal event
- `Reset()` - Reset event
- `IsTriggered()` - Check if triggered
- `GetEventName()` - Get event name

#### SyncSemaphoreCpu

**Methods:**
- `Acquire()` - Acquire semaphore
- `TryAcquire()` - Try to acquire
- `TimedAcquire(timeout)` - Timed acquire
- `Release()` - Release semaphore
- `GetCount()` - Get count
- `GetMaxCount()` - Get max count
- `GetName()` - Get name

### 2.4 Logging and Debug (Namespace: `AsyncIO::IO::LogDebug`)

#### DumpHelper

**Methods:**
- `HexDump(data, length)` - Hex dump to string
- `HexDumpToStream(data, length, stream)` - Hex dump to stream
- `BinaryDump(data, length)` - Binary dump
- `BitDump(data, length)` - Bit dump
- `InspectMemory(address, length)` - Inspect memory
- `RawStringToHex(string)` - String to hex
- `HexToRawString(hex)` - Hex to string
- `DumpMemoryToFile(data, length, filepath)` - Dump memory to file
- `DumpObjectToFile(obj, filepath)` - Dump object to file
- `GetDataStatistics(data, length)` - Get data statistics

#### GPUUsageLogger

**Methods:**
- `Initialize()` - Initialize logger
- `IsInitialized()` - Check initialization
- `StartLogging()` - Start logging
- `StopLogging()` - Stop logging
- `IsLogging()` - Check if logging
- `CaptureSnapshot()` - Capture snapshot
- `GetSnapshots()` - Get snapshots
- `GetAverageUtilization()` - Get average utilization
- `GetMaxUtilization()` - Get max utilization
- `GetAverageTemperature()` - Get average temperature
- `GetPeakMemoryUsage()` - Get peak memory
- `GetUsageReport()` - Get usage report
- `ClearSnapshots()` - Clear snapshots

#### MemoryTracker

**Methods:**
- `Enable()` - Enable tracking
- `Disable()` - Disable tracking
- `IsEnabled()` - Check if enabled
- `TrackAllocation(address, size, tag)` - Track allocation
- `TrackDeallocation(address, size, tag)` - Track deallocation
- `GetTotalAllocations()` - Get total allocations
- `GetTotalDeallocations()` - Get total deallocations
- `GetCurrentlyAllocatedMB()` - Get current allocation
- `GetPeakAllocationMB()` - Get peak allocation
- `GetAllocationsByTag(tag)` - Get allocations by tag
- `GetAllocationReport()` - Get allocation report
- `GetLiveAllocations()` - Get live allocations
- `DetectLeaks()` - Detect leaks

#### PerformanceCounter

**Methods:**
- `StartCounter(name)` - Start counter
- `StopCounter(name)` - Stop counter
- `ResetCounter(name)` - Reset counter
- `ResetAll()` - Reset all
- `GetMetric(name)` - Get metric
- `GetAllMetrics()` - Get all metrics
- `GetTotalTimeMS(name)` - Get total time
- `GetAverageTimeMS(name)` - Get average time
- `GetCallCount(name)` - Get call count
- `RecordElapsed(name, elapsedMS)` - Record elapsed time
- `GetReport()` - Get report
- `GetMetricReport(name)` - Get metric report
- `Enable()` - Enable counter
- `Disable()` - Disable counter
- `IsEnabled()` - Check if enabled

**Helper Class: ScopedTimer** (RAII timer)

---

## 3. Cache Subsystem (`src/cache`)

### 3.1 Cache Management (Namespace: `Engine::Cache`)

#### CacheManager

**Static Methods:**
- `Instance()` - Get singleton

**Methods:**
- `RootDirectory()` - Get root directory
- `Persistent()` - Get persistent storage
- `Temporary()` - Get temporary storage
- `EntryFor(scope, key, name)` - Create cache entry
- `DirectoryFor(scope, key)` - Get directory for scope/key
- `PathFor(scope, key, name)` - Get path for entry
- `WritePersistentText(scope, key, name, text, errorOut)` - Write persistent text
- `ReadPersistentText(scope, key, name, text, errorOut)` - Read persistent text
- `WritePersistentBinary(scope, key, name, bytes, errorOut)` - Write persistent binary
- `ReadPersistentBinary(scope, key, name, bytes, errorOut)` - Read persistent binary

#### CacheEncryptionHandler

**Methods:**
- `CompressText(text)` - Compress text
- `DecompressText(data)` - Decompress text

#### PersistentStorage

**Methods:**
- `root()` - Get root path
- `EnsureRootDirectory()` - Ensure root directory exists
- `DirectoryFor(scope, key)` - Get directory for scope/key
- `Exists(scope, key, name)` - Check if entry exists
- `Remove(scope, key, name)` - Remove entry
- `PathFor(scope, key, name)` - Get path for entry
- `ReadBinary(scope, key, name, bytes, errorOut)` - Read binary
- `WriteBinary(scope, key, name, bytes, errorOut)` - Write binary
- `ExistsRelative(scope, key, relativePath)` - Check if relative path exists
- `RemoveRelative(scope, key, relativePath)` - Remove relative path
- `ReadBinaryRelative(scope, key, relativePath, bytes, errorOut)` - Read binary relative
- `WriteBinaryRelative(scope, key, relativePath, bytes, errorOut)` - Write binary relative
- `ReadTextRelative(scope, key, relativePath, text, errorOut)` - Read text relative
- `WriteTextRelative(scope, key, relativePath, text, errorOut)` - Write text relative

#### TemporaryStorage

**Methods:**
- `Exists(scope, key, name)` - Check if entry exists
- `Remove(scope, key, name)` - Remove entry
- `PathFor(scope, key, name)` - Get path for entry
- `ReadBinary(scope, key, name, bytes, errorOut)` - Read binary
- `WriteBinary(scope, key, name, bytes, errorOut)` - Write binary

#### CacheStorage (Abstract Base Class)

**Virtual Methods:**
- `Exists(scope, key, name)` - Check if entry exists
- `Remove(scope, key, name)` - Remove entry
- `PathFor(scope, key, name)` - Get path for entry
- `ReadBinary(scope, key, name, bytes, errorOut)` - Read binary
- `WriteBinary(scope, key, name, bytes, errorOut)` - Write binary

**Methods:**
- `ReadText(scope, key, name, text, errorOut)` - Read text
- `WriteText(scope, key, name, text, errorOut)` - Write text

### 3.2 Free Functions

- `CacheConfigurationFromEnvironment()` - Get config from environment
- `PrepareCacheDirectory(config)` - Prepare cache directory
- `MakeEntry(scope, key, name)` - Create cache entry
- `MakeEntryWithTtl(scope, key, name, ttl)` - Create entry with TTL
- `IsValid(entry)` - Validate entry
- `HashKey(key)` - Hash key
- `SanitizeFileName(name)` - Sanitize filename
- `BuildScopedDirectory(scope, key)` - Build scoped directory
- `TempPathForTarget(scope, key, name)` - Get temp path
- `CommitTempFile(tempPath, targetPath)` - Commit temp file
- `ScriptLockPath(scope, key)` - Get script lock path
- `AcquireDirectoryLock(scope, key, options)` - Acquire directory lock
- `CleanupDirectoryLock(scope, key)` - Cleanup directory lock
- `CleanupScriptLock(scope, key)` - Cleanup script lock

### 3.3 Constants

- `kFlowScriptScope` - "flow_script"
- `kRuntimeLiveScope` - "runtime_live"
- `kErrorReportScope` - "error_report"

---

## 4. Audio Subsystem (`src/audio`)

### 4.1 Audio Core (Namespace: `Engine::Audio::Core`)

#### AudioAsyncManager

**Static Methods:**
- `Instance()` - Get singleton

**Methods:**
- `EnqueueAudioOperation(operation)` - Enqueue operation
- `ProcessAudioAsync(audio, callback)` - Async process audio
- `LoadAudioFileAsync(filepath, callback)` - Async load audio file
- `AnalyzeAudioAsync(audio, type, callback)` - Async analyze audio
- `ProcessEffectAsync(audio, effect, callback)` - Async process effect
- `Start()` - Start manager
- `Stop()` - Stop manager
- `IsRunning()` - Check if running
- `GetPendingOperationCount()` - Get pending count

#### AudioBitDepthConverter

**Methods:**
- `ConvertBuffer(input, output, inputDepth, outputDepth)` - Convert buffer
- `ConvertMultiChannel(input, output, inputDepth, outputDepth, channels)` - Convert multi-channel

**Static Methods:**
- `GetBytesPerSample(depth)` - Get bytes per sample
- `IsFloatingPoint(depth)` - Check if floating point
- `IsSupported(depth)` - Check if supported
- `NormalizeSamples(samples, depth)` - Normalize samples
- `DenormalizeSamples(samples, depth)` - Denormalize samples

#### AudioBuffer

**Methods:**
- `GetData()` - Get data pointer
- `GetFloatData()` - Get float data pointer
- `GetInfo()` - Get frame info
- `GetFrameCount()` - Get frame count
- `GetByteSize()` - Get byte size
- `GetSampleCount()` - Get sample count
- `Clear()` - Clear buffer
- `Fill(value)` - Fill with value
- `Resize(frameCount)` - Resize buffer

#### AudioBufferManager

**Static Methods:**
- `Instance()` - Get singleton

**Methods:**
- `AcquireBuffer(frameCount, channels, sampleRate)` - Acquire buffer
- `ReleaseBuffer(buffer)` - Release buffer
- `Resize(maxBuffers)` - Resize pool
- `Clear()` - Clear pool
- `GetAvailableBuffers()` - Get available count
- `GetTotalBuffers()` - Get total count
- `GetPoolStatus()` - Get pool status

#### AudioCacheManager

**Static Methods:**
- `Instance()` - Get singleton

**Methods:**
- `CacheAudioMetadata(filepath, metadata)` - Cache metadata
- `GetCachedMetadata(filepath)` - Get cached metadata
- `InvalidateMetadata(filepath)` - Invalidate metadata
- `ClearMetadataCache()` - Clear metadata cache
- `CacheEffectPreset(effectName, preset)` - Cache effect preset
- `GetCachedPreset(effectName)` - Get cached preset
- `InvalidatePreset(effectName)` - Invalidate preset
- `ClearPresetCache()` - Clear preset cache
- `CacheAnalysisResult(audioHash, type, result)` - Cache analysis result
- `GetCachedAnalysisResult(audioHash, type)` - Get cached analysis
- `InvalidateAnalysisResult(audioHash, type)` - Invalidate analysis
- `ClearAnalysisCache()` - Clear analysis cache
- `GetMetadataCount()` - Get metadata count
- `GetPresetCount()` - Get preset count
- `GetAnalysisCount()` - Get analysis count
- `SetDefaultMetadataTtl(ttl)` - Set metadata TTL
- `SetDefaultPresetTtl(ttl)` - Set preset TTL
- `SetDefaultAnalysisTtl(ttl)` - Set analysis TTL
- `GetDefaultMetadataTtl()` - Get metadata TTL
- `GetDefaultPresetTtl()` - Get preset TTL
- `GetDefaultAnalysisTtl()` - Get analysis TTL

#### AudioClockSync

**Methods:**
- `Initialize()` - Initialize clock
- `IsInitialized()` - Check initialization
- `UpdateClock()` - Update clock
- `SyncToExternal(timestamp)` - Sync to external clock
- `GetClockInfo()` - Get clock info
- `GetCurrentFrame()` - Get current frame
- `GetCurrentTime()` - Get current time
- `GetDriftPercentage()` - Get drift percentage
- `CheckClockLock()` - Check clock lock
- `CalculateSampleRateRatio(targetRate)` - Calculate sample rate ratio

#### AudioEngine

**Methods:**
- `Initialize(sampleRate, bufferSize, channelCount)` - Initialize engine
- `IsInitialized()` - Check initialization
- `Start()` - Start engine
- `Stop()` - Stop engine
- `IsRunning()` - Check if running
- `SetBufferSize(frames)` - Set buffer size
- `SetSampleRate(sampleRate)` - Set sample rate
- `GetSampleRate()` - Get sample rate
- `GetBufferSize()` - Get buffer size
- `GetChannelCount()` - Get channel count
- `SetProcessCallback(callback)` - Set process callback
- `ProcessAudio(inputs, outputs, frameCount)` - Process audio
- `GetInputLevels()` - Get input levels
- `GetOutputLevels()` - Get output levels
- `GetCPULoad()` - Get CPU load
- `GetEngineInfo()` - Get engine info

#### AudioFIFOQueue

**Methods:**
- `Write(data, frameCount)` - Write data (single channel)
- `Write(data, frameCount, channels)` - Write data (multi-channel)
- `Read(output, frameCount)` - Read data (single channel)
- `Read(output, frameCount, channels)` - Read data (multi-channel)
- `GetAvailableFrames()` - Get available frames
- `GetCapacity()` - Get capacity
- `IsEmpty()` - Check if empty
- `IsFull()` - Check if full
- `Clear()` - Clear queue
- `Reset()` - Reset queue
- `GetTotalWritten()` - Get total written
- `GetTotalRead()` - Get total read
- `GetOverflowCount()` - Get overflow count

#### AudioInterleaveProcessor

**Static Methods:**
- `Interleave(input, output, frameCount, channels)` - Interleave data
- `Deinterleave(input, output, frameCount, channels)` - Deinterleave data
- `InterleavePlanar(input, output, frameCount, channels)` - Interleave planar
- `DeinterleavePlanar(input, output, frameCount, channels)` - Deinterleave planar
- `DuplicateChannel(input, output, frameCount, sourceChannel, targetChannel)` - Duplicate channel
- `MixChannels(input1, input2, output, frameCount, mix)` - Mix channels

#### LatencyMonitor

**Methods:**
- `StartMonitoring()` - Start monitoring
- `StopMonitoring()` - Stop monitoring
- `IsMonitoring()` - Check if monitoring
- `RecordInputTimestamp(timestamp)` - Record input timestamp
- `RecordOutputTimestamp(timestamp)` - Record output timestamp
- `CalculateLatency()` - Calculate latency
- `GetLatencyHistory()` - Get latency history
- `GetAverageLatency()` - Get average latency
- `GetMaxLatency()` - Get max latency
- `GetMinLatency()` - Get min latency
- `GetPerformanceCounter()` - Get performance counter
- `GetLatencyReport()` - Get latency report

#### AudioSampleRateConverter

**Methods:**
- `Initialize(inputRate, outputRate, quality)` - Initialize converter
- `IsInitialized()` - Check initialization
- `Convert(input, output, frameCount)` - Convert audio
- `ConvertMultiChannel(input, output, frameCount, channels)` - Convert multi-channel
- `GetRatio()` - Get conversion ratio
- `GetInputRate()` - Get input rate
- `GetOutputRate()` - Get output rate
- `GetQuality()` - Get quality
- `Reset()` - Reset converter
- `GetInfo()` - Get info

#### AudioSourceLoader

**Static Methods:**
- `Load(filepath, options)` - Load audio file
- `LoadAsync(filepath, options, callback)` - Load audio file async

#### AudioThreadPriority

**Static Methods:**
- `SetCurrentThreadPriority(priority)` - Set current thread priority
- `SetThreadPriority(threadId, priority)` - Set thread priority
- `SetThreadAffinity(threadId, coreMask)` - Set thread affinity
- `GetOptimalCore()` - Get optimal core
- `GetCurrentPriority()` - Get current priority
- `GetPriorityName(priority)` - Get priority name
- `SetAffinity(threadId, coreId)` - Set affinity
- `GetOptimalAudioCore()` - Get optimal audio core

#### AudioLogger

**Static Methods:**
- `Instance()` - Get singleton

**Methods:**
- `LogEngineStart(config)` - Log engine start
- `LogEngineStop()` - Log engine stop
- `LogEngineError(error)` - Log engine error
- `LogEffectLoad(effectName)` - Log effect load
- `LogEffectProcess(effectName, time)` - Log effect process
- `LogEffectError(effectName, error)` - Log effect error
- `LogAnalysisStart(type)` - Log analysis start
- `LogAnalysisComplete(type, time)` - Log analysis complete
- `LogAnalysisError(type, error)` - Log analysis error
- `LogFileLoad(filepath)` - Log file load
- `LogFileSave(filepath)` - Log file save
- `LogFileError(filepath, error)` - Log file error
- `LogBufferAllocation(size)` - Log buffer allocation
- `LogBufferDeallocation(size)` - Log buffer deallocation
- `LogFFTOperation(size, time)` - Log FFT operation
- `LogFilterOperation(filter, time)` - Log filter operation

#### AudioEngineHost

**Methods:**
- `CreateEngine(name)` - Create engine
- `DestroyEngine(name)` - Destroy engine
- `GetEngine(name)` - Get engine
- `StartAllEngines()` - Start all engines
- `StopAllEngines()` - Stop all engines
- `GetEngineList()` - Get engine list
- `SetGlobalSampleRate(sampleRate)` - Set global sample rate
- `SetGlobalBufferSize(bufferSize)` - Set global buffer size
- `GetEngineCount()` - Get engine count
- `GetHostInfo()` - Get host info

### 4.2 DSP Algorithms (Namespace: `Engine::Audio::DSP`)

#### BiquadProcessor

**Methods:**
- `Configure(type, frequency, Q, gain)` - Configure filter
- `ProcessSample(input)` - Process sample
- `Reset()` - Reset filter
- `GetReport()` - Get report

#### FFTEngine

**Methods:**
- `Forward(input)` - Forward FFT
- `Inverse(input)` - Inverse FFT
- `Reset()` - Reset engine
- `GetSpectrum()` - Get spectrum
- `MutableSpectrum()` - Get mutable spectrum
- `GetMagnitude()` - Get magnitude
- `GetReport()` - Get report

#### ConvolutionEngine

**Methods:**
- `GetMemoryStats()` - Get memory stats
- `SetImpulseResponse(impulse)` - Set impulse response
- `ProcessBlock(input, output)` - Process block
- `Reset()` - Reset engine
- `ProcessBlockAsync(input, output, callback)` - Async process block
- `GetImpulseSize()` - Get impulse size
- `GetReport()` - Get report

#### WindowingFunctions

**Static Methods:**
- `GenerateHann(size)` - Generate Hann window
- `GenerateSqrtHann(size)` - Generate sqrt Hann window
- `GenerateHamming(size)` - Generate Hamming window
- `GenerateBlackman(size)` - Generate Blackman window
- `ApplyWindow(data, window)` - Apply window
- `DescribeWindow(type)` - Describe window

### 4.3 Effects Rack (Namespace: `Engine::Audio::FX`)

#### CustomEffectFlow

**Methods:**
- `Initialize(package)` - Initialize flow
- `Process(input, output)` - Process audio
- `GetReport()` - Get report
- `GetPackage()` - Get package
- `GetLastReport()` - Get last report

#### CustomEffectProcessor

**Methods:**
- `Initialize(package)` - Initialize processor
- `ProcessBlock(input, output, frameCount)` - Process block
- `ProcessBuffer(input, output)` - Process buffer
- `ResetFrameOffset()` - Reset frame offset
- `GetReport()` - Get report
- `GetNode()` - Get node

### 4.4 Free Functions (Effects)

- `BuildSmokeCustomEffectPackage()` - Build smoke package
- `BuildExampleCustomEffectPackage()` - Build example package
- `BuildNamedCustomEffectPackage(name)` - Build named package
- `ListAvailableCustomEffects()` - List available effects
- `RenderCustomEffectExample(package)` - Render example
- `RunCustomEffectPackage(package, input, output)` - Run package
- `RunCustomEffectPackageInterleaved(package, input, output)` - Run package interleaved
- `SerializeCustomEffectPackage(package)` - Serialize package
- `DeserializeCustomEffectPackage(data)` - Deserialize package
- `CustomEffectBackendToString(backend)` - Backend to string
- `ParseCustomEffectBackend(string)` - Parse backend
- `CustomEffectAlgorithmToString(algorithm)` - Algorithm to string
- `ParseCustomEffectAlgorithm(string)` - Parse algorithm
- `BuildCustomEffectReportText(report)` - Build report text
- `ApplyCustomEffectProcessors(processors, input, output)` - Apply processors

### 4.5 Analysis AI (Namespace: `Engine::Audio::AnalysisAI`)

#### AudioFFTAnalyzer

**Methods:**
- `ComputeFFT(audio)` - Compute FFT
- `GetFFTOutput()` - Get FFT output
- `GetMagnitudeSpectrum()` - Get magnitude spectrum
- `GetPhaseSpectrum()` - Get phase spectrum
- `AnalyzeSpectrum()` - Analyze spectrum
- `GetDominantFrequency()` - Get dominant frequency
- `GetFrequencyBands()` - Get frequency bands
- `GetTotalPower()` - Get total power
- `GetRMSEnergy()` - Get RMS energy
- `GetAnalysisReport()` - Get analysis report

#### PitchEstimator

**Methods:**
- `Estimate(audio)` - Estimate pitch
- `Reset()` - Reset estimator
- `GetLastPitch()` - Get last pitch
- `GetLastHarmonics()` - Get last harmonics
- `GetLastNote()` - Get last note
- `GetReport()` - Get report

#### AudioPitchDetector

**Methods:**
- `DetectPitch(audio)` - Detect pitch
- `IsPitched()` - Check if pitched
- `GetFundamentalFrequency()` - Get fundamental frequency
- `GetHarmonics()` - Get harmonics
- `FrequencyToNote(frequency)` - Frequency to note

**Static Methods:**
- `NoteToFrequency(note)` - Note to frequency

**Methods:**
- `GetConfidence()` - Get confidence
- `GetSampleRate()` - Get sample rate

---

## 5. Flux Subsystem (`src/flux`)

### 5.1 Core (Namespace: `flux::core`)

#### FluxConfig (Struct)

**Fields:**
- `application_name` - Application name
- `log_level` - Log level
- `colors` - Enable colors
- `timestamps` - Enable timestamps
- `terminal_size` - Terminal size

#### FluxContext

**Methods:**
- `config()` - Get config
- `logger()` - Get logger
- `window()` - Get terminal window
- `RefreshTerminalWindow()` - Refresh terminal window
- `SetValue(key, value)` - Set value
- `HasValue(key)` - Check if has value
- `GetValue(key)` - Get value
- `Keys()` - Get keys

#### FluxCore

**Methods:**
- `context()` - Get context
- `module_manager()` - Get module manager
- `RegisterModule(module, errorOut)` - Register module
- `Initialize(errorOut)` - Initialize core
- `Shutdown()` - Shutdown core
- `is_running()` - Check if running

#### Logger

**Methods:**
- `UpdateConfig(config)` - Update config
- `config()` - Get config
- `ShouldLog(level)` - Check if should log
- `Log(level, message)` - Log message
- `Debug(message)` - Debug log
- `Info(message)` - Info log
- `Warning(message)` - Warning log
- `Error(message)` - Error log

#### ModuleManager

**Methods:**
- `Register(module)` - Register module
- `HasModule(name)` - Check if has module
- `ModuleNames()` - Get module names
- `StartAll()` - Start all modules
- `StopAll()` - Stop all modules

### 5.2 Free Functions (Core)

- `FluxConfigFromEnvironment()` - Get config from environment
- `ParseLogLevel(string)` - Parse log level
- `ToString(level)` - Log level to string
- `BuildConfigSummary(config)` - Build config summary

### 5.3 Terminal (Namespace: `flux::terminal`)

#### TerminalWindow

**Methods:**
- `size()` - Get size
- `set_size(size)` - Set size
- `RefreshSize()` - Refresh size
- `FitToWidth(text, width)` - Fit text to width
- `Centered(text, width)` - Center text
- `MakeRule(width, char)` - Make rule
- `WriteRule(width, char)` - Write rule
- `WriteTitle(title, width)` - Write title

#### TerminalEmulator

**Methods:**
- `Feed(text)` - Feed text
- `Reset()` - Reset emulator
- `PlainText()` - Get plain text

### 5.4 Free Functions (Terminal)

- `DefaultTerminalSize()` - Get default size
- `DetectTerminalSize()` - Detect terminal size
- `ClampTerminalSize(size)` - Clamp terminal size
- `EmulatePlainText(text)` - Emulate plain text
- `GetDefaultTerminal()` - Get default terminal
- `SetDefaultTerminal(terminal)` - Set default terminal

### 5.5 Console (Namespace: `flux::console`)

#### Free Functions

- `Utf8(value)` - Convert V8 value to UTF8
- `JoinValues(values)` - Join V8 values
- `JoinArguments(args)` - Join V8 arguments
- `WriteLine(stream, text)` - Write line

---

## 6. Implementation Priority

### Phase 1: Foundation (High Priority)
1. **Helper** - V8 integration, async bridge, utilities
2. **Async IO** - Thread pool, file I/O (core infrastructure)
3. **Cache** - Storage layer (needed by audio)

### Phase 2: Core Features (Medium Priority)
4. **Audio Core** - Audio engine, buffers, sample rate conversion
5. **Async IO Hardware** - System monitoring, hardware abstraction

### Phase 3: Advanced Features (Lower Priority)
6. **Audio DSP** - FFT, filters, effects
7. **Audio Analysis** - Pitch detection, spectral analysis
8. **Flux** - Terminal UI, console utilities

---

## 7. Special Considerations

### Thread Safety
- Many classes use internal mutexes
- Async operations require careful JavaScript integration
- Callbacks may run on different threads

### Memory Management
- Shared pointers need careful handling
- Buffer pools require lifecycle management
- Weak cleanup registration for GC

### Error Handling
- Most methods return bool + error_out pattern
- Need consistent error propagation to JavaScript
- Consider promise-based async API

### Performance
- Audio processing requires zero-copy where possible
- Thread pool integration critical for async ops
- Avoid excessive V8 context switches
