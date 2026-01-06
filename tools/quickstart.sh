#!/bin/bash

# Chronos Quickstart Script
# This script helps you set up and start a Chronos node quickly.

set -e

echo "=================================================="
echo "       Chronos Blockchain - Quickstart Setup      "
echo "=================================================="
echo ""

# 1. Detect Public IP Addresses
echo "[*] Detecting public IP addresses..."

IPV4=$(curl -s -4 ifconfig.me || echo "")
IPV6=$(curl -s -6 ifconfig.me || echo "")

if [ -z "$IPV4" ] && [ -z "$IPV6" ]; then
    echo "[-] Could not detect any public IP address. Please check your internet connection."
    # Fallback to local loopback if no internet, but warn user
    echo "[!] WARNING: No internet connection detected. Node will only be accessible locally."
    IPV4="127.0.0.1"
fi

SELECTED_IP=""

if [ ! -z "$IPV4" ] && [ ! -z "$IPV6" ]; then
    echo "[+] Detected IPv4: $IPV4"
    echo "[+] Detected IPv6: $IPV6"
    echo ""
    echo "Which IP address would you like to use for your node?"
    echo "1) IPv4 ($IPV4) - Recommended for compatibility"
    echo "2) IPv6 ($IPV6)"
    read -p "Select an option [1]: " IP_CHOICE
    IP_CHOICE=${IP_CHOICE:-1}

    if [ "$IP_CHOICE" == "1" ]; then
        SELECTED_IP=$IPV4
    else
        SELECTED_IP=$IPV6
    fi
elif [ ! -z "$IPV4" ]; then
    echo "[+] Detected IPv4: $IPV4"
    SELECTED_IP=$IPV4
else
    echo "[+] Detected IPv6: $IPV6"
    SELECTED_IP=$IPV6
fi

echo "[*] Using Public IP: $SELECTED_IP"
echo ""

# Dynamic IP Warning
echo "----------------------------------------------------------------"
echo "IMPORTANT NOTE ON DYNAMIC IPs"
echo "----------------------------------------------------------------"
echo "Most home internet connections have a Dynamic IP (it changes periodically)."
echo "For a standard node or wallet, this is fine."
echo ""
echo "If you intend to be a VALIDATOR (Staking Node):"
echo "1. A Static IP is highly recommended for maximum uptime and rewards."
echo "2. If your IP changes, your node will automatically reconnect, but you"
echo "   might miss consensus rounds (and rewards) during the switch."
echo "3. Consider using a VPS (Virtual Private Server) or asking your ISP for a static IP."
echo "----------------------------------------------------------------"
echo ""

# 2. Check Ports
PORT=8645
echo "[*] Checking if port $PORT is open..."

# Simple check using timeout and bash tcp (for v4) or nc if available
if command -v nc >/dev/null 2>&1; then
    # Try to listen on the port to see if it's free locally
    if nc -z -v -w5 127.0.0.1 $PORT 2>/dev/null; then
        echo "[!] Port $PORT seems to be in use locally. Is a node already running?"
    else
        echo "[+] Port $PORT is free locally."
    fi
else
    echo "[!] 'nc' (netcat) not found. Skipping local port check."
fi

echo "[!] IMPORTANT: Ensure your firewall/router forwards port $PORT (TCP) to this machine."
echo ""

# 3. Time Synchronization Check (Crucial for PoT)
echo "[*] Checking Time Synchronization (Chrony)..."
if command -v chronyc >/dev/null 2>&1; then
    if chronyc tracking >/dev/null 2>&1; then
        # Extract offset (this is a rough check, parsing output might vary by version)
        OFFSET=$(chronyc tracking | grep "System time" | awk '{print $4}')
        echo "[+] Chrony is running. System time offset: $OFFSET seconds"
        
        # Simple check if offset is "small enough" (e.g. not starting with multiple digits before dot)
        # Ideally we'd parse float, but for bash quickstart, existence is the main check.
    else
        echo "[!] WARNING: Chrony is installed but does not seem to be synchronized."
        echo "    Run 'chronyc sources -v' to debug."
    fi
else
    echo "[!] WARNING: 'chronyc' not found."
    echo "    Chronos relies on Proof-of-Time. It is HIGHLY recommended to install and configure Chrony."
    echo "    Ubuntu/Debian: sudo apt install chrony"
fi
echo ""

# 4. Configuration
CONFIG_FILE="config/node_config.toml"
if [ ! -f "$CONFIG_FILE" ]; then
    echo "[*] Creating configuration file from default..."
    mkdir -p config
    cp config/default.toml $CONFIG_FILE
    
    # Update listen_addr based on IP version preference (0.0.0.0 for v4, [::] for v6 usually, but config takes string)
    # For simplicity, we bind to all interfaces. The public IP is mainly for advertising (if we had that setting).
    # Currently config only has listen_addr.
    
    # If user selected IPv6, we might want to listen on [::]
    if [[ "$SELECTED_IP" == *":"* ]]; then
        sed -i 's/listen_addr = "0.0.0.0"/listen_addr = "::"/' $CONFIG_FILE
    fi
    
    echo "[+] Configuration created at $CONFIG_FILE"
else
    echo "[*] Using existing configuration at $CONFIG_FILE"
fi

echo ""

# 4. Key Generation
echo "[*] Checking for node keys..."
KEY_DIR="$HOME/.chronos/keys"
if [ ! -d "$KEY_DIR" ] || [ -z "$(ls -A $KEY_DIR)" ]; then
    echo "[*] No keys found. Generating new node key..."
    mkdir -p build
    if [ ! -f "./build/wallet_cli" ]; then
        echo "[-] wallet_cli not found. Please build the project first."
        exit 1
    fi
    
    read -p "Enter a name for your key (e.g., my-node-key): " KEY_NAME
    KEY_NAME=${KEY_NAME:-my-node-key}
    
    ./build/wallet_cli generate-keys "$KEY_NAME"
    
    # Extract public key to show user
    PUB_KEY=$(./build/wallet_cli show-public "$KEY_NAME")
    echo "[+] Key generated!"
    echo "    Key ID: $KEY_NAME"
    echo "    Public Key: $PUB_KEY"
    
    # Update config with key ID
    sed -i "s/private_key_id = .*/private_key_id = \"$KEY_NAME\"/" $CONFIG_FILE
else
    echo "[*] Keys found in $KEY_DIR."
    echo "    Make sure your config points to the correct key_id."
fi

echo ""

# 5. Staking Guidance
echo "=================================================="
echo "       Staking Your Node                          "
echo "=================================================="
echo "To participate in consensus, your node must be staked."
echo "Minimum stake required: 1,000,000 nanos (0.001 CHRONOS)"
echo ""
echo "1. Fund your address: $PUB_KEY"
echo "2. Once funded, run the following command to stake:"
echo ""
echo "   ./build/wallet_cli stake --amount 1000000 --node-address $PUB_KEY"
echo ""
echo "=================================================="

# 6. Start Node
read -p "Do you want to start the node now? (y/n) [y]: " START_NODE
START_NODE=${START_NODE:-y}

if [ "$START_NODE" == "y" ]; then
    echo "[*] Starting Chronos Node..."
    ./build/chronos_node --config $CONFIG_FILE
fi
