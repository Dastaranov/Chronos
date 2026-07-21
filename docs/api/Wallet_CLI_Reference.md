---
aliases: [Wallet CLI, chronos-wallet, Sleutelbeheer]
tags: [chronos, api, cli, wallet, keys, dilithium, tx]
creation_date: 2026-07-21
status: draft
---

# 👛 Wallet CLI Reference (`chronos-wallet`)

De executable `chronos-wallet` is de interface voor eindgebruikers om sleutelparen te beheren en transacties te signeren. Het leunt zwaar op de cryptografische implementaties beschreven in [[Index_Cryptography]].

## 🔐 Sleutelbeheer

In tegenstelling tot traditionele wallets (secp256k1), genereert deze wallet post-kwantum veilige sleutels:
* `chronos-wallet generate`: Maakt een nieuw wallet-bestand aan. Gebruikt onder de motorkap de CRYSTALS-Dilithium bibliotheek zoals beschreven in [[Post_Quantum_Signatures]].
* `chronos-wallet balance <adres>`: Vraagt het huidige saldo op van een specifiek adres door de lokale node aan te spreken.

## 💸 Transacties

* `chronos-wallet transfer <bestemming> <bedrag>`: Bouwt een transactie, signeert deze met de Dilithium private key, en stuurt de payload naar de node om opgenomen te worden in de mempool.

## ⚠️ Veiligheidswaarschuwing
Wallet-bestanden bevatten zeer gevoelige data. Zorg ervoor dat het bestandssysteem versleuteld is; de CLI biedt de optie om de wallet-bestanden te beveiligen met een sterk wachtwoord en AES-256 encryptie.

## 📝 Openstaande Taken
- [ ] Uitschrijven van de flow voor multisig-transacties met Dilithium.
- [ ] Toevoegen van backup- en herstelinstructies (seed phrases).