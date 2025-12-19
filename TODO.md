# Chronos - TODO List

## Prioriteit: Hoog

*   **Consensus Mechanisme Implementatie:**
    *   Implementeer een robuust consensus mechanisme (bijv. Proof-of-Stake, Proof-of-Authority, of een aangepast mechanisme).
    *   Zorg voor veilige block validatie en chain synchronisatie.
    *   Implementeer fork resolution.
*   **Netwerk Laag:**
    *   Ontwikkel een peer-to-peer netwerk voor communicatie tussen nodes.
    *   Implementeer node discovery.
    *   Zorg voor betrouwbare message passing (blocks, transacties, etc.).
*   **Transaction Provenance & History Tracking (CRITICAL):**
    *   **Wallet history tracking bij elke tokentransactie**: Implementeer volledige transactiegeschiedenis tracking zodat munten steeds terug naar hun origin kunnen worden bekeken.
        - Elke token moet traceerbaar zijn tot genesis allocatie
        - History chain: tx_hash → previous_tx_hash → ... → genesis
        - State moet transaction history bijhouden per address
        - Provenance verification in consensus validation
        - **BLOKKERENDE FACTOR**: Transacties zonder geldige history chain moeten worden afgewezen
        - Storage: Transaction graph met parent-child relaties
        - RPC endpoints voor history queries (get_token_origin, trace_token_path)
        - Validatie: Elke input moet verwijzen naar geldige vorige output (UTXO-achtig model of account history)
        - Consensus regel: Block met transacties zonder traceerbare provenance is invalid
        - Tests: Token tracking vanaf genesis, fork detection in history, invalid provenance rejection
*   **State Management:**
    *   Implementeer een efficiënte state database (bijv. gebruik makend van Merkle trees).
    *   Zorg voor state persistence en recovery.
*   **Security Audits:**
    *   Voer grondige security audits uit van de codebase.
    *   Identificeer en patch potentiële vulnerabilities.

## Prioriteit: Middel

*   **Smart Contract Functionaliteit (Optioneel maar Wenselijk):**
    *   Overweeg het toevoegen van een VM voor smart contracts (bijv. een vereenvoudigde versie van EVM of een custom VM).
    *   Definieer een bytecode formaat en execution model.
*   **RPC API Uitbreiding:**
    *   Breid de RPC API uit met meer nuttige endpoints.
    *   Verbeter error handling en input validatie.
*   **Wallet Functionaliteit:**
    *   Ontwikkel een gebruiksvriendelijke wallet applicatie (CLI en/of GUI).
    *   Implementeer key management en transactie signing.
*   **Testing & CI/CD:**
    *   Verbeter test coverage (unit tests, integration tests, end-to-end tests).
    *   Stel een CI/CD pipeline op voor geautomatiseerde testing en deployment.
*   **Documentatie:**
    *   Schrijf uitgebreide documentatie voor ontwikkelaars en gebruikers.
    *   Maak tutorials en voorbeelden.

## Prioriteit: Laag

*   **Performance Optimalisatie:**
    *   Optimaliseer de performance van de blockchain (block processing, transaction throughput).
    *   Profile de code en identificeer bottlenecks.
*   **Monitoring & Logging:**
    *   Implementeer monitoring tools om de gezondheid van de blockchain te volgen.
    *   Verbeter logging voor debugging en auditing.
*   **Cross-Platform Support:**
    *   Zorg ervoor dat Chronos werkt op verschillende platforms (Linux, macOS, Windows).
*   **Community Building:**
    *   Start een community rondom Chronos.
    *   Maak een website en social media aanwezigheid.

## Voltooide Items

*   Basis blockchain structuur (Block, Transaction).
*   Genesis block creatie.
*   Basis cryptografie (SHA-256 hashing, Ed25519 signatures).
*   Eenvoudige in-memory state management.
*   Basis RPC server met enkele endpoints.
*   Project structuur en basis configuratie.
