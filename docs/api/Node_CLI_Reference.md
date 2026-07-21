---
aliases: [Node CLI, chronosd, Node Commando's]
tags: [chronos, api, cli, node, daemon]
creation_date: 2026-07-21
status: draft
---

# 🖥️ Node CLI Reference (`chronosd`)

De executable `chronosd` (Chronos Daemon) is verantwoordelijk voor het draaien van de node, het synchroniseren van de blockchain en het deelnemen aan [[BFT_Mechanisms]].

## 🚀 Basis Commando's

* `chronosd start`: Start de node op de achtergrond. Optioneel: `--config <pad>` om een specifieke configuratie in te laden.
* `chronosd stop`: Sluit de node veilig af, triggert de shutdown-sequence en sluit de [[Storage_Layer]] (LevelDB) netjes om corruptie te voorkomen.
* `chronosd status`: Toont de huidige block height, het aantal verbonden peers en de uptime.

## 🌐 Netwerkbeheer

Omdat het netwerk de protocollen uit de [[Network_Layer]] gebruikt, zijn er specifieke commando's voor peer management:
* `chronosd peer add <ip>:<port>`: Voeg handmatig een betrouwbare peer toe (handig bij bootstrap/genesis).
* `chronosd peer list`: Toont een lijst van alle actieve en versleutelde verbindingen via [[P2P_Encryption]].

## 📝 Openstaande Taken
- [ ] Tabel toevoegen met alle mogelijke opstart-vlaggen (flags).
- [ ] Documenteren hoe logging verbosity (`--log-level`) werkt.