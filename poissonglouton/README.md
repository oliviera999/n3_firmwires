# Poissonglouton (ESP32-S3)

Firmware de comptage de bouteilles pour poubelle de recyclage ludique.

## Environnements

- `pgl-s3-headless` : ESP32-S3 sans ecran.
- `pgl-s3-display` : Guition JC4827W543 (LVGL + tactile admin).

## Fonctions

- Comptage rigoureux avec debounce.
- Capteurs auto-adaptatifs :
  - IR seul,
  - ultrason seul,
  - tandem IR + ultrason (validation croisee).
- Messages audio via DFPlayer Mini (fallback silencieux si absent).
- Buffer FIFO offline (NVS) et envoi par lot vers le serveur.
- Deep sleep avec reveil IR (ext0) ou timer fallback.

## Secrets

Poissonglouton utilise ses **propres** secrets locaux (macros prefixees `PGL_`),
et **n'utilise pas** le `credentials.h` racine partage par n3pp / msp /
uploadphotosserver.

Copier `include/secrets.h.example` en `include/secrets.h` (non versionne, voir
`.gitignore`) puis renseigner :

- SSID / mot de passe Wi-Fi (`PGL_WIFI_SSID_1/2`, `PGL_WIFI_PASS_1/2`).
- `PGL_API_KEY` (meme valeur que cote serveur).

Inclusion (`src/pgl_network.cpp`) : `secrets.h` si present, sinon repli sur
`secrets.h.example` (valeurs factices, pour que la compilation passe), sinon
erreur de build. Les deux fichiers sont resolus via `-Iinclude`
(`platformio.ini`).

## Build

Depuis `firmwires/poissonglouton/` :

```bash
pio run -e pgl-s3-headless
pio run -e pgl-s3-display
```

## Audio SD

DFPlayer lit les fichiers `001.mp3` a `010.mp3` dans `mp3/` sur la carte SD.

Exemples de voix :

- "Merci champion du recyclage !"
- "Glouglou ! Une bouteille sauvee !"
- "Encore une ! Poissonglouton est content."
