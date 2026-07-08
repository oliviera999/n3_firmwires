# CLAUDE.md

Guide pour Claude Code (et tout agent) travaillant sur **n3_firmwires** — monorepo de
**firmwares ESP32 / ESP32-S3 / ESP32-CAM / Arduino** (PlatformIO) pour l'écosystème IoT
« salle aérée n³ » (serre/aquaponie, station météo, caméras, recyclage ludique).

> 📘 Le `README.md` reste la référence détaillée (build Windows, recovery PlatformIO, libs `shared/`).
> Ce fichier en donne la version opérationnelle pour Claude Code. Les firmwares envoient leurs
> données au serveur du dépôt **n3_serveur**.

## Carte du dépôt (firmwares actifs)

| Firmware | Dossier | Carte | Envs PlatformIO | Version définie dans |
|----------|---------|-------|-----------------|----------------------|
| **n3pp** (serre/aquaponie) | `n3pp/` | ESP32 | `esp32dev`, `esp32dev_test`, `n3pp-https` | `include/n3pp_config.h` (`FIRMWARE_VERSION`) |
| **msp** (station météo) | `msp/` | ESP32 | `esp32dev`, `esp32dev_test`, `msp-https` | `include/msp_config.h` (`FIRMWARE_VERSION`) |
| **uploadphotosserver** (CAM unifié) | `uploadphotosserver/` | ESP32-CAM | `msp1`, `n3pp`, `ffp3` (esp32cam + HTTPS) | `include/config.h` (`FIRMWARE_VERSION`) |
| **ffp5cs** (aquaponie) | `ffp5cs/` | ESP32 WROOM / S3 | `wroom-prod`, `wroom-test`, `wroom-s3-*`… | `include/config.h` / `VERSION.md` |
| **poissonglouton** (recyclage) | `poissonglouton/` | ESP32-S3 | `pgl-s3-headless`, `pgl-s3-display` | `include/config.h` (`PGL_FIRMWARE_VERSION`) |

Le catalogue machine de tous les firmwares (chemins, cartes, cibles OTA, source de version,
envs) est **`firmwares.manifest.json`** — le mettre à jour si on ajoute/déplace un firmware.

> 🏷️ **Nomenclature `ffp3` / `ffp5cs`** : le firmware aquaponie s'appelle **`ffp5cs`**, mais le
> système supervisé **côté serveur** (tables `ffp3Data*`, routes `/post-data*`, cible OTA) s'appelle
> **`ffp3`** ; le firmware s'y identifie via le champ POST `sensor="ffp3"` (`ProjectConfig::SYSTEM_ID`),
> distinct de `BOARD_TYPE` (`esp32-wroom`/`esp32-s3`, clé OTA + `post_id`). Le mot « ffp3 » recouvre
> aussi la galerie caméra (`uploadphotosserver -e ffp3`), le sous-module serveur `ffp5cs/ffp3`, et le
> contrat HMAC générique de `shared/n3_data`. Détails et chantiers différés : **`docs/NOMENCLATURE_FFP3.md`**.

> ⚠️ **Ne pas utiliser** `archive/` (code legacy, ex. anciennes caméras → remplacées par
> `uploadphotosserver/` unifié) ni `à voir/` (prototypes non maintenus : `LVGL_Widgets`, `ratata`).

## Compiler / flasher / monitorer

Chaque firmware se construit **depuis son propre dossier** :

```bash
cd n3pp                    # ou msp, poissonglouton, uploadphotosserver, ffp5cs
pio run                    # build env par défaut
pio run -e esp32dev_test   # build d'un env précis
pio run -e esp32dev -t upload
pio device monitor -e esp32dev          # 115200 bauds
```

- **uploadphotosserver** (multi-galeries) : choisir l'env cible — `pio run -e msp1` / `-e n3pp` / `-e ffp3`.
  Moniteur CAM : `python tools/monitor_serial_cam.py COMx -s 120` (DTR/RTS désactivés).
- Adapter `upload_port` / `monitor_port` dans le `platformio.ini` du projet selon la machine.
- **Tests natifs (Unity)** — ce que vérifie la CI, lançable en local :
  ```bash
  cd shared/tests_native && pio test -c platformio-native.ini -e native -f test_hmac
  cd ffp5cs && pio test -c platformio-native.ini -e native -f test_nvs
  ```

