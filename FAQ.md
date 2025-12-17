**Vragenlijst**

1.  **Hoe communiceren nodes met elkaar? Hiermee bedoel ik, hoe gebeurt de toekenning van IP-adressen en wordt dit aan elke node doorgegeven? Hoe vinden ze elkaar over het internet? Gaan firewalls en vpn's een probleem geven? Hoe kunnen we dit best aanpakken?**
    *   **Communicatieprotocol:** Nodes communiceren via een P2P (Peer-to-Peer) gossip-protocol dat draait over TCP/IP. De `SocketTransport` klasse (`src/p2p/socket_transport.cpp`) beheert de directe verbindingen. Berichten (gedefinieerd in `.proto` bestanden) worden geserialiseerd met Google Protobuf.
    *   **Peer Discovery (Hoe vinden ze elkaar?):** Er is geen centrale server die IP-adressen toekent. Het netwerk gebruikt een **seed peer** mechanisme. In het configuratiebestand (`config/default.toml`) staat een lijst met `seed_peers`. Een nieuwe node maakt verbinding met deze seeds en vraagt hen om een lijst van andere bekende peers (`PeerListMessage`). Op deze manier ontdekt de node organisch het netwerk.
    *   **Firewalls en VPNs:** Ja, dit is een potentieel probleem. Een node achter een strikte firewall of NAT (Network Address Translation) die geen inkomende verbindingen toestaat, kan zich niet als een volwaardige peer gedragen. Hij kan wel verbindingen met anderen opzetten, maar het netwerk kan geen verbinding met hem opzetten. Dit schaadt de decentralisatie en robuustheid.
    *   **Aanbevolen aanpak:** De meest betrouwbare oplossing is **port forwarding**. Gebruikers moeten de P2P-poort (standaard `6868` of `8645`) forwarden in hun router naar de machine waar de node op draait. Toekomstige verbeteringen zouden de implementatie van UPnP (Universal Plug and Play) of STUN/TURN-servers kunnen omvatten om dit proces te automatiseren, maar dit is momenteel niet aanwezig.

2.  **Hoe kan ik een saldo van een bepaald adres uit de ledger uitlezen?**
    De node draait een JSON-RPC server (standaard op poort `8080`). Je kunt een request sturen naar de `get_balance` methode met het adres als parameter.
    *Voorbeeld request:*
    ```json
    {
        "jsonrpc": "2.0",
        "method": "get_balance",
        "params": {
            "address": "cqc1q..."
        },
        "id": 1
    }
    ```
    De `JsonRpcServer::handle_get_balance` functie (`src/rpc/handlers.cpp`) verwerkt dit door de balans op te vragen bij de `State` manager (`src/ledger/state.cpp`).

3.  **Is adresrotatie reeds verwerkt in het programma?**
    Nee, de Chronos node software implementeert momenteel geen logica voor automatische adresrotatie (zoals bij HD-wallets, waarbij voor elke transactie een nieuw adres wordt gebruikt). Een adres is een directe hash van een publieke sleutel. Hoewel een gebruiker manueel meerdere sleutelparen (en dus adressen) kan aanmaken en gebruiken, is er geen ingebouwd mechanisme in de node om dit te beheren. Een wallet-applicatie zou dit bovenop de node moeten implementeren.

4.  **Welke standaard is er opgezet voor Protonbuf, en kan deze standaard in de toekomst nog aangepast worden?**
    *   **Standaard:** De structuur van netwerkberichten is vastgelegd in Protobuf-bestanden (`.proto`) met de `proto3` syntax. De twee belangrijkste bestanden zijn:
        *   `proto/p2p_messages.proto`: Definieert algemene P2P-berichten zoals `Handshake`, `Block`, `Transaction` en peer discovery berichten.
        *   `proto/bft_messages.proto`: Definieert de berichten voor het BFT-consensusalgoritme (`Prevote`, `Precommit`, `NewRound`).
    *   **Aanpasbaarheid:** Ja, de standaard kan worden aangepast door de `.proto` bestanden te wijzigen. Nadat een wijziging is doorgevoerd, moeten de C++ klassen opnieuw worden gegenereerd. Het CMake-buildsysteem is geconfigureerd om dit automatisch te doen wanneer `protoc` (de Protobuf-compiler) wordt uitgevoerd. Bij het aanpassen moet rekening worden gehouden met achterwaartse compatibiliteit. Niet-compatibele wijzigingen vereisen een gecoördineerde update van het netwerk (een hard-fork).

