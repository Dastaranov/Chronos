---
aliases: [PQ Signatures, Dilithium, Transactie Handtekeningen, Wallet Keys]
tags: [chronos, cryptography, signatures, dilithium, post-quantum]
creation_date: 2026-07-21
status: draft
---

# 🖋️ Post-Quantum Signatures (CRYSTALS-Dilithium)

Voor digitale handtekeningen gebruikt Chronos **CRYSTALS-Dilithium**, een van de primaire algoritmes geselecteerd door NIST voor post-kwantum standaardisatie. Dit algoritme is gebaseerd op 'lattice-based cryptography' (roosters).

## 🔑 Sleutelbeheer

* **Public Keys:** Aanzienlijk groter dan traditionele keys (enkele kilobytes in plaats van bytes). Dit heeft impact op de block size limieten gedefinieerd in de [[Storage_Layer]].
* **Private Keys:** Worden gegenereerd en beheerd via de [[Wallet_CLI_Reference]].

## ✅ Validatieproces
Wanneer een transactie wordt voorgesteld, controleert de node de handtekening over de hash van de transactie-payload. Dit proces is razendsnel in verificatie, wat essentieel is voor de doorvoer tijdens de [[Transaction_Validation]] fase.

## 📝 Openstaande Taken
- [ ] Protobuf-schema updaten om de grotere Dilithium-handtekeningen optimaal te comprimeren.
- [ ] Code snippets toevoegen van de `Sign()` en `Verify()` wrappers rond de C++ cryptografie library.