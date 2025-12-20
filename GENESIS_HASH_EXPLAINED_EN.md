# Genesis Hash: How Nodes Stay in Sync

## Overview

### What is the Genesis Hash?

The genesis hash is a unique cryptographic fingerprint of the first block (genesis block) in the Chronos blockchain. This hash ensures that all nodes in the network start from **exactly the same initial state**. If even a single bit differs in the genesis block, the hash will be completely different, allowing nodes to detect incompatibility.

### Why is the Genesis Hash Important?

The genesis hash serves as both **network identification** and **consistency verification**:

1. **Network Identification**: Nodes with the same genesis hash belong to the same network
2. **Fork Prevention**: Prevents nodes from accidentally following different chains
3. **Reproducible Initialization**: Guarantees everyone with the same configuration starts the same blockchain
4. **Configuration Error Detection**: If a node has an incorrect genesis hash, it's immediately detected

### How Does the Genesis Block Work?

The genesis block in Chronos has a specific structure that is **deterministic** - meaning with the same inputs, you always get the same outputs.

#### Genesis Block Structure

The genesis block contains the following fields:

```cpp
// Genesis block fields
prev_block_hash:           32 bytes of zeros (0x00000000...)
height:                    0 (the first block)
consensus_time:            Configurable timestamp (in milliseconds)
timestamp:                 Unix timestamp (in seconds)
transactions:              Empty (no transactions in genesis)
transactions_merkle_root:  Hash of empty transaction list
```

#### Genesis Hash Calculation

The genesis hash is calculated through the following steps:

```
1. Serialize all header fields:
   - prev_block_hash (32 bytes)
   - timestamp (8 bytes, uint64_t)
   - transactions_merkle_root (32 bytes)
   - height (8 bytes, uint64_t) 
   - consensus_time (8 bytes, uint64_t)

2. Concatenate all bytes in fixed order

3. Compute BLAKE3 hash (256-bit / 32 bytes):
   genesis_hash = BLAKE3(serialized_header)
```

**Important**: The hash is fully deterministic. If two nodes use the same configuration, they get **exactly the same genesis hash**.

### Implementation in Chronos

#### Code Location

The genesis block is created in `src/node/node_app.cpp` in the `run()` method:

```cpp
// Create genesis block if blockchain is empty
Bytes zero_hash(32, 0);  // Previous hash = all zeros
uint64_t genesis_consensus_time = cfg_.genesis_consensus_time;

Block genesis_block(zero_hash, 0, genesis_consensus_time, {});

// Validate against expected hash
if (!cfg_.genesis_expected_hash.empty()) {
    Bytes expected_hash = hex_to_bytes(cfg_.genesis_expected_hash);
    Bytes actual_hash = genesis_block.get_header_hash();
    
    if (expected_hash != actual_hash) {
        // ERROR! This node has a different genesis configuration
        throw std::runtime_error("Genesis block hash mismatch!");
    }
}
```

#### Hash Calculation

The hash is computed in `src/ledger/block.cpp`:

```cpp
Bytes Block::get_header_hash() const {
    Bytes header_data;
    
    // 1. Append prev_block_hash (32 bytes)
    header_data.insert(header_data.end(), 
                      prev_block_hash.begin(), 
                      prev_block_hash.end());
    
    // 2. Append timestamp (8 bytes, little-endian)
    Bytes timestamp_bytes(sizeof(timestamp));
    std::memcpy(timestamp_bytes.data(), &timestamp, sizeof(timestamp));
    header_data.insert(header_data.end(), 
                      timestamp_bytes.begin(), 
                      timestamp_bytes.end());
    
    // 3. Append merkle root (32 bytes)
    header_data.insert(header_data.end(), 
                      transactions_merkle_root.begin(), 
                      transactions_merkle_root.end());
    
    // 4. Append height (8 bytes, little-endian)
    Bytes height_bytes(sizeof(height));
    std::memcpy(height_bytes.data(), &height, sizeof(height));
    header_data.insert(header_data.end(), 
                      height_bytes.begin(), 
                      height_bytes.end());
    
    // 5. Append consensus_time (8 bytes, little-endian)
    Bytes consensus_time_bytes(sizeof(consensus_time));
    std::memcpy(consensus_time_bytes.data(), &consensus_time, sizeof(consensus_time));
    header_data.insert(header_data.end(), 
                      consensus_time_bytes.begin(), 
                      consensus_time_bytes.end());
    
    // 6. Compute BLAKE3 hash
    return blake3(header_data);
}
```

### Configuration: Setting the Genesis Hash

In `config/default.toml` you can configure the expected genesis hash:

