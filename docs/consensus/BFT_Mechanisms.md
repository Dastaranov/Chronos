---
aliases: [BFT, Byzantine Fault Tolerance, Node Overeenstemming]
tags: [chronos, consensus, bft, validators, security]
creation_date: 2026-07-21
status: draft
---

# 🛡️ BFT Mechanisms (Byzantine Fault Tolerance)

Chronos bereikt netwerkconsensus via een geoptimaliseerd Byzantine Fault Tolerance (BFT) protocol. Dit zorgt ervoor dat het netwerk correct blijft functioneren, zelfs als tot 1/3e van de nodes offline gaat of kwaadaardige data probeert te verspreiden.

## 🔄 Blok-productie Cyclus (View Change)

1. **Proposer Selectie:** Bepaald door een deterministisch algoritme gekoppeld aan [[Proof_of_Time]].
2. **Pre-Prepare & Prepare:** De proposer broadcast een nieuw blok. Nodes valideren het blok en sturen een prepare-stem naar elkaar via de [[Network_Layer]].
3. **Commit & Finality:** Zodra een node een 2/3 meerderheid aan cryptografisch ondertekende prepare-stemmen ontvangt, wordt het blok definitief weggeschreven naar de [[Storage_Layer]].

## 🚫 Isolatie van Bad Actors
Nodes die ongeldige handtekeningen sturen (falende verificatie via [[Post_Quantum_Signatures]]) of proberen de tijd te manipuleren, worden door het netwerk gedropt en komen op een blacklist.

## 📝 Openstaande Taken
- [ ] De exacte timeout-waarden documenteren voor het triggeren van een 'view change' (als een leider faalt).
- [ ] Implementatiedetails voor de Node Politics & Governance (consensus approval flow voor nieuwe nodes) toevoegen.