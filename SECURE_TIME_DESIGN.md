# Secure Time Synchronization Design

## 1. Vision
Chronos aims to be the most time-accurate blockchain in existence. To achieve this, we are building a **Time Source Plugin Architecture** that supports a hierarchy of time sources, ranging from standard NTP to state-of-the-art Quantum Clocks.

The accuracy and security of a node's time source directly impact its **Time Quality Score**, which influences its reputation and weight in the Proof-of-Time (PoT) consensus.

## 2. Architecture: The "Time Socket"

We define a universal interface (the "socket") that any time source must implement. This allows future hardware (e.g., university quantum clocks) to be "plugged in" without rewriting the core consensus logic.

### 2.1 The Interface (`ITimeSource`)

```cpp
struct TimeSample {
    uint64_t timestamp_nanos;
    double error_margin_ns;
    double drift_ppm;
    bool is_authenticated;
    std::string signature; // For hardware signing if available
};

enum class TimeSourceType {
    QUANTUM_CLOCK = 5,      // Tier 1: Future-tech (e.g., Optical Lattice)
    ATOMIC_CLOCK = 4,       // Tier 2: Cesium/Rubidium hardware attached via PPS/Serial
    GNSS_SECURE = 3,        // Tier 3: GPS/Galileo with anti-spoofing (OSNMA)
    NTS_NETWORK = 2,        // Tier 4: Authenticated Network Time (Chrony/NTS)
    NTP_INSECURE = 1,       // Tier 5: Standard NTP (fallback)
    LOCAL_SYSTEM = 0        // Tier 6: Unreliable local clock
};

class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    
    // The core "reading" method
    virtual TimeSample get_sample() = 0;
    
    // Metadata for scoring
    virtual TimeSourceType get_type() const = 0;
    virtual std::string get_device_id() const = 0;
    virtual double get_confidence_score() const = 0; // 0.0 to 1.0
};
```

### 2.2 The Manager (`TimeSourceManager`)
The manager holds a collection of active "plugs" (implementations of `ITimeSource`).
- It queries all sources periodically.
- It filters outliers using MAD (Median Absolute Deviation).
- It calculates a weighted average based on `TimeSourceType`.

## 3. Supported Implementations

### 3.1 Phase 1: Network Time Security (NTS)
*   **Implementation**: `ChronyBackend`
*   **Type**: `NTS_NETWORK`
*   **Mechanism**: Wraps the `chronyd` daemon. Parses `chronyc -c authdata` to verify NTS cookies.
*   **Security**: Protects against Man-in-the-Middle (MitM) attacks on NTP packets.

### 3.2 Phase 2: Hardware Atomic Clocks
*   **Implementation**: `AtomicClockSerialBackend`
*   **Type**: `ATOMIC_CLOCK`
*   **Mechanism**: Reads directly from a hardware device (e.g., Microsemi SA.45s CSAC) via RS-232/USB and PPS (Pulse Per Second) GPIO pin.
*   **Security**: Physical hardware connection. Extremely low drift.

### 3.3 Phase 3: Quantum Clocks (Future Proofing)
*   **Implementation**: `QuantumInterfaceBackend`
*   **Type**: `QUANTUM_CLOCK`
*   **Mechanism**: Reserved for future optical lattice clocks or entangled time sources.
*   **Interface**: Likely a dedicated PCIe card or quantum-link interface. The `ITimeSource` abstraction ensures we are ready.

## 4. Time Quality Score & Reputation

A node's influence in the network is partially derived from its **Time Quality Score**.

### 4.1 Calculation
```cpp
double calculate_time_score(const std::vector<ITimeSource*>& sources) {
    double max_score = 0.0;
    for (auto* src : sources) {
        double base_score = static_cast<double>(src->get_type()) * 20.0; // 0-100 scale
        max_score = std::max(max_score, base_score);
    }
    return max_score;
}
```

### 4.2 Network Effect
- **Gossip**: Nodes broadcast their `TimeQualityScore` in the `Handshake` and `NodeStatus` messages.
- **Consensus**:
    - Nodes with `ATOMIC` or `QUANTUM` scores are preferred as **Time Leaders** for PoT aggregation.
    - Their timestamps are given higher weight (e.g., 5x) in the global median calculation.
- **Incentives**: Higher time scores = higher staking rewards (future implementation).

## 5. Configuration (`chronos.toml`)

```toml
[time]
# Primary source configuration
enable_nts = true
nts_servers = ["time.cloudflare.com", "nts.netnod.se"]

# Hardware Clock "Socket" Configuration
[time.hardware]
enabled = false
type = "atomic_serial" # or "quantum_link", "gnss_pps"
device_path = "/dev/ttyUSB0"
pps_pin = 18
```

## 6. Security Considerations
- **Hardware Spoofing**: A node could lie about having an atomic clock.
    - *Mitigation*: Future versions will require **Hardware Attestation** (TPM/Secure Enclave) where the clock device signs the timestamp with a burned-in private key.
- **Sybil Attacks**: NTS prevents network spoofing, but not malicious nodes. The `PoTAggregator` uses statistical filtering (MAD) to ignore nodes that deviate too far from the weighted median, even if they claim to have a Quantum Clock.

## 7. Verification & Trust Model (Anti-Spoofing)

The biggest risk is a node lying about its time source (e.g., claiming an Atomic Clock while using NTP). We solve this with a **Challenge-Response & Reputation** model:

### 7.1 Cryptographic Attestation (Hardware)
*   **Tier 1/2 (Quantum/Atomic)**: Requires a hardware module (HSM/TPM) that provides cryptographically signed "ticks" or status reports. The driver sends this signature along with the timestamp.
*   **Future**: Integration with specific hardware vendors to bake keys into the clock hardware itself.

### 7.2 Statistical Verification (Network Consensus)
*   The network "measures" a node's quality. A node claiming Tier 1 must exhibit extremely low jitter and drift relative to the network average.
*   **Penalty System**: If a "Tier 1" node deviates outside the tight tolerances of Tier 1 (e.g., >1ms deviation), its `trust_score` is penalized much more heavily than a Tier 4 node.
*   **Principle**: "High claims require high proof."

### 7.3 Randomized Audits (Spot Checks)
*   Other Tier 1/2 nodes can send random "ping-pong" challenges to measure latency and processing time. Real hardware clocks respond more deterministically than software emulations.

### 7.4 Stake-Locking
*   To claim Tier 1/2 status in the `NodeRegistry`, a node must lock a significantly higher **stake**. Upon detection of fraud (spoofing), this stake is fully slashed.
