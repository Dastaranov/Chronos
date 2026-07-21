---
aliases: [Consensus Overzicht, Consensus Engine, Netwerk Overeenstemming]
tags: [chronos, consensus, bft, proof-of-time, index]
creation_date: 2026-07-21
status: in_progress
---

# ⚖️ Consensus Overzicht

Dit is de Map of Content (MoC) voor de consensus-engine van Chronos. De consensuslaag is verantwoordelijk voor het valideren van transacties, het ordenen van blokken en het beschermen van het netwerk tegen kwaadwillende actoren.

## ⚙️ Kerncomponenten

Chronos gebruikt een hybride benadering om schaalbaarheid en veiligheid te garanderen:

1. **Overeenstemming:** Zie [[BFT_Mechanisms]] voor de implementatie van Byzantine Fault Tolerance (hoe we omgaan met valse of haperende nodes).
2. **Tijdsynchronisatie:** Zie [[Proof_of_Time]] voor onze unieke benadering van cryptografisch veilige klokken (NTS en atoomklok-ondersteuning).
3. **Validatie Lifecycle:** Zie [[Transaction_Validation]] voor de reis van een transactie (van de mempool tot definitieve opname in een blok).

## 🔗 Relatie met andere Lagen
De consensuslaag ontvangt berichten via de [[Network_Layer]] en leunt op de cryptografische functies in [[Post_Quantum_Signatures]] om te bewijzen dat transacties authentiek zijn, waarna de geaccepteerde state naar de [[Storage_Layer]] wordt geschreven.

## 📝 Openstaande Taken
- [ ] Flowchart toevoegen van een blok-voorstel (block proposal) tot definitieve goedkeuring (finality).