```toml
[genesis]
# Consensus time for genesis block (milliseconds since epoch)
# Use a fixed value for reproducible genesis hash
consensus_time = 1704067200000  # January 1, 2024 00:00:00 UTC

# Expected hash of the genesis block (hex-encoded)
# All nodes in the network MUST have this hash
expected_hash = "a1b2c3d4e5f6..."  # Replace with your actual genesis hash

# Maximum balance per account (overflow protection)
max_supply_per_account = 1000000000000000

# Genesis allocations: initial balances
[genesis.allocations]
"cqc1q..." = 1000000000  # First account gets 1 billion nanos
"cqc1z..." = 500000000   # Second account gets 500 million nanos
```

### Generating Genesis Hash: Step-by-Step

#### Step 1: Configure Genesis Parameters

Edit `config/default.toml`:

```toml
[genesis]
consensus_time = 1704067200000  # Fixed timestamp
expected_hash = ""               # Leave empty for first run

[genesis.allocations]
"cqc1validatoraddr123..." = 1000000000
```

#### Step 2: Start the Node (First Time)

```bash
./chronos_node --config config/default.toml
```

The node will:
1. Detect that no blockchain exists
2. Create a genesis block
3. Log the hash in the console and logfile

Look in the output for:

```
[CONSENSUS] Genesis block created with hash: a1b2c3d4e5f6789...
```

#### Step 3: Copy the Genesis Hash

Copy the complete hex string of the genesis hash.

#### Step 4: Update Configuration

Add the hash to `config/default.toml`:

```toml
[genesis]
consensus_time = 1704067200000
expected_hash = "a1b2c3d4e5f6789..."  # Pasted hash from step 3
```

#### Step 5: Distribute Configuration