5.  **Wat is het geschat cpu-verbruik van het programma?**
    Dit is sterk afhankelijk van de netwerkactiviteit.
    *   **In rust:** Zeer laag. De node wacht voornamelijk op netwerkberichten.
    *   **Bij activiteit (piekverbruik):** Het CPU-verbruik zal pieken tijdens:
        *   **Cryptografische operaties:** Het verifiëren van Dilithium-handtekeningen en het hashen van blokken/transacties (BLAKE3) zijn CPU-intensieve taken.
        *   **Blokvalidatie:** Het verwerken van een nieuw blok met veel transacties.
        *   **Consensus:** Actief deelnemen aan BFT-stemrondes.
    Een voorzichtige schatting is een **laag tot matig gemiddeld verbruik**, met korte, intense pieken tijdens de consensus en validatie van nieuwe blokken.

6.  **Wat is het geschat ram-gebruik van het programma?**
    *   **Full Node:** Het RAM-gebruik wordt voornamelijk bepaald door de `State` klasse, die de volledige map van accountbalansen in het geheugen laadt. Dit betekent dat het geheugengebruik **lineair schaalt met het aantal unieke adressen** in de ledger. Dit kan oplopen van enkele honderden MB's tot **meerdere GB's** op een groot netwerk.
    *   **Light Node:** In de huidige configuratie gebruikt een light node `MemoryBlockchainStorage`, wat de blockchain in het RAM houdt. Dit is niet schaalbaar en bedoeld voor testdoeleinden.

7.  **Wat is het geschat HD-gebruik van het programma?**
    *   **Full Node:** Aanzienlijk. De standaard opslag (`FileKv`) is een tekstbestand dat data in hexadecimaal formaat wegschrijft. Dit is **zeer inefficiënt** en kan de opslaggrootte met een factor 2 of meer vergroten vergeleken met binaire opslag. Zowel de blockchain zelf als de state worden op deze manier opgeslagen. Het HD-gebruik zal dus **snel en aanzienlijk groeien**.
    *   **Light Node:** Minimaal. Enkel configuratie- en logbestanden worden opgeslagen, aangezien de data in het geheugen wordt bijgehouden.

8.  **kan ik met een oud adres ook een saldo raadplegen en kan ik het laatste nieuwe adres terugvinden?**
    *   **Saldo raadplegen:** Ja, het saldo van elk adres, oud of nieuw, kan op elk moment worden opgevraagd via de `get_balance` RPC-call, zolang het adres een saldo heeft of ooit heeft gehad. Adressen "vervallen" niet.
    *   **Laatste adres terugvinden:** Nee, de node zelf houdt geen concept bij van "wallets" of "gebruikers". Het is een gedistribueerde database van adressen en saldi. Het bijhouden van welke adressen bij een gebruiker horen, inclusief het "laatste" adres, is de verantwoordelijkheid van een externe wallet-applicatie.

9.  **Kunnen wij een interface bouwen, in de terminal, die de stand van zaken weergeeft van het volledige netwerk? Zoals aantal nodes, snelheid van verwerking, grootte van de ledger, aantal blocks in verwerking, aantal actieve NTP's en GPS klokken, aantal full nodes en aantal light nodes, enz..**
    Ja, dit is zeker mogelijk. De basis hiervoor is zelfs al gelegd:
    *   De `NodeStatus` struct (`src/node/node_status.hpp`) bevat al velden zoals `current_block_height`, `mempool_size` en `connected_peers`.
    *   De `ConsoleDisplay` klasse (`src/util/console_display.cpp`) kan een dashboard in de terminal tekenen en bijwerken.
    *   De `get_status` RPC-methode geeft al een deel van deze informatie.
    Om dit uit te breiden, moeten de `NodeStatus`-struct en `ConsoleDisplay`-klasse worden uitgebreid met de extra gewenste metrics. De `NodeApp`-loop moet deze metrics vervolgens periodiek bijwerken.

