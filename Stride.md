# STRIDE Security Analysis - Chronos Blockchain

This document outlines the security measures implemented in the Chronos blockchain project, following the STRIDE threat model (Spoofing, Tampering, Repudiation, Information Disclosure, Denial of Service, Elevation of Privilege).

## 1. Spoofing (Identity Theft)
**Threat:** An attacker impersonates a validator or a node to inject false data or disrupt consensus.
**Mitigation:**
*   **Public Key Infrastructure (PKI):** All validators are identified by their Ed25519/Dilithium public keys.
*   **Digital Signatures:** Every BFT message (Prevote, Precommit, NewRound) and Transaction is cryptographically signed.
*   **Signature Verification:** Nodes strictly verify signatures against the sender's public key before processing any message. Invalid signatures result in immediate peer penalization.
*   **Node Registry:** A governance-managed registry (`NodeRegistry`) tracks authorized validators. Only registered validators can participate in consensus.

## 2. Tampering (Data Modification)
**Threat:** An attacker modifies blockchain data (blocks, transactions) or network messages in transit.
**Mitigation:**
*   **Cryptographic Hashing:** Blocks and transactions are identified by BLAKE3 hashes. Any modification changes the hash, invalidating the object.
*   **Merkle Trees:** Transactions within a block are committed via a Merkle root.
*   **Chained Headers:** Each block contains the hash of the previous block, creating an immutable chain.
*   **Authenticated Encryption (P2P):** The P2P layer uses Kyber-based KEM and AES-GCM for authenticated encryption of all peer-to-peer traffic, preventing man-in-the-middle attacks and tampering.

## 3. Repudiation (Denial of Action)
**Threat:** A validator performs a malicious action (e.g., double voting) and later denies it.
**Mitigation:**
*   **Signed Messages:** Since all consensus messages are signed, a validator cannot deny having sent a specific message.
*   **Evidence Handling:** The BFT gadget detects equivocation (e.g., two Prevotes for the same round). The conflicting signed messages serve as cryptographic proof of misbehavior.
*   **Slashing:** The `SlashCallback` mechanism allows the network to punish validators (e.g., burn stake) upon providing proof of malfeasance.

## 4. Information Disclosure (Data Leakage)
**Threat:** Sensitive information (e.g., P2P topology, future blocks) is leaked to unauthorized parties.
**Mitigation:**
*   **Encrypted Transport:** All P2P communication is encrypted using post-quantum secure algorithms (Kyber + AES), protecting metadata and content from eavesdroppers.
*   **Private Key Protection:** Private keys are stored in encrypted key files (`KeyManager`) and never exposed in logs or API responses.
*   **Minimal Metadata:** The P2P protocol exposes minimal information about the internal state of the node.

## 5. Denial of Service (DoS)
**Threat:** An attacker floods the network with invalid messages to exhaust resources.
**Mitigation:**
*   **Peer Scoring & Banning:** The `NodeApp` maintains a score for each peer. Sending invalid blocks, bad signatures, or oversized messages reduces the score. Peers dropping below a threshold are disconnected and banned.
*   **Rate Limiting:** (Planned) API and P2P layers implement rate limiting.
*   **Size Limits:** Strict limits on Block size (`MAX_BLOCK_SIZE`), Transaction size (`MAX_TRANSACTION_SIZE`), and Mempool size (`MAX_MEMPOOL_SIZE`) prevent memory exhaustion.
*   **Deterministic Leader Selection:** Prevents leader grinding attacks.

## 6. Elevation of Privilege
**Threat:** An attacker gains unauthorized control over the node or network governance.
**Mitigation:**
*   **Role-Based Access:** The `NodeRegistry` distinguishes between `VALIDATOR`, `OBSERVER`, and `CANDIDATE` roles.
*   **Governance Voting:** Promoting a node to Validator requires a signed `VOTE` transaction from existing validators, ensuring decentralized control.
*   **Admin Separation:** Critical admin functions (staking, voting) are separated from the standard wallet CLI and require access to the validator's private key.

## Status Summary
| Threat Category | Status | Key Implementations |
| :--- | :--- | :--- |
| **S**poofing | ✅ Protected | Dilithium Signatures, NodeRegistry |
| **T**ampering | ✅ Protected | BLAKE3 Hashing, Kyber/AES P2P Encryption |
| **R**epudiation | ✅ Protected | Signed Consensus Messages, Slashing Logic |
| **I**nformation Disclosure | ✅ Protected | Encrypted Key Storage, P2P Encryption |
| **D**enial of Service | ⚠️ In Progress | Peer Scoring (Implemented), Rate Limiting (Partial) |
| **E**levation of Privilege | ✅ Protected | On-chain Governance, Role Enforcement |
