---
aliases: [Opslaglaag, LevelDB, Database, State Storage]
tags: [chronos, architecture, storage, leveldb, protobuf]
creation_date: 2026-07-21
status: draft
---

# 🗄️ Storage Layer (Opslaglaag)

De Storage Layer in Chronos is verantwoordelijk voor het persistent maken van de blockchain state, blocks en mempool-transacties. 

## 🛠️ Technologie-stack

* **Database Engine:** LevelDB. Gekozen vanwege de hoge schrijf- en leessnelheden die vereist zijn voor de node.
* **Serialisatie:** Google Protobuf. Alle on-chain objecten (blocks, transacties) worden geserialiseerd via Protobuf voordat ze naar LevelDB worden geschreven.

## 📂 Datastructuur & Keyspaces

Om efficiënt te kunnen zoeken, maken we gebruik van geprefixte keyspaces in LevelDB:
* `b_` : Blocks (op block hash).
* `h_` : Block heights (voor snelle keten-navigatie).
* `t_` : Transacties (op TXID).
* `s_` : World State (voor saldi en smart contract data).

## 🔄 Interactie met Geheugen
Om de I/O-overhead te minimaliseren, werkt de opslaglaag nauw samen met de caching-mechanismen die beschreven staan in [[Memory_Management]].

## 📝 Openstaande Taken
- [ ] Schema's van de `.proto` bestanden uitschrijven.
- [ ] Database migratie/upgradability paden uitschrijven.