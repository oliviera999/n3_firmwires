# Migration HTTPS (TLS) — canal d'envoi de donnees `n3pp` / `msp` / `uploadphotosserver`

Capacite HTTPS (TLS) pour le canal d'envoi de donnees, derriere un **flag de
compilation** (`USE_HTTPS_ENDPOINTS`).

**Etat actuel (audit 2026-07)** : `n3pp` et `msp` activent HTTPS sur l'env par
defaut `esp32dev` (`-DUSE_HTTPS_ENDPOINTS` + `https://` dans les config headers).
`uploadphotosserver` est HTTPS depuis v2.54.

Rollback HTTP possible au build avec `-DUSE_HTTP_ENDPOINTS` (voir ci-dessous).

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

## Build HTTPS (defaut n3pp / msp / cam)

PlatformIO (depuis le dossier du firmware) :

```bash
# n3pp / msp — HTTPS inclus dans esp32dev
cd n3pp && pio run -e esp32dev
cd msp  && pio run -e esp32dev

# uploadphotosserver (ESP32-CAM) — HTTPS par défaut depuis v2.54
cd uploadphotosserver && pio run -e msp1
```

- **n3pp** / **msp** : `esp32dev` et `esp32dev_test` portent `-DUSE_HTTPS_ENDPOINTS`
  dans `platformio.ini` (plus d'env `*-https` separe : supprime comme redondant).
- **uploadphotosserver** : tous les envs (`msp1` / `n3pp` / `ffp3`) héritent de
  `cam-base` : **espressif32@6.13** + **`board = esp32cam`** (PSRAM) +
  `-DUSE_HTTPS_ENDPOINTS`. Plus d'envs `*-cam` ni `msp1-https` (v2.54).

---

## Rollback (retour HTTP)

Rebuild + reflash avec le flag de rollback HTTP :

```bash
# n3pp / msp : retirer USE_HTTPS_ENDPOINTS et ajouter USE_HTTP_ENDPOINTS
# (ex. build_flags dans platformio.ini, ou -DUSE_HTTP_ENDPOINTS en ligne de commande)
cd n3pp && pio run -e esp32dev --build-flag "-DUSE_HTTP_ENDPOINTS"
cd msp  && pio run -e esp32dev --build-flag "-DUSE_HTTP_ENDPOINTS"

cd uploadphotosserver
# Retirer -DUSE_HTTPS_ENDPOINTS de [env:cam-base] dans platformio.ini, puis :
pio run -e msp1
```

Le rollback est purement **logiciel cote build** : aucun etat appareil a defaire.

---

## CI

`.github/workflows/firmware-ci.yml` compile `n3pp` / `msp` sur `esp32dev`
(chemin TLS inclus via `USE_HTTPS_ENDPOINTS`).

**uploadphotosserver** (`msp1` / `n3pp` / `ffp3`) compile en **HTTPS + esp32cam**
dans la matrice CI (meme stack que la prod depuis v2.54).

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
- **uploadphotosserver** (ESP32-CAM AI Thinker) : stack **esp32cam + PSRAM** par
  defaut (v2.54). Sur module **avec PSRAM** (~4 Mo), un cycle complet TLS a ete
  valide en terrain (voir ci-dessous, heap min ~106 Ko). Sur module **sans PSRAM**
  ou clone sans puce, le handshake TLS et la capture SXGA peuvent **echouer** ;
  rollback : retirer `-DUSE_HTTPS_ENDPOINTS` de `cam-base` dans `platformio.ini`.

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

---

## Validation terrain uploadphotosserver (`msp1`, HTTPS par défaut)

**Date** : 2026-07-03 — **firmware** : uploadphotosserver 2.52+ (tests initiaux
sous env `msp1-https`, unifié en `msp1` depuis **2.54**) — **carte** : ESP32-CAM
AI-Thinker, MAC `08:3a:f2:aa:42:74`, port COM7.

### PSRAM (preuve matérielle)

Logs `[DIAG]` au boot :

```
CONFIG_SPIRAM=y
spiram_heap total=4194303 free=4192123 largest_block=4128756
esp_psram_is_initialized=true chip_size=4194303
psramFound()=true
Criteres quantitatifs SPIRAM OK pour tenter SXGA
[CAM] mode actif: SXGA/psram
```

Comparaison historique (avant v2.54) : l'ancien env `msp1` pioarduino affichait
`CONFIG_SPIRAM=n`, `spiram_heap total=0`, capture **CIF** ~13 Ko — le build sans
`esp32cam` ne permettait pas d'attester la PSRAM.

### HTTPS (TLS)

| Flux | URL / comportement | Resultat |
|------|-------------------|----------|
| GET `outputs_state` | `https://iot.olution.info/msp1gallery/uploadphotoserver-outputs-action.php?...` | HTTP **200**, ~1,5 s |
| POST version | `https://iot.olution.info/msp1gallery/post-uploadphotoserver-version.php` | HTTP **200**, ~1,4 s |
| Upload photo (sync SD) | multipart via `WiFiClientSecure` | HTTP **200**, ~183 Ko JPEG |
| OTA metadata | `http://iot.olution.info/ota/cam/metadata.json` | **HTTP** (hors flag, attendu) |

`setInsecure()` : chiffrement sans verification certificat (cf. caveat §1).

### Heap et cycle

- `min_heap` pendant le cycle : **~106 Ko** (SXGA + TLS + sync SD).
- WiFi RSSI observe : -63 a -71 dBm.
- Deep sleep : 300 s (config distante).

### Notes SCCB

La sonde `[DIAG][SCCB]` peut afficher **NACK** sur 0x30 alors que `esp_camera_init`
reussit en **SXGA/psram** : ne pas conclure a une panne OV2640 sur ce seul log.

### Reproductibilite

```powershell
cd firmwires/uploadphotosserver
pio run -e msp1 -t upload --upload-port COMx
python tools/monitor_serial_cam.py COMx -s 120
```

**Non valide** pour : autres modules (ex. carte sans PSRAM ou bus SCCB mort),
déploiement prod généralisé sans re-test sur chaque carte.
