# Poissonglouton (ESP32-S3)



Firmware de comptage de bouteilles pour poubelle de recyclage ludique.



## Environnements

- **`pgl-s3-display`** (defaut) : Guition JC4827W543 — ecran LVGL 480x272, tactile GT911, audio I2S.
- **`pgl-s3-jc3248`** : Guition JC3248W535 — ecran LVGL 320x480 portrait, tactile AXS15231B, flash 16 Mo.
- **`pgl-s3-headless`** (secondaire) : ESP32-S3 DevKit sans ecran — bench / tests capteurs (audio desactive).
- **`pgl-s3-display-pir`** : JC4827 + capteur PIR (GPIO5, `-DPGL_ENABLE_PIR=1`).
- **`pgl-s3-headless-sleep`** : headless avec deep sleep (`-DPGL_ENABLE_SLEEP=1`).
- **`pgl-s3-debug`** : comme display mais veille desactivee et logs verbeux (`PGL_DEBUG_NO_SLEEP=1`).

Reference materielle JC3248 : `docs/JC3248W535_REFERENCE.md` (depot parent IOT_n3).



## Fonctions



- Comptage rigoureux avec debounce.

- Capteurs auto-adaptatifs :

  - IR seul,

  - ultrason seul,

  - tandem IR + ultrason (validation croisee).

- Messages audio via ampli I2S integre JC4827W543 (sortie **speak**, fallback silencieux si SD absente).

- Buffer FIFO offline (NVS) et journal append-only sur carte SD (`pgl_event_journal`) avec sync idempotente (`device_event_id` / `last_acked_event_id`).

- Envoi par lot vers le serveur (`POST /pgl/post-data`, HTTP par defaut ; env `*-https` pour TLS).

- **Auth serveur** : `api_key` seule par defaut (pas de HMAC FFP3). HMAC optionnel via
  `PGL_API_SIG_SECRET` dans `secrets.h` si NTP synchronise (aligne n3pp/msp).

- **OTA** : metadata `http://iot.olution.info/ota/pgl/metadata.json` (HTTP volontaire,
  independant du schema POST). Verification sha256 + ECDSA P-256 (`n3_common/n3_ota`).
  Desactiver : `-DPGL_ENABLE_OTA=0`. Cadence : 2 h (`PGL_OTA_CHECK_INTERVAL_MS`).
  Voir `docs/WIFI_OTA_REFERENCE.md` (depot firmwires).

- **Offline-first** : le comptage, l'audio et l'ecran sont operationnels des le boot ; le WiFi se connecte en arriere-plan si un reseau configure est disponible (retry toutes les 60 s en cas d'echec).

- Deep sleep avec reveil IR (ext0) ou timer fallback.

## Interface LVGL

- Tableau de bord en **cartes** (compteurs, capteurs, système) en LVGL 8.4.
- Pastilles LED d’état (IR, WiFi, serveur), jauge ultrason (arc), et barre de file d’attente upload.



## Câblage JC4827W543 (`pgl-s3-display`)

| Element | Broche / connecteur |
|---------|---------------------|
| Haut-parleur | Connecteur **speak** (JST 1,25 mm, 8 Ω 0,5–3 W) |
| Carte SD | Fente micro-SD du module (pas de DFPlayer externe) |
| Capteur IR | **GPIO 7** (RTC/ext0 ; `-DPGL_IR_PRESENT=1` en prod display) |
| Capteur PIR (option) | **GPIO 5** (`-DPGL_ENABLE_PIR=1`, env `pgl-s3-display-pir`) |
| Ultrason HC-SR04 | GPIO 6 (broche partagee trig/echo) |

## Câblage JC3248W535 (`pgl-s3-jc3248`)

| Element | Broche / connecteur |
|---------|---------------------|
| Ecran / tactile | Integres (AXS15231B QSPI + I2C SDA=4 SCL=8) |
| Capteur IR externe | **GPIO 7** (eviter GPIO4 = SDA tactile) |
| Ultrason HC-SR04 | GPIO 6 |
| Audio / SD | Broches JC4827 par defaut (SD/I2S) — a valider sur le module 3,5" au flash |



## Secrets



Poissonglouton utilise ses **propres** secrets locaux (macros prefixees `PGL_`),

et **n'utilise pas** le `credentials.h` racine partage par n3pp / msp /

uploadphotosserver.



Copier `include/secrets.h.example` en `include/secrets.h` (non versionne, voir

`.gitignore`) puis renseigner :



- SSID / mot de passe Wi-Fi (`PGL_WIFI_SSID_1/2`, `PGL_WIFI_PASS_1/2`).

- `PGL_API_KEY` (meme valeur que cote serveur).

- Optionnel : `PGL_API_SIG_SECRET` pour HMAC FFP3 (voir commentaire dans l'exemple).



Inclusion (`src/pgl_network.cpp`) : `secrets.h` si present, sinon repli sur

`secrets.h.example` (valeurs factices, pour que la compilation passe), sinon

erreur de build. Les deux fichiers sont resolus via `-Iinclude`

(`platformio.ini`).



## Build



Depuis `firmwires/poissonglouton/` :



```bash
pio run -e pgl-s3-headless
pio run -e pgl-s3-display
pio run -e pgl-s3-jc3248
```



## Audio SD



L'ESP32 lit des fichiers MP3 numerotes dans le dossier `mp3/` a la racine de la carte SD

du module (`001.mp3`, `002.mp3`, … sans trou dans la numerotation). Au demarrage, le firmware

detecte automatiquement le nombre de pistes presentes et en joue une au hasard a chaque

comptage (sans repeter la meme deux fois de suite si plusieurs pistes).



Preparation : formater en FAT32, creer `mp3/`, copier les fichiers, inserer la SD dans

le JC4827W543 (`scripts/prepare_audio_sd.ps1`).



Broches I2S (interne) : BCLK=42, LRCK=2, DOUT=41. SPI SD : CS=10, SCK=12, MOSI=11, MISO=13.



Exemples de voix :



- "Merci champion du recyclage !"

- "Glouglou ! Une bouteille sauvee !"

- "Encore une ! Poissonglouton est content."

