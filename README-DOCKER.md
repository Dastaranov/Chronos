# Chronos Node — Docker Deployment Handleiding

## Vereisten op de server
- Docker Engine ≥ 24.0
- Docker Compose ≥ 2.20
- Ubuntu 22.04 / Debian 12 (aanbevolen)
- Poort **8645** open in firewall (P2P)
- Poort 8080 hoeft **niet** publiek open (alleen lokaal / reverse proxy)

---

## Stap 1 — Repository klonen op de server

```bash
git clone <jouw-repo-url> /opt/chronos
cd /opt/chronos
```

---

## Stap 2 — Docker image bouwen

```bash
docker compose build
```

> ⚠️ De eerste build duurt ~15–30 minuten (Protobuf, LevelDB en liboqs worden gecompileerd).

---

## Stap 3 — Validator sleutel genereren

```bash
docker compose run --rm chronos wallet_cli generate-keys validator-1
```

De sleutel wordt opgeslagen in het `chronos-keys` Docker volume (persistent).

Toon de publieke sleutel:

```bash
docker compose run --rm chronos wallet_cli show-public validator-1
```

Kopieer de uitvoer — een lange hex-string.

---

## Stap 4 — Config aanpassen

Bewerk `config/node.toml`:

```toml
[consensus]
validators = ["PLAK_HIER_DE_PUBLIEKE_SLEUTEL"]

[crypto]
private_key_id = "validator-1"
public_key = "PLAK_HIER_DE_PUBLIEKE_SLEUTEL"
```

---

## Stap 5 — Node starten

```bash
docker compose up -d
```

Logs bekijken:

```bash
docker compose logs -f
```

Je ziet berichten als:
```
Block finalized at height 1 round 0 with hash ...
Block finalized at height 2 round 0 with hash ...
```

---

## Beheer

### Status opvragen (JSON-RPC)
```bash
curl http://localhost:8080/status
```

### Node stoppen
```bash
docker compose down
```

### Node stoppen en data verwijderen (⚠️ onherstelbaar)
```bash
docker compose down -v
```

### Updaten naar nieuwe versie
```bash
git pull
docker compose build
docker compose up -d
```

---

## Firewall instellen (ufw)

```bash
ufw allow 8645/tcp comment "Chronos P2P"
ufw reload
```

---

## Tweede node toevoegen (multi-validator setup)

Zodra je een tweede server hebt:
1. Genereer ook daar een validator sleutel
2. Voeg beide publieke sleutels toe aan `validators = [...]` op beide servers
3. Voeg de server IP's toe aan `seeds = ["ip1:8645", "ip2:8645"]`
4. Herstart beide nodes

---

## Data locaties (binnen de container)

| Pad | Inhoud |
|-----|--------|
| `/data/chronos/` | Blockchain data (LevelDB) |
| `/home/chronos/.chronos/keys/` | Private sleutels |
| `/data/chronos/config.toml` | Config (mount vanuit `config/node.toml`) |

---

## Troubleshooting

**Node start niet:**
```bash
docker compose logs chronos | tail -50
```

**Poort al in gebruik:**
```bash
ss -tlnp | grep 8645
```

**Sleutel kwijt:**  
De sleutels zitten in het `chronos-keys` Docker volume. Backup dit volume regelmatig:
```bash
docker run --rm -v chronos-keys:/keys -v $(pwd):/backup ubuntu \
    tar czf /backup/chronos-keys-backup.tar.gz -C /keys .
```
