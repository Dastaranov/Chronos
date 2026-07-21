---
aliases: [Netwerk Encryptie, P2P Security, Kyber, AES-256-GCM]
tags: [chronos, cryptography, encryption, p2p, kyber, aes]
creation_date: 2026-07-21
status: draft
---

# 🔒 P2P Encryption (Kyber & AES-256-GCM)

Elke verbinding op de [[Network_Layer]] wordt standaard versleuteld. Dit voorkomt afluisteren, metadata-analyse en man-in-the-middle (MITM) aanvallen op het p2p-netwerk.

## 🤝 De Hybride Handshake

Omdat asymmetrische post-kwantum encryptie te zwaar is voor continu dataverkeer, gebruiken we een hybride model:

1. **Key Encapsulation (KEM):** Bij het opzetten van een verbinding gebruiken nodes **CRYSTALS-Kyber** om veilig een gedeeld geheim (shared secret) uit te wisselen.
2. **Symmetrische Tunnel:** Zodra het gedeelde geheim is vastgesteld, schakelt de verbinding over naar **AES-256-GCM**. Dit algoritme is extreem snel dankzij hardware-acceleratie (AES-NI) in moderne CPU's en biedt tevens 'authenticated encryption' (GCM).

## 🔄 Key Rotation
Om zogenaamde 'forward secrecy' te garanderen, wordt de AES-sessiesleutel periodiek (bijv. elk uur of na X megabytes aan data) vernieuwd via een nieuwe Kyber-handshake, zonder dat de netwerkverbinding of verbonden [[BFT_Mechanisms]] daar hinder van ondervinden.

## 📝 Openstaande Taken
- [ ] Implementatiedetails van de Kyber KEM integratie uitschrijven.
- [ ] Sequence-diagram toevoegen van de handshake flow.