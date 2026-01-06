# Chronos Blockchain - User Manual

## Introduction
Welcome to Chronos, a post-quantum secure blockchain designed for high-speed financial transactions and robust decentralized consensus. This manual guides you through setting up a wallet, running a node, and participating in the network.

## Quickstart

For users who want to get up and running quickly, we provide a `quickstart.sh` script located in the root directory. This script automates the setup process:

1.  **Detects Public IP:** Checks for both IPv4 and IPv6 connectivity and lets you choose which one to use.
2.  **Checks Ports:** Verifies if the required P2P port (default 8645) is free on your machine.
3.  **Generates Keys:** Creates a secure key pair for your node if one doesn't exist.
4.  **Configures Node:** Creates a `config.toml` tailored to your environment.
5.  **Staking Assistance:** Guides you through funding your address and running the stake command.
6.  **Starts Node:** Launches the node immediately.

To use it:
```bash
./quickstart.sh
```

## 1. Installation

### Prerequisites
- Linux (Ubuntu 22.04+ recommended)
- Internet connection

### Building from Source
Currently, Chronos is distributed as source code. You need to compile it:

```bash
# Install dependencies
sudo apt update
sudo apt install build-essential cmake libssl-dev libgmp-dev libsodium-dev nlohmann-json3-dev libsnappy-dev libleveldb-dev curl netcat-openbsd

# Clone repository
git clone https://github.com/chronos-org/chronos.git
cd chronos

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Network Connectivity & Dynamic IPs

**Public IP Address:**
Your node needs to be reachable by other peers. The `quickstart.sh` script attempts to detect your public IP.

**Dynamic vs. Static IP:**
*   **Regular Nodes / Wallets:** A dynamic IP (standard for home connections) is perfectly fine. The network's peer discovery protocol handles IP changes automatically.
*   **Validators (Staking Nodes):** While the software supports dynamic IPs, a **Static IP** is strongly recommended.
    *   *Why?* If your IP changes, your node effectively "moves" in the network. It takes time for other validators to discover your new location. During this window, you might miss consensus votes, lowering your `uptime_score` and potential rewards.
    *   *Recommendation:* Run validators on a VPS (Virtual Private Server) or a connection with a static IP.

## 2. Wallet Usage (`wallet_cli`)
The wallet is your interface for managing funds and keys.

### Creating a Wallet
```bash
./wallet_cli generate-keys my-wallet
```
This creates `my-wallet.pub` (public key) and `my-wallet.priv` (private key) in `~/.chronos/keys/`.
**IMPORTANT:** Back up your `.priv` file!

### Checking Balance
```bash
./wallet_cli get-balance <your-address>
```

### Sending Transactions
```bash
./wallet_cli send <key-id> <recipient-address> <amount>
```

### Staking (For Validators)
To become a validator, you must stake coins:
```bash
./wallet_cli stake <key-id> <amount> [time-tier] [name]
```

### Unstaking
```bash
./wallet_cli unstake <key-id> <amount>
```

### Importing Keys
You can import an existing key pair (hex format):
```bash
./wallet_cli import-key <key-id> <private-key-hex> <public-key-hex>
```

## 3. Running a Node (`chronos_node`)

### Configuration
The default configuration is in `config/default.toml`. You can modify it to set your node type (full/light), network ports, and validator keys.

### Starting the Node
```bash
./chronos_node --config config/default.toml
```

### Node Administration (`node_cli`)
Use the `node_cli` tool to interact with a running node.

#### Checking Status
```bash
./node_cli status
```

#### Viewing Peers
```bash
./node_cli peers
```

#### Viewing Mempool
```bash
./node_cli mempool
```

#### Generating API Key
To secure your RPC interface, generate an API key:
```bash
./node_cli generate-api-key
```
Add the output to your `config.toml` under the `[rpc]` section.

#### Voting on Proposals
If you are a validator, you can vote on network proposals (e.g., adding new validators):
```bash
./node_cli vote <proposal_id> <approve|reject>
```

### Time Synchronization (Crucial)

Chronos uses a **Proof-of-Time (PoT)** consensus mechanism. This means your node's clock accuracy directly impacts your ability to participate in the network and earn rewards.

#### Why Chrony?
We use **Chrony** as the backend for time synchronization because it is more accurate and robust than standard NTP implementations. It supports NTS (Network Time Security) to prevent time spoofing attacks.

#### Time Tiers
Your node is assigned a "Time Tier" based on the accuracy and security of your clock:
*   **Tier 1 (Atomic/Quantum):** Direct hardware link to atomic clock (Future support). Highest rewards.
*   **Tier 2 (GPS/PTP):** High-precision hardware source.
*   **Tier 3 (Authenticated NTS):** Chrony with NTS enabled. Standard for secure validators.
*   **Tier 4 (Standard NTP):** Standard Chrony/NTP. Minimum requirement for validators.
*   **Tier 5 (Unsynchronized):** Clock is drifting or unsynced. **Validators in Tier 5 cannot propose blocks.**

#### Checking Your Time Status
To ensure your node is synchronized:
1.  Check Chrony status:
    ```bash
    chronyc tracking
    ```
    Look for "System time" offset. It should be very small (e.g., < 0.001 seconds).
2.  Check sources:
    ```bash
    chronyc sources -v
    ```
    Ensure you have reachable servers (marked with `*`).

**Note:** If your Chrony service fails or your clock drifts significantly, your node will automatically downgrade to Tier 5. If this persists (e.g., > 24 hours), you may be slashed or removed from the validator set.

## 4. Network Setup & Genesis

### The Genesis Block
The Genesis Block is the very first block in the blockchain (height 0). It defines the initial state of the network, including the initial token distribution and the initial set of validators.

**Crucial Concept:** For a node to join a specific Chronos network (e.g., Mainnet, Testnet), it **must** be initialized with the exact same Genesis Block as all other nodes. If you generate a new Genesis Block, you are effectively creating a new, separate network that cannot communicate with the existing one.

### For Network Creators (Superusers)
If you are launching a *new* network (e.g., a private testnet), you need to generate the Genesis Block once.

1.  **Generate Genesis Data:**
    Use the `genesis_tool` to create the initial configuration.
    ```bash
    ./build/tools/genesis_tool --output config/genesis.json --supply 1000000000 --address <your_wallet_address>
    ```
    *   `--supply`: Total initial supply of tokens (in whole units).
    *   `--address`: The wallet address that will receive this initial supply.

2.  **Distribute `genesis.json`:**
    You must share this `config/genesis.json` file with anyone who wants to join your network.

3.  **Start the First Node:**
    Configure your node to use this genesis file.

### For Network Participants (Users/Validators)
If you are joining an existing network (like the official Chronos Mainnet):

1.  **Do NOT run `genesis_tool`.**
2.  **Obtain the Official Genesis File:**
    Download the `genesis.json` provided by the network administrators (or it may be bundled with the release).
3.  **Configure Your Node:**
    Ensure your `config/default.toml` (or custom config) points to this file:
    ```toml
    [consensus]
    genesis_file = "config/genesis.json"
    ```
4.  **Start Your Node:**
    When your node starts, it will load the genesis block. During the handshake process with other peers, it will verify that your Genesis Hash matches theirs. If it doesn't, the connection will be rejected to prevent network forks.

## Tokenomics & Initial Supply

Since only the Genesis Block can create the initial supply of tokens:
*   **Initial Distribution:** The "superuser" (creator) controls the initial allocation via the address specified in `genesis_tool`.
*   **Circulation:** Users acquire tokens by receiving them from the initial holder (e.g., via an ICO, airdrop, or exchange) or by earning transaction fees/block rewards as validators (if inflation is enabled).
*   **Security:** It is impossible for a new node to "print" more money by creating a fake genesis block, because that node would simply be rejected by the rest of the network.

## 5. Security Best Practices
- **Keys:** Never share your private keys. Store them securely.
- **Network:** Ensure your node is behind a firewall, allowing only necessary ports (default 9000 for P2P, 8080 for RPC).
- **Updates:** Keep your node software up to date to ensure network compatibility and security.

## 6. FAQ
**Q: How do I get testnet coins?**
A: Use the genesis key provided in the dev environment or ask a faucet (if available).

**Q: My node isn't syncing.**
A: Check your internet connection and ensure port 9000 is open. Verify you have valid seed nodes in your config.
