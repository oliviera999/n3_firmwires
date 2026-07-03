# Upload Photos — firmware ESP32-CAM unifié

Un seul code source pour trois cibles (galeries iot.olution.info) :

| Env        | Galerie     | Stack PlatformIO              | Comportement              |
|------------|-------------|-------------------------------|---------------------------|
| `msp1`     | msp1gallery | pioarduino + `esp32dev`       | Deep sleep 600 s, SD_MMC  |
| `n3pp`     | n3ppgallery | pioarduino + `esp32dev`       | Deep sleep 600 s, SD_MMC  |
| `ffp3`     | ffp3gallery | pioarduino + `esp32dev`       | Deep sleep 600 s, SD_MMC  |
| `msp1-cam` | msp1gallery | espressif32@6.13 + `esp32cam` | **Recommandé** si PSRAM réelle (AI-Thinker) |
| `n3pp-cam` | n3ppgallery | idem                          | SXGA typique si PSRAM OK  |
| `ffp3-cam` | ffp3gallery | idem                          | idem                      |

Les envs `*-cam` activent `psramFound()` et la résolution SXGA lorsque la carte dispose de PSRAM QSPI. Les envs sans suffixe restent alignés sur la toolchain WROOM du dépôt (CIF/DRAM si pas de PSRAM détectée).

## Compilation

1. **Credentials** : copier `firmwires/credentials.h.example` vers `firmwires/credentials.h` et remplir `WIFI_LIST[]`. Ne pas versionner `credentials.h`.
2. Compiler : `pio run -e msp1` | `pio run -e n3pp` | `pio run -e ffp3` (ou `*-cam` sur matériel AI-Thinker)
3. Upload : `pio run -e <env> -t upload`
4. Monitor : `pio device monitor -e <env>` (115200 bauds, DTR/RTS désactivés dans `platformio.ini`)

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
| `camera_setup` | Init OV2640, warmup, exposition |
| `camera_time` | NTP offline-first, TZ `Africa/Casablanca` |
| `camera_sleep` | Créneau photo 6h–22h (fail-closed si horloge non fiable) |
| `camera_remote` | Config distante GET + POST version |
| `camera_uploader` / `camera_upload` | Upload multipart HTTP (RAM ou streaming SD) |
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

## Contrôle distant (GET + POST version)

À chaque réveil, après connexion WiFi, le firmware :

1. récupère les paramètres distants via `n3_data` (`GET`),
2. poste sa version firmware (`POST`) dans la table de contrôle,
3. applique les paramètres runtime :
   - mail (`gpio 102`),
   - notifications mail (`gpio 103`, `checked/false`),
   - `forceWakeUp` one-shot (`gpio 104`),
   - `sleepTime` en secondes (`gpio 105`),
   - `resetMode` (`gpio 106`) — consomme un cycle complet puis redémarre.

Endpoints legacy par env (compatibilité) :

- `msp1` : `/msp1gallery/uploadphotoserver-outputs-action.php` et `/msp1gallery/post-uploadphotoserver-version.php` (board 6 / `UploadPhoto2Outputs`)
- `n3pp` : `/n3ppgallery/uploadphotoserver-outputs-action.php` et `/n3ppgallery/post-uploadphotoserver-version.php` (board 7 / `UploadPhoto3Outputs`)
- `ffp3` : `/ffp3gallery/uploadphotoserver-outputs-action.php` et `/ffp3gallery/post-uploadphotoserver-version.php` (board 5 / `UploadPhoto1Outputs`). Ne pas prefixer par `/ffp3/` sur les GET (301 Apache) ; les anciens chemins `/ffp3/ffp3gallery/...` restent valides en POST via rewrite interne.
- Le serveur valide `board` et `sensor` sur le POST version (HTTP 400 si mismatch), et valide `board` sur le GET state quand fourni.

Carte : ESP32-CAM AI Thinker (OV2640).

## OTA

- Metadata : `http://iot.olution.info/ota/cam/metadata.json`
- Chaque cible (`msp1`, `n3pp`, `ffp3`) expose `version`, `url`, `sha256` et `signature` (optionnelle).
- Le firmware vérifie la version distante, puis **valide sha256** du binaire avant flash.
- Si `signature` est présente, une vérification ECDSA (clé publique embarquée) est aussi effectuée.
- Vérification OTA périodique : toutes les 2h cumulées de cycles deep sleep.

## HTTPS (expérimental)

Env `msp1-https` : TLS opt-in, ~40 Ko RAM supplémentaires. Validation sur cible obligatoire avant prod. Voir `docs/HTTPS_MIGRATION.md`.