10. **Kan je mij een gedetaileerd stappenplan maken van de werking van het programma?**
    Zeker, hier is een overzicht van de levenscyclus van de node:
    1.  **Initialisatie (`main.cpp` -> `NodeApp::NodeApp`)**:
        *   De configuratie wordt geladen uit een `.toml` bestand.
        *   Logging, P2P-transport (`SocketTransport`), gossip-protocol, en cryptografische `Signer` worden opgezet.
        *   De state-database (`state_kv_store_`) en blockchain-database (`blockchain_storage_`) worden geïnitialiseerd.
        *   De `State` manager laadt alle accountbalansen in het geheugen.
        *   Consensus-modules (`PoTAggregator`, `BftGadget`) en de `ExternalTimeSourceManager` (voor NTP) worden gestart.
        *   De JSON-RPC server wordt voorbereid.
    2.  **Start (`NodeApp::run`)**:
        *   De P2P-server begint te luisteren naar inkomende verbindingen.
        *   De node verbindt met `seed_peers` en verstuurt een `Handshake`-bericht.
        *   De RPC-server wordt in een aparte thread gestart.
        *   De blockchain-geschiedenis wordt geladen. Als deze leeg is, wordt een **genesis block** aangemaakt.
    3.  **Main Event Loop (continue cyclus in `NodeApp::run`)**:
        *   **Peer Management:** Periodiek worden nieuwe peers ontdekt en worden slechte peers verwijderd.
        *   **Consensus:**
            *   De `PoTAggregator` berekent een betrouwbare netwerktijd (consensus time).
            *   De `BftGadget` wijst een leider aan voor de huidige ronde.
            *   **Als leider:** De node bundelt transacties uit de `mempool` in een nieuw blok en stelt dit voor aan het netwerk via een `NewRound`-bericht.
            *   **Als volger:** De node wacht op een voorstel en stemt (`Prevote`, `Precommit`) op basis van de validatieregels.
    *   **Berichtverwerking (`NodeApp::handle_p2p_message`)**:
        *   Inkomende P2P-berichten (transacties, blokken, stemmen) worden continu verwerkt. Nieuwe transacties gaan de `mempool` in, BFT-berichten worden naar de `BftGadget` gestuurd.
    4.  **Blokfinalisatie (`BftGadget` -> `NodeApp::add_block`)**:
        *   Zodra een 2/3+ meerderheid van `Precommit`-stemmen is ontvangen, is het blok gefinaliseerd.
        *   De transacties in het blok worden toegepast op de `State` (balansen worden bijgewerkt).
        *   Het blok wordt opgeslagen in de `blockchain_storage_`.
        *   De node gaat door naar de volgende blokhoogte en het proces herhaalt zich.

11. **In de code staan er nog heel wat zaken als "te implementeren", kan je deze oplijsten?**
    Hier is een samenvatting van de belangrijkste `TODO`'s en te implementeren onderdelen:
    *   **Consensus (`src/consensus/bft.cpp`)**: De logica voor het daadwerkelijk finaliseren van een blok na een precommit-quorum en het voorstellen van een blok als leider is nog niet volledig uitgewerkt.
    *   **NTP Client (`src/consensus/ntp_client.cpp`)**: De betrouwbaarheidsscore (`confidence`) en foutmarge (`error_ms`) van een NTP-tijdmeting zijn nu hardcoded en moeten dynamisch berekend worden.
    *   **Node App (`src/node/node_app.cpp`)**:
        *   Transactievalidatie in de mempool moet worden uitgebreid (bv. saldo controleren).
        *   Er is nog geen timeout-mechanisme als een BFT-ronde vastloopt.
        *   Het herstellen van de state vanuit een snapshot is nog niet geïmplementeerd.
    *   **Snapshots (`src/storage/snapshots.cpp`)**: Het serialiseren en deserialiseren van de volledige ledger `State` voor snapshots is nog een placeholder.
    *   **RPC (`src/rpc/handlers.cpp`)**: De `is_syncing` status in de `get_status` call is nog niet dynamisch.
