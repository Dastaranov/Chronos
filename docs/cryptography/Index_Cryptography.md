---
aliases: [Cryptografie Overzicht, Security, Post-Quantum, PQ]
tags: [chronos, cryptography, post-quantum, security, index]
creation_date: 2026-07-21
status: in_progress
---

# 🔐 Cryptografie Overzicht

Dit is de Map of Content (MoC) voor de cryptografische fundering van Chronos. Waar veel bestaande blockchains nog leunen op elliptische curven (zoals secp256k1) die kwetsbaar zijn voor toekomstige kwantumcomputers, is Chronos vanaf de basis ontworpen met Post-Quantum (PQ) cryptografie.

## 🛡️ Kernimplementaties

De cryptografie is opgedeeld in twee hoofddoelen:

1. **Authenticatie (Transacties):** Zie [[Post_Quantum_Signatures]] voor de implementatie van CRYSTALS-Dilithium, gebruikt voor het genereren van wallets en het ondertekenen van data.
2. **Vertrouwelijkheid (Netwerk):** Zie [[P2P_Encryption]] voor de hybride Kyber / AES-256-GCM opzet die het node-to-node verkeer beveiligt.

## 🔗 Relatie met de Architectuur
De cryptografische modules zijn sterk ontkoppeld gehouden. De [[Network_Layer]] roept de encryptie-functies aan, terwijl de [[Transaction_Validation]] in de consensuslaag leunt op de Dilithium-handtekening verificatie.

## 📝 Openstaande Taken
- [ ] Benchmarks toevoegen: CPU-load van Dilithium validatie ten opzichte van klassieke ECDSA.
- [ ] Documenteren van de random number generation (RNG) in C++20 voor de sleutelgeneratie.