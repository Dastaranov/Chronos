---
aliases: [Netwerklaag, P2P, Routing, Peer-to-Peer]
tags: [chronos, architecture, p2p, networking, tcp]
creation_date: 2026-07-21
status: draft
---

# 🌐 Network Layer (Netwerklaag)

De Network Layer beheert de Peer-to-Peer (P2P) verbindingen, het ontdekken van nieuwe nodes (peer discovery) en het efficiënt uitzenden (gossippen) van blokken en transacties.

## 📡 P2P Topologie

Chronos maakt gebruik van een ongestructureerd P2P-netwerk via TCP. 
* **Peer Discovery:** Nodes onderhouden een lokale lijst met bekende, betrouwbare peers (opgeslagen via de [[Storage_Layer]]).
* **Gossip Protocol:** Transacties en blokken worden asynchroon verspreid met minimale duplicatie.

## 🔒 Beveiliging van de Laag

Elke verbinding op de netwerklaag is standaard versleuteld. De netwerklaag roept direct de modules uit [[P2P_Encryption]] aan om de Kyber/AES-256-GCM handshake en encryptie uit te voeren voordat er blockchain-specifieke data over de lijn gaat.

## 📬 Message Types
Het protocol kent verschillende interne message types:
* `PING`/`PONG` (Keep-alive)
* `TX_BROADCAST` (Nieuwe transacties voor de mempool)
* `BLOCK_PROPOSE` (Nieuw blok, triggert [[BFT_Mechanisms]])
* `SYNC_REQUEST` (Voor nodes die achterlopen)