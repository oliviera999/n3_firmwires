# Upload Photos — firmware ESP32-CAM unifié

Un seul code source pour trois cibles (galeries iot.olution.info) :

| Env   | Galerie           | Comportement              |
|-------|-------------------|---------------------------|
| `msp1`| msp1gallery       | Deep sleep 15 s (temporaire), SD_MMC |
| `n3pp`| n3ppgallery       | Deep sleep 15 s (temporaire), SD_MMC |
| `ffp3`| ffp3/ffp3gallery  | Deep sleep 15 s (temporaire), SD_MMC |

## Compilation

1. **Credentials** : copier `include/credentials.h.example` vers `include/credentials.h` et remplir `WIFI_LIST[]`. Ne pas versionner `credentials.h`.
2. Compiler : `pio run -e msp1` | `pio run -e n3pp` | `pio run -e ffp3`
3. Upload : `pio run -e <env> -t upload`
4. Monitor : `pio device monitor -e <env>`

## Configuration

- `include/config.h` : constantes communes et par cible (SERVER_PATH, deep sleep, SD, NTP, créneau 6h–22h).
- Les build flags `-DTARGET_MSP1`, `-DTARGET_N3PP`, `-DTARGET_FFP3` sont définis par l’env PlatformIO.
- `FIRMWARE_VERSION` actuelle : `2.27`.

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
- `ffp3` : `/ffp3/ffp3gallery/uploadphotoserver-outputs-action.php` et `/ffp3/ffp3gallery/post-uploadphotoserver-version.php` (board 5 / `UploadPhoto1Outputs`)

Carte : ESP32-CAM AI Thinker (OV2640).
