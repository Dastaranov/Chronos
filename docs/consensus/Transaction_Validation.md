---
aliases: [Tx Validatie, Transactie Lifecycle, Mempool]
tags: [chronos, consensus, validation, mempool, double-spend]
creation_date: 2026-07-21
status: draft
---

# ✅ Transaction Validation

Voordat een transactie in een blok terechtkomt of zelfs maar wordt doorgegeven aan andere peers, doorloopt deze een strikte validatie-flow.

## 🛤️ Lifecycle van een Transactie

1. **Ontvangst:** Een transactie komt binnen via de API of via de [[Network_Layer]].
2. **Structurele Validatie:** Is de Protobuf-envelop correct gevormd?
3. **Cryptografische Validatie:** Het netwerk controleert de CRYSTALS-Dilithium handtekening (zie [[Post_Quantum_Signatures]]). Als deze onjuist is, wordt de verbinding met de peer verbroken.
4. **State Validatie (Double-Spend check):** De node raadpleegt de [[Storage_Layer]] (de 'World State') om te controleren of:
   - De afzender voldoende saldo heeft.
   - De 'nonce' (volgnummer) correct is, om replay-attacks te voorkomen.
5. **Mempool:** Als de transactie slaagt, wordt deze aan de lokale mempool toegevoegd en gegossipt naar andere nodes.

## 🧹 Mempool Beheer
De mempool gebruikt de caching-mechanismen uit [[Memory_Management]] om snel te bepalen welke transacties prioriteit krijgen voor het volgende blok.

## 📝 Openstaande Taken
- [ ] Kosten/Fee-model (Tokenomics 2.0) integreren in de state validatie.
- [ ] Code-voorbeelden toevoegen van de C++ `ValidateTx()` functie.