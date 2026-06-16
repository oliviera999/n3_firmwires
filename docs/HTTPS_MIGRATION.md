# Migration HTTPS (TLS) — canal d'envoi de donnees `n3pp` / `msp` / `uploadphotosserver`

Capacite HTTPS (TLS) **opt-in** pour le canal d'envoi de donnees, derriere un
**flag de compilation**. **HTTP reste le defaut** : tant que le flag n'est pas
active, le comportement des appareils est **strictement identique** a l'existant.

Ce livrable est **REVERSIBLE** : rebuild sans le flag = retour HTTP, aucune
modification cote appareil.

---

## Nom du flag

```
USE_HTTPS_ENDPOINTS
```

Reutilise le flag deja introduit cote `ffp5cs` (`include/server_url_config.h`,
env `wroom-prod-https`) pour la coherence du depot. `ffp5cs` n'utilise pas
`shared/n3_data` et n'est **pas** touche par cette migration (seul le **nom** du
flag est partage).

---

## Ce que fait le flag

| Composant | Flag OFF (defaut) | Flag ON (`-DUSE_HTTPS_ENDPOINTS`) |
|---|---|---|
| `shared/n3_data` (`n3DataPost` / `n3DataGet`) | `WiFiClient` (HTTP clair) | `WiFiClientSecure` + `setInsecure()` |
| URLs n3pp (`n3pp/src/n3pp_globals.cpp`) | `http://iot.olution.info/...` | `https://iot.olution.info/...` |
| URLs msp (`msp/src/main.cpp`) | `http://iot.olution.info/...` | `https://iot.olution.info/...` |
| URLs upload (`uploadphotosserver/include/config.h`) | `http://...`, port 80 | `https://...`, port 443 |
| Upload photo (`uploadphotosserver/src/main.cpp`) | `WiFiClient` + `http://` | `WiFiClientSecure` + `setInsecure()` + `https://` |

Le schema d'URL et le type de client sont selectionnes par le **meme** flag
(`#if defined(USE_HTTPS_ENDPOINTS) ... #else ...(code HTTP inchange)... #endif`).

`shared/n3_data` n'inclut `<WiFiClientSecure.h>` **que** sous le flag : le build
par defaut et les tests natifs (`shared/tests_native`, qui ne fournissent pas de
stub `WiFiClientSecure.h`) restent inchanges.

> **Hors scope** : les URLs OTA (`/ota/.../metadata.json`) restent en HTTP et ne
> sont pas modifiees par ce flag (canal distinct, traitement OTA separe ; meme
> approche que `ffp5cs` ou l'OTA garde sa propre URL).

---

## Activer (build HTTPS)

PlatformIO (depuis le dossier du firmware) :

```bash
# n3pp
cd n3pp && pio run -e n3pp-https

# msp
cd msp && pio run -e msp-https

# uploadphotosserver (ESP32-CAM) — EXPERIMENTAL, voir caveat RAM ci-dessous
cd uploadphotosserver && pio run -e msp1-https
```

Chaque env `-https` herite de l'env de production correspondant et ajoute
seulement `-DUSE_HTTPS_ENDPOINTS` (les envs par defaut HTTP sont inchanges).

---

## Rollback (retour HTTP)

Rebuild + reflash avec l'environnement **par defaut** (sans le flag) :

```bash
cd n3pp && pio run -e esp32dev
cd msp  && pio run -e esp32dev
cd uploadphotosserver && pio run -e msp1
```

Le rollback est purement **logiciel cote build** : aucun etat appareil a defaire.
Le chemin par defaut de `shared/n3_data` n'ayant jamais bouge, les firmwares
construits sans le flag (y compris `poissonglouton` et `ffp5cs`) sont
**bit-pour-bit equivalents** a avant cette migration.

---

## CI

`.github/workflows/firmware-ci.yml` compile en plus les variantes HTTPS pour
detecter toute erreur de compilation du chemin TLS :

- `n3pp-https` (env `n3pp-https`)
- `msp-https` (env `msp-https`)

Les builds HTTP par defaut (`n3pp` / `msp` / `uploadphotosserver-msp1`) restent
inchanges dans la matrice. **Aucun** env HTTPS ESP32-CAM n'est ajoute a la CI
(voir caveat RAM).

> La CI ne fait que **compiler**. Elle ne valide **pas** le handshake TLS reel ni
> la reception des donnees (pas de materiel ni de serveur en CI).

---

## Caveats (a lire avant tout deploiement)

### 1. `setInsecure()` — chiffrement sans epinglage de certificat

Le chemin TLS appelle `client.setInsecure()` : la connexion est **chiffree**
(confidentialite : `api_key`, capteurs, body ne transitent plus en clair) mais le
certificat serveur **n'est pas verifie** (pas de protection contre un MITM actif).

C'est le **1er pas** qui regle la confidentialite. **Suivi documente** :
epinglage CA via `client.setCACert(...)` (certificat racine de `iot.olution.info`)
ou bundle de CA, en complement de la signature HMAC `X-Signature` deja en place
(integrite). A planifier avant de basculer HTTPS en defaut.

### 2. Cout RAM (~40 KB/handshake)

`WiFiClientSecure` alloue ~40 KB de heap par handshake TLS, sur des appareils
deja contraints.

- **n3pp / msp** (ESP32 WROOM) : marge a priori suffisante — **a confirmer sur
  cible** (heap libre au moment de l'envoi).
- **uploadphotosserver** (ESP32-CAM AI Thinker) : **RAM tres tendue**. Les ~40 KB
  TLS s'ajoutent au framebuffer photo : le handshake peut **echouer faute de
  RAM**. C'est pourquoi l'env `msp1-https` est marque **EXPERIMENTAL** et **exclu
  de la CI**. Ne l'activer que pour une validation sur cible dediee, avec preuve
  d'un upload TLS reussi (heap surveille). En cas d'echec, garder l'upload photo
  en HTTP et n'activer HTTPS que sur n3pp/msp.

### 3. Validation SUR CIBLE — OBLIGATOIRE avant prod

**Pompes / poissons en jeu** : un envoi muet est un risque metier.

Avant tout deploiement prod d'un firmware avec `USE_HTTPS_ENDPOINTS`, valider
**sur materiel reel** :

1. Handshake TLS reel reussi avec `iot.olution.info` (pas seulement compilation).
2. Donnees POST **reellement recues** et acceptees cote serveur (code 2xx,
   ligne BDD).
3. GET de config distante (`outputs`) fonctionnel en TLS.
4. Upload photo confirme (uploadphotosserver) si HTTPS y est tente.
5. Heap libre suffisant pendant le handshake (pas de reset/OOM), sur plusieurs
   cycles.

Tant que cette validation n'est pas faite, **rester en HTTP (defaut)**.
