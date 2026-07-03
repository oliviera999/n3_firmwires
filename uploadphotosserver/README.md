# Upload Photos — firmware ESP32-CAM unifié

Un seul code source pour trois cibles (galeries iot.olution.info). **Stack unique** pour tous les envs :

| Env    | Galerie     | Stack PlatformIO              | HTTPS | PSRAM |
|--------|-------------|-------------------------------|-------|-------|
| `msp1` | msp1gallery | espressif32@6.13 + `esp32cam` | oui   | oui (diagnostic `[DIAG]`) |
| `n3pp` | n3ppgallery | idem                          | oui   | oui |
| `ffp3` | ffp3gallery | idem                          | oui   | oui |

- **PSRAM** : `board = esp32cam` → `CONFIG_SPIRAM=y`, `psramFound()`, résolution **SXGA** si la puce répond (~4 Mo).
- **HTTPS** : `-DUSE_HTTPS_ENDPOINTS` dans `cam-base` → URLs `https://`, `WiFiClientSecure` (galerie, upload, sync). **OTA** reste en **HTTP** (`/ota/cam/metadata.json`).
- Anciens envs `*-cam` et `msp1-https` supprimés (redondants depuis v2.54).

## Compilation

1. **Credentials** : copier `firmwires/credentials.h.example` vers `firmwires/credentials.h` et remplir `WIFI_LIST[]`. Ne pas versionner `credentials.h`.
2. Compiler : `pio run -e msp1` | `pio run -e n3pp` | `pio run -e ffp3`
3. Upload : `pio run -e <env> -t upload --upload-port COMx`
4. Monitor : `python tools/monitor_serial_cam.py COMx -s 120` (recommandé) ou `pio device monitor` (DTR/RTS désactivés dans `platformio.ini`)

### Moniteur série après deep sleep

Au réveil, les logs n’apparaissent que pendant le cycle actif (~30 s). Pour le debug :

- Script dédié : `python tools/monitor_serial_cam.py COMx -s 120` (DTR/RTS off, attente 120 s)
- Pause boot temporaire : dans `include/config.h`, mettre `SERIAL_BOOT_PAUSE_MS` à **3000–5000** (valeur prod : **0**)

## Configuration

- `include/config.h` : constantes communes et par cible (SERVER_PATH, deep sleep, SD, NTP, créneau 6h–22h).
- Les build flags `-DTARGET_MSP1`, `-DTARGET_N3PP`, `-DTARGET_FFP3` sont définis par l’env PlatformIO.
- `FIRMWARE_VERSION` actuelle : voir `include/config.h` et `VERSION.md`.

### Modules

| Module | Rôle |
|--------|------|
| `camera_setup` | Init OV2640, warmup, exposition, diagnostics `[DIAG]` PSRAM et `[DIAG][SCCB]` |
| `camera_time` | NTP offline-first, TZ `Africa/Casablanca` |
| `camera_sleep` | Créneau photo 6h–22h (fail-closed si horloge non fiable) |
| `camera_remote` | Config distante GET + POST version |
| `camera_uploader` / `camera_upload` | Upload multipart HTTPS (RAM ou streaming SD) |
| `camera_sync` | Backlog SD, drain hybride, sessions sync serveur |
| `camera_mail_events` | Mails transitions jour/nuit, OTA |

## Heure et créneau photo (v2.47+)

- Au réveil : restauration epoch depuis NVS, puis sync NTP (`pool.ntp.org`), fuseau `Africa/Casablanca`.
- Le créneau 6h–22h est interprété en heure locale Casablanca.
- Si l’horloge n’est pas fiable, **aucune capture** (fail-closed) ; le drain backlog SD reste possible si WiFi OK (v2.48+).

## Synchronisation backlog SD (v2.40+, optimisé v2.48+)

- Chaque photo est numérotée et écrite sur SD ; un curseur NVS mémorise la dernière confirmée serveur.
- **Stratégie hybride** : max 10 photos/réveil par défaut ; vidage complet si backlog > 25.
- **v2.48** : drain possible **hors créneau photo** (nuit) si WiFi + SD OK — sans initialiser la caméra.
- **v2.49** : upload backlog par **streaming** depuis la SD (chunks 4096 o), sans charger le JPEG entier en RAM.
- **v2.53+** : pause entre uploads backlog (rate-limit serveur), retry HTTP 429.

## Contrôle distant (GET + POST version)

À chaque réveil, après connexion WiFi, le firmware :

1. récupère les paramètres distants via `n3_data` (`GET` HTTPS),
2. poste sa version firmware (`POST` HTTPS) dans la table de contrôle,
3. applique les paramètres runtime (mail, notifications, `forceWakeUp`, `sleepTime`, `resetMode`).

Endpoints par env (board / sensor) :

- `msp1` : `/msp1gallery/...` (board 6)
- `n3pp` : `/n3ppgallery/...` (board 7)
- `ffp3` : `/ffp3gallery/...` (board 5) — GET sans préfixe `/ffp3/` (301 Apache)

Carte : ESP32-CAM AI Thinker (OV2640).

## Diagnostic PSRAM (boot `[DIAG]`)

| Log | Signification |
|-----|----------------|
| `CONFIG_SPIRAM=y` + `chip_size≈4194304` + `spiram_heap total>0` | PSRAM **présente et utilisable** |
| `CONFIG_SPIRAM=y` + `chip_size=0` | Build OK mais **puce absente** ou non détectée (clone sans PSRAM) |
| `psramFound()=true` + critères SXGA OK | Tentative **SXGA** au prochain `esp_camera_init` |

## Diagnostic SCCB (`[DIAG][SCCB]`)

Sonde I2C (GPIO 26/27) avant `esp_camera_init` : ping **0x30**, PID **0x2642**, scan bus.

- **NACK** + échec caméra → nappe FFC, alim 5 V.
- **NACK** mais **SXGA/psram OK** → faux négatif possible (sonde `Wire` vs `esp_camera`).

## OTA

- Metadata : `http://iot.olution.info/ota/cam/metadata.json` (HTTP, hors flag TLS)
- Vérification sha256 + signature ECDSA optionnelle ; périodique toutes les 2 h (cycles deep sleep).

## HTTPS

TLS activé par défaut sur tous les envs (`USE_HTTPS_ENDPOINTS` dans `platformio.ini`). `setInsecure()` : chiffrement sans épinglement certificat.

### Validation terrain (2026-07-03, env `msp1`, firmware 2.52+)

Module AI-Thinker (MAC `08:3a:f2:aa:42:74`) : PSRAM ~4 Mo, SXGA, GET/POST/upload **HTTPS 200**, heap min ~106 Ko. Détails : `docs/HTTPS_MIGRATION.md`.

### Rollback HTTP

Retirer `-DUSE_HTTPS_ENDPOINTS` de `[env:cam-base]` dans `platformio.ini`, recompiler et flasher.
