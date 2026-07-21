---
aliases: [API Overzicht, CLI Tools, Interfaces]
tags: [chronos, api, cli, tooling, index]
creation_date: 2026-07-21
status: in_progress
---

# 🔌 API & Command Line Tools

Dit is de Map of Content (MoC) voor alle interfaces van het Chronos-ecosysteem. Omdat Chronos is gebouwd voor efficiëntie en veiligheid, verloopt de primaire interactie via gestructureerde Command Line Interfaces (CLI).

## 🧰 Beschikbare Tools

Het project levert standaard twee gecompileerde binaries op:

1. **De Node Daemon (`chronosd`):** Het hart van de blockchain. Zie [[Node_CLI_Reference]] voor commando's om de node te starten, configureren en monitoren.
2. **De Wallet CLI (`chronos-wallet`):** De tool voor gebruikers. Zie [[Wallet_CLI_Reference]] voor sleutelbeheer (post-kwantum) en het genereren van transacties.

## 📡 Toekomstige RPC/REST API
Momenteel verloopt lokale communicatie met de node via UNIX sockets of directe IPC. 
Er staat op de roadmap om een lichte, lokale REST/JSON-RPC API toe te voegen zodat webinterfaces (zoals block explorers) eenvoudig de state kunnen uitlezen uit de [[Storage_Layer]].

## 📝 Openstaande Taken
- [ ] Documenteren van de JSON-RPC endpoints.
- [ ] Toevoegen van voorbeelden voor geautomatiseerde bash-scripts.