**IMPORTANT**: All nodes in the network must use **exactly the same** `config/default.toml`:
- Same `genesis.consensus_time`
- Same `genesis.allocations` (order doesn't matter, but addresses and balances must be identical)
- Same `genesis.expected_hash`

### How Nodes Validate Each Other

When a node starts with a non-empty blockchain:

```cpp
// Load existing blockchain
auto height_data = blockchain_storage_->getMetadata(NEXT_BLOCK_HEIGHT_KEY);
auto hash_data = blockchain_storage_->getMetadata(LAST_BLOCK_HASH_KEY);

// Use loaded values
last_block_hash_ = *hash_data;
next_block_height_ = loaded_height;
```

When nodes connect to each other:
1. They exchange block headers via P2P gossip
2. If a node receives a block with `height = 0`, it checks the hash
3. If the genesis hash doesn't match, the peer is considered **incompatible**

### Practical Example

#### Scenario: Network with 3 Validators

**Nodes A, B, and C want to start the same network.**

1. **Coordinator (e.g., Node A)** creates initial configuration:
   ```toml
   [genesis]
   consensus_time = 1704067200000
   expected_hash = ""
   
   [genesis.allocations]
   "cqc1validator_a..." = 1000000000
   "cqc1validator_b..." = 1000000000
   "cqc1validator_c..." = 1000000000
   ```

2. **Node A starts** and logs:
   ```
   Genesis block created with hash: 4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c6e5d8a1f4b7c2e9d6a3f8b1c4e7d2a5f
   ```

3. **Coordinator distributes** the complete configuration (with genesis hash) to Nodes B and C

4. **All nodes use exactly the same config**:
   ```toml
   [genesis]
   consensus_time = 1704067200000
   expected_hash = "4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c6e5d8a1f4b7c2e9d6a3f8b1c4e7d2a5f"
   ```

5. **Nodes B and C start** and validate:
   ```
   [CONSENSUS] Genesis block hash validated successfully
   ```

#### What Happens on Error?

If Node C accidentally uses incorrect configuration:

```toml
# ERROR: Wrong consensus_time!
consensus_time = 1704067200001  # +1 millisecond difference
```

Then Node C will crash on start:

```
[ERROR] Genesis block hash mismatch!
Expected: 4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c6e5d8a1f4b7c2e9d6a3f8b1c4e7d2a5f
Got:      9a3c5e7f1b4d6a8c2e0f4b7d9a1c3e5f7b9d1f3a5c7e9b1d3f5a7c9e1b3d5f7a

Genesis block hash mismatch!
```

This prevents Node C from accidentally creating a **fork** or joining the wrong network.

### Important Considerations

#### 1. Determinism is Crucial

The genesis hash is only reliable if the calculation is **deterministic**:

- ✅ **Use fixed consensus_time**: Don't use system time
- ✅ **Fixed byte order**: Always the same serialization
- ✅ **Identical allocations**: All addresses and balances must match
- ⚠️ **Watch the timestamp**: The `timestamp` field is automatically set at construction and can vary

**Current Limitation**: The `timestamp` field is set via `std::chrono::system_clock::now()`, which is **non-deterministic**. For a fully deterministic genesis block, this should also be configured.

#### 2. Genesis Allocations Affect the Hash

The genesis block itself contains **no transactions**, but the configuration of `genesis.allocations` must be **identical** on all nodes because:

1. Allocations are applied to State
2. State affects network operation
3. Although allocations aren't in the block, consistency is crucial

**Best Practice**: Always distribute the **complete** configuration file, not just the hash.

#### 3. Changes Require New Genesis

If you modify the genesis configuration:
- The genesis hash changes
- This creates a **new network**
- Old nodes can no longer connect

This is **desired behavior** for:
- Testnet vs Mainnet distinction
- Different forks/versions of the chain
- Isolated development environments

### Technical Details

#### BLAKE3 Hash Function

Chronos uses BLAKE3 for all cryptographic hashing:

- **Output**: 256-bit (32 bytes)
- **Performance**: Faster than SHA-256, SHA-3
- **Security**: Post-quantum resistant against hash collision attacks
- **Determinism**: Guaranteed deterministic for same input

#### Byte Ordering (Endianness)

The current implementation uses **native endianness** (usually little-endian on modern systems):

```cpp
std::memcpy(timestamp_bytes.data(), &timestamp, sizeof(timestamp));
```

**Future Improvement**: For maximum portability, explicit little-endian encoding should be used (see `src/util/codec.hpp`).

### Tests

The genesis hash functionality is tested in `tests/test_genesis.cpp`:

```cpp
TEST_CASE(GenesisHashValidation, "Genesis Hash Validation") {
    // Create two identical genesis blocks
    Bytes zero_hash(32, 0);
    
    Block genesis1;
    genesis1.prev_block_hash = zero_hash;
    genesis1.height = 0;
    genesis1.consensus_time = 1704067200000;
    genesis1.timestamp = 1704067200;
    genesis1.transactions = {};
    genesis1.calculate_merkle_root();
    
    Block genesis2;
    genesis2.prev_block_hash = zero_hash;
    genesis2.height = 0;
    genesis2.consensus_time = 1704067200000;
    genesis2.timestamp = 1704067200;
    genesis2.transactions = {};
    genesis2.calculate_merkle_root();
    
    Bytes hash1 = genesis1.get_header_hash();
    Bytes hash2 = genesis2.get_header_hash();
    
    // Hashes must be identical
    ASSERT_TRUE(hash1 == hash2);
}
```

### Frequently Asked Questions

**Q: Can I manually calculate the genesis hash?**

A: Yes, if you know the exact field values. Use the formula:
```
genesis_hash = BLAKE3(
    zero_bytes(32) ||           // prev_block_hash
    uint64_le(timestamp) ||     // timestamp in seconds
    merkle_root(empty_txs) ||   // merkle root of empty tx list
    uint64_le(0) ||             // height = 0
    uint64_le(consensus_time)   // consensus_time in milliseconds
)
```

**Q: What if my node detects an incorrect genesis hash?**

A: The node will crash with an error message. This is **desired behavior** - it prevents your node from accidentally joining the wrong network. Check your configuration and ensure all fields match.

**Q: Can two different networks have the same genesis hash?**

A: Technically possible but **extremely unlikely**. BLAKE3 is a cryptographic hash function where even a 1-bit difference in input results in a completely different 256-bit output. The chance of a collision is astronomically small (2^-256).

**Q: Should I share the genesis hash with other node operators?**

A: Yes, share the **complete configuration file** (`config/default.toml`) with all node operators who want to join your network. The genesis hash alone is not sufficient - they also need the allocations and other parameters.

**Q: What happens if I accidentally use the wrong genesis configuration?**

A: Your node will start its **own, isolated blockchain** that is incompatible with the main network. Other nodes will reject your blocks and you cannot synchronize. Delete your `data/` directory and restart with the correct configuration.

### Summary

The genesis hash in Chronos is a **fundamental security feature** that guarantees:

1. ✅ All nodes have the same starting point
2. ✅ Configuration errors are immediately detected  
3. ✅ Network isolation is possible (testnet vs mainnet)
4. ✅ No accidental forks can occur

**Critical Steps for a New Network:**
1. Configure genesis parameters (consensus_time, allocations)
2. Start first node and note the genesis hash
3. Distribute complete configuration to all validators
4. All nodes validate automatically on start

**Remember**: The genesis hash is a *cryptographic guarantee* that all nodes follow the same rules from the very first block.
