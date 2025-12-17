# Error Handling Gaps Analysis - Session 5

## Executive Summary
Current error handling is minimal and reactive. Most critical operations lack:
- Structured error information (just throw or return false)
- Retry logic for transient failures
- Graceful degradation strategies
- Context-aware error messages
- Recovery paths beyond crash

## Critical Error Handling Gaps

### 1. Storage Operations (HIGHEST RISK)
**Current State:**
```cpp
// node_app.cpp:500
blockchain_storage_->saveBlock(b);  // No error check, can fail silently

// node_app.cpp:507-508
blockchain_storage_->saveMetadata(NEXT_BLOCK_HEIGHT_KEY, height_bytes);
blockchain_storage_->saveMetadata(LAST_BLOCK_HASH_KEY, b.get_header_hash());
// No error handling - if metadata save fails, state is corrupted!

// node_app.cpp:197-202
auto height_data = blockchain_storage_->getMetadata(NEXT_BLOCK_HEIGHT_KEY);
if (!height_data) { ... }  // Silent failure, assumes defaults

// node_app.cpp:855
std::optional<Block> block = blockchain_storage_->getBlock(from_hash);
// No retry, no logging for failures
```

**Risks:**
- Block saved but metadata not → blockchain corruption
- Read failures return empty → invalid state assumptions
- Disk full/permission errors → undetected data loss
- No retry on transient I/O errors

**Required Fixes:**
- Return status/error codes from saveBlock/saveMetadata
- Add retry logic with exponential backoff (max 3 retries)
- Log all storage failures with context
- Implement transaction/atomic writes (save block + metadata together)
- Graceful degradation: stop consensus if storage fails

### 2. Network/P2P Operations (HIGH RISK)
**Current State:**
```cpp
// node_app.cpp:855 - GetBlocks request
std::optional<Block> block = blockchain_storage_->getBlock(from_hash);
if (!block) {
    // Peer asked for non-existent block
    LOG_INFO(P2P, "Block not found...");
    // No response sent! Peer hangs indefinitely
}

// p2p_client.cpp - Message sending
socket_.send(data);  // Can fail, no retry mechanism

// gossip.cpp - Peer discovery
// Only tries once, no reconnection logic
```

**Risks:**
- Peers don't know if block request succeeded
- Network hiccups cause permanent disconnects
- DoS vectors from peer asking for non-existent blocks
- No exponential backoff for failing peers

**Required Fixes:**
- Send explicit "block not found" or "not available" response
- Implement GetBlocks response with multiple blocks (batching)
- Add automatic retry for transient failures (exponential backoff)
- Circuit breaker for consistently failing peers
- Peer timeouts and health checks

### 3. Consensus Operations (MEDIUM RISK)
**Current State:**
```cpp
// bft.cpp - Message validation
if (height == 0) return false;  // Silent failure

// node_app.cpp - Block finalization
try {
    received_block = Block::deserialize(...);
} catch (const std::exception& e) {
    LOG_WARN(P2P, "Failed to deserialize block from {}: {}. Penalizing.",
             sender_id, e.what());
    update_peer_score(sender_id, -10, true);
    break;  // Just skip the message, no recovery
}
```

**Risks:**
- Consensus can get stuck if leader can't propose
- Invalid blocks cause complete message rejection
- No timeout for stalled rounds
- No fallback to new leader if current stuck

**Required Fixes:**
- Add consensus timeout detection (e.g., 30s per round)
- Implement leader rotation on timeout
- Add block proposal retry with backoff
- Log consensus state transitions with context
- Circuit breaker: fallback to light sync if stuck

### 4. Configuration Operations (LOW-MEDIUM RISK)
**Current State:**
```cpp
// config.cpp
Config cfg = Config::load(config_path);  // Throws on parse error
// If throws, application crashes immediately
// No validation of loaded values

// node_app.cpp:107-120
if (cfg.node_type == NodeType::FULL) {
    blockchain_storage_ = std::make_unique<DiskBlockchainStorage>(kv);
}
// No error handling if DiskBlockchainStorage init fails
```

