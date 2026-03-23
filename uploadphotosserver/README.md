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

Carte : ESP32-CAM AI Thinker (OV2640).
