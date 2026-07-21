---
aliases: [Proof of Time, PoT, NTS, Tijdsynchronisatie]
tags: [chronos, consensus, time, nts, atomic-clock]
creation_date: 2026-07-21
status: active
---

# ⏱️ Proof of Time (Tijdsynchronisatie)

In veel BFT-systemen is de volgorde van gebeurtenissen een zwakke plek. Chronos lost dit op via een cryptografisch beveiligde Proof-of-Time architectuur.

## 🕰️ Gelaagde Tijd-architectuur

Om te voorkomen dat nodes worden misleid met valse timestamps, gebruiken we een gelaagd systeem:

1. **Network Time Security (NTS):** Chronos-nodes gebruiken NTS om tijdstempels cryptografisch te verifiëren, wat beschermt tegen NTP-spoofing en MITM-aanvallen.
2. **Hardware Integratie (Geavanceerd):** Voor tier-1 validators is de software voorbereid op directe integratie met atoomklokken (of in de toekomst kwantumklokken). Dit minimaliseert 'clock drift' tot vrijwel nul.

## 🔗 Integratie met BFT
De gevalideerde netwerktijd wordt gebruikt als een strakke constraint in de [[BFT_Mechanisms]]. Als de timestamp van een voorgesteld blok te ver afwijkt van de lokale Proof-of-Time (rekening houdend met netwerk-latency), wordt het blok onmiddellijk afgewezen.

## 🧮 Wiskundig Bewijs voor Veilige Tijd

Om te bewijzen dat het netwerk veilig is tegen tijdsmanipulatie, tonen we aan dat een leugenachtige *Proposer* altijd gedetecteerd wordt zonder dat legitieme netwerkvertragingen leiden tot onterechte afwijzingen.

### 1. Definities
* $t$: De absolute, ware fysieke tijd.
* $C_i(t)$: De uitgelezen klokwaarde van node $i$ op de ware tijd $t$.
* $\epsilon$: De maximale onzekerheidsmarge (drift) van de atoomklok/NTS.
* $\delta$: De netwerkvertraging (latency) voor blok-overdracht, waarbij $\delta \ge \delta_{min} > 0$.

### 2. Axioma (Begrensde Onzekerheid)
Door de hardware-integratie begrenzen we de afwijking ten opzichte van de ware tijd strikt:
$$|C_i(t) - t| \le \epsilon \quad \forall i, \forall t$$

### 3. De Stelling
Een *Proposer* ($p$) creëert een blok op ware tijd $t_1$ met tijdstempel $T_p = C_p(t_1)$. 
Een *Validator* ($v$) ontvangt dit op ware tijd $t_2 = t_1 + \delta$ en leest eigen klok $T_v = C_v(t_2)$.

Een blok is uitsluitend temporeel valide indien:
$$T_p < T_v + 2\epsilon - \delta_{min}$$

### 4. Bewijsvoering
De marges voor een eerlijke proposer zijn:
$$t_1 - \epsilon \le T_p \le t_1 + \epsilon$$

Voor de validator op $t_2$:
$$(t_1 + \delta) - \epsilon \le T_v \le (t_1 + \delta) + \epsilon$$

Het maximale verschil tussen waarnemer en proposer is:
$$T_v - T_p = (t_1 + \delta \pm \epsilon) - (t_1 \pm \epsilon) = \delta \pm 2\epsilon$$

Aangezien $\delta \ge \delta_{min}$, is het waargenomen verschil minimaal:
$$T_v - T_p \ge \delta_{min} - 2\epsilon$$

Herschreven voor de validatie-logica in de applicatie:
$$T_p \le T_v + 2\epsilon - \delta_{min}$$

**Conclusie:** Een kwaadwillende node die een tijdstempel in de toekomst forceert ($T_{fake} > t_1 + \epsilon$) overschrijdt onvermijdelijk de grens $T_p \le T_v + 2\epsilon - \delta_{min}$. Het blok wordt direct op de netwerklaag verworpen. $\blacksquare$

> **Architecturale impact:** Met atoomklokken nadert $\epsilon$ tot nul. Hierdoor convergeert de check naar $T_p \le T_v - \delta_{min}$, waardoor we blokken op de maximale fysieke netwerksnelheid kunnen verifiëren zonder kunstmatige en vertragende 'sleep()' checks.

## 🛠️ Technische Vertaling naar C++

De formule is nu direct vertaald naar code in de PoT-validatiepipeline.

### 1. Formule -> API
De wiskundige check
$$T_p \le T_v + 2\epsilon - \delta_{min}$$
is geimplementeerd als statische helper:

- `chrono_consensus::PoTAggregator::validate_timestamp(proposer_timestamp_ms, local_consensus_time_ms, epsilon_ms, delta_min_ms)`

Deze functie berekent de rechterkant van de ongelijkheid met signed arithmetic om underflow te vermijden bij kleine lokale tijd en relatief grote `delta_min`.

### 2. Symbolen -> Runtime variabelen
- $T_p$  -> `b.consensus_time` uit het voorgestelde block.
- $T_v$  -> `pot_.get_consensus_time()` op de validator.
- $\epsilon$ -> `cfg_.pot_epsilon_ms` (fallback op `secure_time.max_skew_ms` als niet expliciet gezet).
- $\delta_{min}$ -> `cfg_.pot_delta_min_ms`.

### 3. Integratiepunt
De check draait tijdens block-acceptatie in `NodeApp::add_block(...)` voor alle niet-genesis blocks (`height > 0`).

Gedrag:
- Indien de ongelijkheid faalt: block wordt verworpen.
- Indien lokale consensus tijd niet beschikbaar is: block wordt verworpen (fail-closed voor tijdveiligheid).

### 4. Configuratie
Nieuwe consensus-parameters:

```toml
[consensus]
pot_epsilon_ms = 1000
pot_delta_min_ms = 1
```

`pot_delta_min_ms` moet strikt groter zijn dan nul.

### 5. Testdekking
Unit tests verifiëren nu expliciet:
- acceptatie op de grenswaarde (`T_p == T_v + 2\epsilon - \delta_{min}`),
- reject boven de grens,
- reject als de rechterkant negatief zou worden.

## 📝 Openstaande Taken
- [ ] Setup-instructies schrijven voor de NTS-daemon.
- [x] Vertaal het wiskundig bewijs naar de C++ timestamp-validatiefunctie.