**Risks:**
- Invalid config crashes node (no graceful error message)
- Missing required keys not caught early
- Invalid values (negative ports, etc.) not validated
- No fallback to default config

**Required Fixes:**
- Structured config validation with specific error messages
- Try-catch for config load with helpful error output
- Validate all numeric fields (ports, timeouts, limits)
- Validate file paths exist and readable
- Optional fallback to default/safe config

### 5. State & Snapshot Operations (MEDIUM RISK)
**Current State:**
```cpp
// snapshots.cpp:47-48
if (!saved) {
    LOG_ERROR(STORAGE, "Failed to save snapshot at height {}", height);
    return false;  // Snapshot lost, no recovery attempt
}

// node_app.cpp - start_from_snapshot
// No validation that restored state is correct
// If corrupt snapshot, silently loads corrupt state
```

**Risks:**
- Failed snapshots lose recovery points
- Corrupt snapshots silently load bad state
- No integrity checks on snapshot data
- No backup/versioning of snapshots

**Required Fixes:**
- Retry snapshot save with backoff
- Add checksum/hash validation to snapshots
- Implement snapshot versioning (keep last 3)
- Validate restored state is reasonable
- Fallback: resume from genesis if all snapshots corrupt

## Error Handling Patterns to Implement

### Pattern 1: Structured Error Codes
```cpp
enum class StorageError {
    IOError,
    NotFound,
    Corrupted,
    DiskFull,
    PermissionDenied,
    TransactionFailed
};

struct Result<T> {
    bool success;
    T value;
    StorageError error;
    std::string message;
};
```

### Pattern 2: Exponential Backoff Retry
```cpp
template<typename Func>
bool retry_with_backoff(Func operation, int max_retries = 3) {
    int backoff_ms = 100;
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        try {
            operation();
            return true;
        } catch (const std::exception& e) {
            if (attempt < max_retries - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms *= 2;  // Exponential increase
            }
        }
    }
    return false;
}
```

### Pattern 3: Context-Aware Logging
```cpp
LOG_ERROR(STORAGE, 
    "Failed to save block (height={}, hash={}, attempt={}/{}): {}. "
    "Recovery: Retrying with exponential backoff...",
    height, bytes_to_hex(hash), attempt + 1, max_retries, error);
```

### Pattern 4: Graceful Degradation
```cpp
// Try full sync, fallback to light sync
if (!sync_blockchain_full()) {
    LOG_WARN(P2P, "Full sync failed, switching to light sync mode");
    enable_light_sync();
}
```

## Implementation Priority

### Phase 1 (Critical - This Session)
1. Storage operations error handling
   - Add error codes to saveBlock/saveMetadata
   - Implement retry logic
   - Add atomic transaction support
   
2. Enhanced error logging
   - Context-aware messages for all critical operations
   - Error chains and root causes
   - Recovery action documentation

### Phase 2 (Important - Next Session)
3. Network retry logic
   - Exponential backoff for peer requests
   - Circuit breaker for failing peers
   - GetBlocks response batching

4. Consensus timeout detection
   - Round timeout (30s default)
   - Leader rotation on timeout
   - Stuck consensus recovery

### Phase 3 (Nice-to-Have)
5. Configuration validation
   - Structured config parsing errors
   - Value range validation
   - Path/file existence checks

6. Snapshot integrity
   - Checksum validation
   - Versioning system
   - Corruption recovery

## Test Coverage Needed
- Storage operation failures (I/O, disk full, permissions)
- Network timeouts and retries
- Consensus stuck scenarios
- Config parse errors
- Snapshot corruption and recovery
- Cascading failures (e.g., storage failure during consensus)

## Success Metrics
✓ All critical operations have structured error handling
✓ Transient failures auto-retry with exponential backoff
✓ Errors logged with context (operation, reason, recovery)
✓ System gracefully degrades instead of crashing
✓ >90% test coverage for error paths
