# Poissonglouton (ESP32-S3)



Firmware de comptage de bouteilles pour poubelle de recyclage ludique.



## Environnements

- **`pgl-s3-display`** (defaut) : Guition JC4827W543 — ecran LVGL, tactile, audio I2S.
- **`pgl-s3-headless`** (secondaire) : ESP32-S3 DevKit sans ecran — bench / tests capteurs (audio desactive).



## Fonctions



- Comptage rigoureux avec debounce.

- Capteurs auto-adaptatifs :

  - IR seul,

  - ultrason seul,

  - tandem IR + ultrason (validation croisee).

- Messages audio via ampli I2S integre JC4827W543 (sortie **speak**, fallback silencieux si SD absente).

- Buffer FIFO offline (NVS) et envoi par lot vers le serveur.

- Deep sleep avec reveil IR (ext0) ou timer fallback.



## Câblage JC4827W543 (display)



| Element | Broche / connecteur |

|---------|---------------------|

| Haut-parleur | Connecteur **speak** (JST 1,25 mm, 8 Ω 0,5–3 W) |

| Carte SD | Fente micro-SD du module (pas de DFPlayer externe) |

| Capteur IR | **GPIO 7** (RTC/ext0 ; libre du bus tactile GT911 GPIO 4/8/38) |

| Ultrason HC-SR04 | GPIO 6 (broche partagee trig/echo) |



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

