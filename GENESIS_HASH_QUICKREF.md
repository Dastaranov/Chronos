# Genesis Hash Quick Reference

## What is it?
The genesis hash is a 32-byte BLAKE3 hash that uniquely identifies the first block (genesis block) of the Chronos blockchain.

## Why is it important?
- **Network Identity**: All nodes with the same genesis hash belong to the same network
- **Fork Prevention**: Prevents accidental chain splits
- **Configuration Validation**: Detects setup errors immediately
- **Reproducibility**: Same config = same genesis hash, guaranteed

## Quick Formula

```
genesis_hash = BLAKE3(
    prev_block_hash (32 bytes, all zeros) ||
    timestamp (8 bytes, uint64_le) ||
    transactions_merkle_root (32 bytes) ||
    height (8 bytes, uint64_le = 0) ||
    consensus_time (8 bytes, uint64_le)
)
```

## Setup Steps

### 1. Configure Genesis Parameters

Edit `config/default.toml`:

```toml
[genesis]
consensus_time = 1704067200000    # Fixed timestamp (ms since epoch)
expected_hash = ""                 # Empty for first run

[genesis.allocations]
"cqc1address1..." = 1000000000    # Initial balances
"cqc1address2..." = 500000000
```

### 2. Generate Hash (First Node)

```bash
./chronos_node --config config/default.toml
```

Look for log output:
```
[CONSENSUS] Genesis block created with hash: 4f8b2a1c9e3d7f6a...
```

### 3. Update Config with Hash

```toml
[genesis]
consensus_time = 1704067200000
expected_hash = "4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c..."  # Copy from logs
```

### 4. Distribute to All Nodes

**Important**: All nodes MUST use the **exact same** configuration file.

## Validation

When a node starts:
- If blockchain is empty → create genesis block
- If `expected_hash` is set → validate against it
- If hash doesn't match → **crash with error** (prevents wrong network)

## Common Errors

### "Genesis block hash mismatch!"

**Cause**: Your config doesn't match the network.

**Solution**:
1. Get the correct `config/default.toml` from network coordinator
2. Delete your local `data/` directory
3. Restart with correct config

### Different Genesis Hash on Each Node

**Cause**: Nodes using different configurations.

**Solution**:
- Ensure all nodes have identical `genesis.consensus_time`
- Ensure all nodes have identical `genesis.allocations`
- Distribute the **same config file** to everyone

## Code Locations

- Genesis block creation: `src/node/node_app.cpp:214-271`
- Hash calculation: `src/ledger/block.cpp:65-84`
- Configuration: `config/default.toml` section `[genesis]`
- Tests: `tests/test_genesis.cpp`

## Full Documentation

See complete documentation with examples:
- **Dutch**: `GENESIS_HASH_EXPLAINED.md`
- **English**: `GENESIS_HASH_EXPLAINED_EN.md`

## Key Takeaway

The genesis hash ensures **all nodes start from the same initial state**. This is the foundation of network consensus and security.
