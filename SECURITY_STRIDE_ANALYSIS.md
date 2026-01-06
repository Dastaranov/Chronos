# Chronos Security Analysis (STRIDE Model)
**Date:** 2025-12-29
**Version:** 0.1.0

This document provides a security assessment of the Chronos blockchain node using the STRIDE threat model.

## 1. Spoofing (Identity)
**Risk Level: CRITICAL**

*   **Current Status:**
    *   **Node Identity:** Nodes use Post-Quantum Dilithium-3 keys for identity.
    *   **P2P Handshake:** Includes `node_id` and `genesis_hash`.
    *   **Consensus:** BFT messages are signed.
*   **Critical Gaps:**
    *   **Validator PKI Missing:** The `NodeApp::get_validator_public_key` method is a placeholder returning a dummy 1-byte key. Validators currently **cannot verify each other's signatures**. This means any node can spoof BFT messages if they know a validator's ID, and the network will likely crash or halt upon signature verification.
    *   **Action Required:** Implement a mechanism to distribute validator public keys (e.g., via an on-chain Validator Registry or authenticated P2P handshake exchange).

## 2. Tampering (Integrity)
**Risk Level: HIGH**

*   **Current Status:**
    *   **Blockchain:** Blocks are linked via Blake3 hashes.
    *   **Transactions:** Signed and hashed.
*   **Critical Gaps:**
    *   **Private Key Storage:** Private keys are stored in **plaintext** on disk (`~/.chronos/keys/`). If an attacker gains file system access, they can steal keys and impersonate validators.
    *   **Action Required:** Implement encrypted key storage (Keystore) using a passphrase (e.g., AES-256-GCM wrapping).

## 3. Repudiation
**Risk Level: LOW**

*   **Current Status:**
    *   **Non-Repudiation:** All critical actions (transactions, consensus votes) are cryptographically signed.
    *   **Logging:** Extensive logging of all P2P and consensus activities.
*   **Gaps:**
    *   None significant, assuming signature verification is fixed.

## 4. Information Disclosure
**Risk Level: MEDIUM**

*   **Current Status:**
    *   **P2P Encryption:** Kyber-1024 Key Encapsulation + AES-256-GCM is implemented for P2P transport.
*   **Critical Gaps:**
    *   **RPC Interface:** The JSON-RPC server runs over HTTP (plaintext). API tokens or sensitive data (if any) would be exposed.
    *   **Action Required:** Implement TLS for RPC or restrict it to `localhost` by default (currently binds to `0.0.0.0`).

## 5. Denial of Service (DoS)
**Risk Level: HIGH**

*   **Current Status:**
    *   **P2P Limits:** `MAX_MESSAGE_SIZE` and `MAX_BLOCK_SIZE` are enforced.
    *   **Peer Scoring:** Malicious peers are penalized and disconnected.
    *   **IP Limiting:** Basic connection limits implemented.
*   **Critical Gaps:**
    *   **Unbounded Mempool:** The transaction mempool (`std::vector<Transaction>`) has no size limit. An attacker can flood the node with valid transactions, causing OOM (Out of Memory) crashes.
    *   **RPC Rate Limiting:** No rate limiting on RPC endpoints.
    *   **Action Required:** Implement a hard limit on mempool size (e.g., 50,000 txs) and RPC rate limiting.

## 6. Elevation of Privilege
**Risk Level: MEDIUM**

*   **Current Status:**
    *   **Command Separation:** Sensitive admin commands (stop, key management) are isolated in `node_cli` and not exposed via RPC.
*   **Gaps:**
    *   **RPC Authorization:** The RPC interface has no authentication. Any user with network access to the RPC port can submit transactions or query node status.
    *   **Action Required:** Implement API Key authentication or JWT for RPC access if exposed publicly.

## Summary of Required Actions

1.  **Fix Validator PKI (Priority: Critical):** [COMPLETED] Implemented public key storage in `NodeRegistry` and updated `NodeApp` to verify signatures using registered keys.
2.  **Encrypt Keystore (Priority: High):** [COMPLETED] Implemented AES-256-GCM encryption in `KeyManager` with passphrase derivation. Updated `wallet_cli` to support encrypted keys.
3.  **Limit Mempool (Priority: High):** [COMPLETED] Added `MAX_MEMPOOL_SIZE` (50,000) limit in `NodeApp`.
4.  **Secure RPC (Priority: Medium):** [COMPLETED] RPC server now binds to `127.0.0.1` by default. Added `rpc_bind_ip` configuration option.