## Secrets — OBLIGATOIRE avant compilation

La compilation **échoue** sans le fichier de secrets correspondant (jamais versionné) :

- **n3pp, msp, uploadphotosserver** : un seul fichier partagé `credentials.h` à la racine du dépôt
  → copier depuis `credentials.h.example` (WiFi, SMTP `SMTP_*`, `API_KEY`, optionnel `API_SIG_SECRET`).
- **ffp5cs** : `include/secrets.h` (WiFi/SMTP, copier l'exemple) et
  `include/secrets_config.h` (`API_KEY`, destinataire, HMAC — copier l'exemple).
- **poissonglouton** : `include/secrets.h` (`PGL_WIFI_*`, `PGL_API_KEY` — copier l'exemple).

En CI, ces fichiers sont provisionnés depuis les `.example` (placeholders).

## Bibliothèques partagées (`shared/`)

Code commun à n3pp / msp / ffp5cs, sous forme de modules PlatformIO (`library.json` + `src/`).
**Réutiliser ces libs plutôt que dupliquer** : `n3_wifi` (scan RSSI multi-réseaux), `n3_data`
(POST URL-encoded + HMAC, remplace `n3_http` déprécié), `n3_hmac` (HMAC-SHA256), `n3_mail` (SMTP),
`n3_time`, `n3_sleep` (deep sleep), `n3_display` (OLED), `n3_analog_sensors`, `n3_battery`,
`n3_tracker` (logique pure du tracker solaire msp, testée en natif), et
`n3_common` (**OTA** `n3_ota` avec vérif **sha256 + ECDSA P-256**, `n3_defaults.h`, `n3_outputs_json`).

## Versionnage firmware — à faire à chaque modification

Voir le skill [`bump-firmware-version`](.claude/skills/bump-firmware-version/SKILL.md). Pour chaque
firmware modifié :
1. Incrémenter la **version** dans sa source (`include/*config.h` → `FIRMWARE_VERSION` /
   `PGL_FIRMWARE_VERSION`, ou `VERSION.md` pour ffp5cs) — voir `versionSource` dans `firmwares.manifest.json`.
2. Documenter dans le `VERSION.md` du firmware (historique).
3. Garder l'alignement avec le serveur quand un contrat d'API/OTA change (cf. `docs/`).

## OTA & toolchain

- **OTA** : cible par firmware (`otaTarget` dans `firmwares.manifest.json`), via le serveur n3_serveur ;
  binaire vérifié sha256 + ECDSA P-256 (`n3_common/n3_ota`). Réf : `docs/WIFI_OTA_REFERENCE.md`.
- **Toolchain** : framework Arduino. WROOM (n3pp/msp/ffp5cs/upload) = **pioarduino** arduino-esp32 3.3.x
  (ESP-IDF 5.x) ; S3 et `*-cam` = `espressif32@6.13.0` (arduino-esp32 2.0.x). Détails et pièges
  Windows (chemins longs `C:\pio-builds`, recovery PlatformIO) dans le `README.md`.

## CI

`.github/workflows/firmware-ci.yml` (sur push `master` / PR) : **tests natifs Unity**
(`shared/` + `ffp5cs`, suite par suite) puis **builds matriciels** (n3pp, msp, variantes `*-https`,
ffp5cs `wroom-test`, poissonglouton headless/display, uploadphotosserver `msp1`). Les secrets sont
provisionnés depuis les `.example`. Avant de pousser : compiler localement le firmware touché +
lancer ses tests natifs (skills [`build-firmware`](.claude/skills/build-firmware/SKILL.md) et
[`firmware-native-tests`](.claude/skills/firmware-native-tests/SKILL.md)).

## Règles

- ❌ Ne jamais committer `credentials.h`, `ffp5cs/include/secrets.h`, `secrets_config.h` (secrets).
- ❌ Ne pas modifier `archive/` ni `à voir/` (legacy / prototypes).
- ✅ Mutualiser le code dans `shared/` ; mettre à jour `firmwares.manifest.json` si la topologie change.
- ✅ Bumper la version du firmware touché (+ `VERSION.md`).
- ✅ Le sous-module `ffp5cs/ffp3` est géré à part (`git submodule update --init` au besoin).
