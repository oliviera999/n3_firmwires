---
name: build-firmware
description: Build, upload or monitor an ESP32/ESP32-S3/ESP32-CAM firmware in the n3_firmwires PlatformIO monorepo. Use when asked to "build/compile a firmware", "flash/upload to the board", "monitor serial", or to reproduce a CI build failure. Knows the per-firmware directories, env names, and the mandatory credentials/secrets step.
---

# Build / flash / monitor d'un firmware (PlatformIO)

Chaque firmware se construit **depuis son propre dossier**. Identifier d'abord la cible
(dossier + env) ; le catalogue est `firmwares.manifest.json`.

## 0. Pré-requis OBLIGATOIRE : secrets

La compilation échoue sans le fichier de secrets (jamais versionné) :
- **n3pp / msp / uploadphotosserver** → `credentials.h` à la racine (copier `credentials.h.example`).
- **ffp5cs** → `include/secrets.h` (+ `include/secrets_config.h`) depuis leurs `.example`.

Pour un simple build de vérification (sans vrais identifiants), les placeholders de l'`.example`
suffisent — c'est ce que fait la CI.

## 1. Build

```bash
cd <dossier>            # n3pp | msp | uploadphotosserver | poissonglouton | ffp5cs
pio run -e <env>
```

Envs principaux :
- **n3pp** : `esp32dev` (défaut), `esp32dev_test`, `n3pp-https`
- **msp** : `esp32dev`, `esp32dev_test`, `msp-https`
- **uploadphotosserver** : `msp1` / `n3pp` / `ffp3` — **esp32cam** + PSRAM + **HTTPS** par défaut (v2.54)
- **poissonglouton** : `pgl-s3-headless`, `pgl-s3-display`
- **ffp5cs** : `wroom-prod`, `wroom-test`, `wroom-s3-test`…

## 2. Flash & moniteur

```bash
pio run -e <env> -t upload
pio device monitor -e <env>          # 115200 bauds
```

- **ESP32-CAM** (uploadphotosserver) : moniteur fiable via `python tools/monitor_serial_cam.py COMx -s 120`
  (DTR/RTS désactivés) ; si rien ne sort, appuyer sur **EN**.
- Adapter `upload_port` / `monitor_port` dans le `platformio.ini` du projet.

## Notes

- 1er build WROOM : téléchargement plateforme pioarduino (~500 Mo).
- Windows : artefacts redirigés vers `C:\pio-builds\…` (voir README, section build Windows / recovery).
- Toujours compiler le firmware touché avant de pousser (la CI compile la même matrice).
