#!/bin/bash
# =============================================================================
# Chronos Seed Node — Docker Entrypoint
# =============================================================================
# Initialiseert de P2P identiteitssleutel (eenmalig) en start de seed node.
# De sleutel wordt opgeslagen in een persistent volume zodat de node ID
# stabiel blijft bij herstarts.
# =============================================================================
set -e

CHRONOS_HOME="${HOME:-/home/chronos}/.chronos"
KEYS_DIR="$CHRONOS_HOME/keys"
KEY_ID="seed-identity"
CONFIG="${CHRONOS_CONFIG:-/data/chronos/seed.toml}"

# Zorg dat de key directory bestaat (persistent volume)
mkdir -p "$KEYS_DIR"

# Genereer P2P identiteitssleutel als nog niet aanwezig
if [ ! -f "$KEYS_DIR/${KEY_ID}.key" ]; then
    echo "[seed] Genereer P2P identiteitssleutel '${KEY_ID}'..."
    # --yes zorgt voor geen passphrase (geschikt voor Docker zonder TTY)
    wallet_cli generate-keys "$KEY_ID" --yes
    echo "[seed] Sleutel '${KEY_ID}' aangemaakt in ${KEYS_DIR}"
else
    echo "[seed] P2P identiteitssleutel '${KEY_ID}' gevonden."
fi

# Toon het adres van de seed node (handig in logs)
PUB=$(wallet_cli show-public "$KEY_ID" --yes 2>/dev/null || echo "n/a")
echo "[seed] Publieke sleutel: ${PUB}"

echo ""
echo "========================================="
echo "  Chronos Seed Node opstarten"
echo "  Config : ${CONFIG}"
echo "  P2P    : 0.0.0.0:8645"
echo "  Modus  : light (geen validator)"
echo "========================================="
echo ""

exec chronos_node --config "$CONFIG"
