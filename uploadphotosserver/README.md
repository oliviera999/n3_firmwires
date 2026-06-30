# Upload Photos — firmware ESP32-CAM unifié

Un seul code source pour trois cibles (galeries iot.olution.info) :

| Env   | Galerie           | Comportement              |
|-------|-------------------|---------------------------|
| `msp1`| msp1gallery       | Deep sleep 600 s, SD_MMC |
| `n3pp`| n3ppgallery       | Deep sleep 600 s, SD_MMC |
| `ffp3`| ffp3gallery       | Deep sleep 600 s, SD_MMC |

## Compilation

1. **Credentials** : copier `firmwires/credentials.h.example` vers `firmwires/credentials.h` et remplir `WIFI_LIST[]`. Ne pas versionner `credentials.h`.
2. Compiler : `pio run -e msp1` | `pio run -e n3pp` | `pio run -e ffp3`
3. Upload : `pio run -e <env> -t upload`
4. Monitor : `pio device monitor -e <env>`

## Configuration

- `include/config.h` : constantes communes et par cible (SERVER_PATH, deep sleep, SD, NTP, créneau 6h–22h).
- Les build flags `-DTARGET_MSP1`, `-DTARGET_N3PP`, `-DTARGET_FFP3` sont définis par l’env PlatformIO.
- `FIRMWARE_VERSION` actuelle : voir `include/config.h` et `VERSION.md`. Code modularisé (`camera_setup`, `camera_upload`, `camera_sleep`, `camera_mail_events`, `camera_remote`).

## File SD et synchronisation offline

- Chaque photo enregistrée sur SD reçoit un numéro NVS (`pic_count`) et reste en attente tant que le serveur ne l'a pas confirmée.
- Le curseur d'upload (`up_cursor`) n'avance qu'après un upload HTTP accepté (`200`/`202`). Si le compteur NVS annonce un backlog mais que l'énumération SD ne retourne aucune photo, le curseur est conservé pour éviter d'effacer logiquement des photos encore récupérables après une erreur SD transitoire.

## Contrôle distant (GET + POST version)

À chaque réveil, après connexion WiFi, le firmware :

1. récupère les paramètres distants via `n3_data` (`GET`),
2. poste sa version firmware (`POST`) dans la table de contrôle,
3. applique les paramètres runtime :
   - mail (`gpio 102`),
   - notifications mail (`gpio 103`, `checked/false`),
   - `forceWakeUp` one-shot (`gpio 104`),
   - `sleepTime` en secondes (`gpio 105`),
   - `resetMode` (`gpio 106`).

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
