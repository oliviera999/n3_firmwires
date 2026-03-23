# Firmwires – Projets ESP32 et Arduino (PlatformIO)

*Contexte : [salle aérée n³](https://n3.olution.info) · Données : [iot.olution.info](https://iot.olution.info). Voir [README racine](../README.md), [RECOMMANDATIONS_IOT](../RECOMMANDATIONS_IOT.md), [inventaire des appareils](../docs/inventaire_appareils.md).*

Dépôt regroupant **plusieurs firmwares** : deux projets principaux ESP32 (serre/aquaponie, station météo), trois projets ESP32-CAM d’envoi de photos vers un serveur web, et le kit **ratata** (ZYC0108-EN) avec exemples Arduino UNO et ESP32-CAM. Chaque projet (ou environnement) est compilable avec **PlatformIO**.

## Projets

| Projet | Dossier | Carte | Description |
|--------|---------|-------|-------------|
| **N3PhasmesProto (n3pp)** | `n3pp/` | ESP32 dev | Contrôle serre / aquaponie : température/humidité air, 4 capteurs humidité sol, pompe, luminosité, mails d’alerte, serveur web, NTP, OLED, deep sleep. |
| **MeteoStationPrototype (msp)** | `msp/` | ESP32 dev | Station météo + tracker solaire : 2× DHT, humidité sol, pluie, DS18B20, 4 LDR, 2 servos, relais, mail, serveur web, NTP, OLED. |
| **Upload Photos (unifié)** | `uploadphotosserver/` | ESP32-CAM | Un seul code, trois envs : **msp1** (msp1gallery, OTA distant HTTP, deep sleep 600 s, version courante `2.23`), **n3pp** (n3ppgallery, deep sleep 600 s, SD), **ffp3** (ffp3gallery, deep sleep 600 s). Upload avec header `X-Api-Key`, retries de connexion, vérification du code HTTP retour. Contrôle distant au réveil (GET paramètres + POST version firmware) via les tables `UploadPhoto*Outputs` (boards 5/6/7). Notifications mail SMTP (credentials partagés) : **une fois** au premier démarrage réel (hors réveil deep sleep, flag NVS `upcam/fb_mail`), démarrage/fin OTA, transitions matin/soir du créneau photo. `pio run -e msp1` / `-e n3pp` / `-e ffp3`. |
| **Upload Photos MSP1** (legacy) | `uploadphotosserver_msp1/` | ESP32-CAM | Référence ; préférer `uploadphotosserver` env msp1. |
| **Upload Photos N3PP** (legacy) | `uploadphotosserver_n3pp_1_6_deppsleep/` | ESP32-CAM | Référence ; préférer `uploadphotosserver` env n3pp. |
| **Upload Photos FFP3** (legacy) | `uploadphotosserver_ffp3_1_5_deppsleep/` | ESP32-CAM | Référence ; préférer `uploadphotosserver` env ffp3. |
| **FFP5CS (aquaponie)** | `ffp5cs/` | ESP32 / ESP32-S3 | Contrôleur aquaponie (WROOM/S3), modulaire, API FFP3. |
| **LVGL_Widgets** | `LVGL_Widgets/` | ESP32-S3 | Interface écran tactile ; pas de serveur dédié. |
| **Ratata (ZYC0108-EN)** | `ratata/` | 7× UNO, 1× ESP32-CAM | Huit exemples : déplacement, servo, ultrason, évitement, suivi de ligne, voiture caméra WiFi. |

## Prérequis

- [PlatformIO](https://platformio.org/) (CLI ou extension VSCode/Cursor)
- Selon le projet : carte **ESP32** (esp32dev), **ESP32-CAM** (esp32cam), ou **Arduino UNO** (uno)

## Compilation et upload

Chaque projet se compile et s’upload depuis **son dossier** :

```bash
# N3PhasmesProto (n3pp)
cd n3pp
pio run
pio run -t upload
pio device monitor

# MeteoStation (msp)
cd msp
pio run
pio run -t upload
pio device monitor

# Upload photos (ESP32-CAM unifié) – cible msp1, n3pp ou ffp3
cd uploadphotosserver
pio run -e msp1
pio run -e msp1 -t upload
pio device monitor -e msp1
# Ou : -e n3pp, -e ffp3 selon la galerie cible.

# Ratata – compiler un environnement (ex. 1_auto_move ou 6_1_esp32_car)
cd ratata
pio run -e 1_auto_move
pio run -e 1_auto_move -t upload
pio device monitor -e 1_auto_move
```

Adapter `upload_port` et `monitor_port` dans chaque `platformio.ini` (ex. `COM3`, `COM4`, `/dev/ttyUSB0`) selon ta machine.

## Build Windows : chemins courts, redirection et nettoyage

- **Redirection des artefacts** : les projets qui incluent `firmwires/scripts/pio_redirect_build_dir.py` (**ffp5cs**, **n3pp**, **msp**, **uploadphotosserver**, dossiers **test psram s3***) placent les binaires sous `C:\pio-builds\<slug-projet>\<env>\` sur Windows par défaut (évite chemins longs et soucis d’espaces dans le clone). **Linux / CI** : pas de redirection sauf si la variable d’environnement `N3_PIO_BUILD_ROOT` est définie — les artefacts restent alors sous `.pio/build/<env>/` (comportement GitHub Actions inchangé).
- **Désactiver la redirection** : `N3_PIO_BUILD_REDIRECT=0` (PowerShell : `$env:N3_PIO_BUILD_REDIRECT='0'`). **Autre racine** : `N3_PIO_BUILD_ROOT=D:\mes-builds`.
- **Chemins côté scripts** : `firmwires/scripts/Get-PioBuildHelpers.ps1` (fonctions `Get-N3PioFirmwareBin`, etc.) — utilisé par `IOT_n3/scripts/publish_ota.ps1` pour trouver `firmware.bin` / `littlefs.bin` que le build soit sous `C:\pio-builds` ou dans `.pio/build`.
- **Nettoyage global** : depuis la racine **IOT_n3**, `.\scripts\clean-firmware-builds.ps1` (ajouter `-WhatIf` pour simulation). Options : `-IncludePioBuildsRoot` pour purger les sous-dossiers `C:\pio-builds\<slug>` des projets listés, `-IncludeLegacyFfp5Mirror` pour supprimer l’ancien miroir `C:\ffp5cs_build`.
- **S3 + espaces dans le chemin** : `ffp5cs/run_s3_build_from_safe_path.bat` (miroir sous `C:\pio-builds\ffp5cs-space-mirror` par défaut) ou `run_s3_fix_via_subst.bat` (lecteur `P:`).
- **Toolchain GCC 14 (Xtensa) + Arduino-ESP32 3.3.x** : erreur de link `undefined reference to __atomic_fetch_add_4` (libstdc++ / `shared_ptr` dans Network, FS, SD) — fichier `src/gcc_atomic_compat.c` dans **n3pp**, **msp** et **uploadphotosserver**. **ESP Mail Client** sur partition SPIFFS uniquement : script `firmwires/scripts/pio_patch_esp_mail_fs_spiffs.py` (référencé par **n3pp**, **msp** et **uploadphotosserver**), qui patche une fois `ESP_Mail_FS.h` dans `.pio/libdeps` (idempotent).

Les projets **legacy** caméra (`uploadphotosserver_*` hors dépôt unifié) ou exemples sans `platformio.ini` à la racine peuvent encore compiler uniquement sous `.pio/build/` : ajouter la même ligne `pre:../scripts/pio_redirect_build_dir.py` si besoin.

## Scripts de monitoring et erase-flash

### Référence FFP5CS (workflow complet)

Le projet **ffp5cs** définit le workflow de validation de référence, aligné sur les règles cœur de projet (offline-first, robustesse, vérification après modification) :

- **Workflow complet** : erase flash → flash firmware (+ LittleFS sauf prod) → monitoring N min → analyse du log. Script principal : `ffp5cs/erase_flash_fs_monitor_5min_analyze.ps1` (options `-Port`, `-Environment`, `-DurationMinutes`, `-SkipBuild`, `-NoPrompt`). Pour **ESP32-S3**, utiliser `-Environment wroom-s3-test` ou `-Environment wroom-s3-prod` ; scripts dédiés : `ffp5cs/run_s3_validation.ps1`, `ffp5cs/run_s3_psram_validation.ps1`, `ffp5cs/run_wroom_s3_prod_workflow.ps1`.
- **Monitoring** : `ffp5cs/monitor_5min.ps1` — capture série N secondes. Les logs et rapports d'analyse sont écrits dans le dossier dédié `ffp5cs/logs/` (plus à la racine du projet).
- **Monitoring crash/reboot** : `ffp5cs/monitor_until_crash.ps1` (wrapper PowerShell) appelle `ffp5cs/tools/monitor/monitor_until_crash.py` pour capturer jusqu'au crash/reboot, puis générer un rapport.
- **Analyse des logs** : `ffp5cs/analyze_log.ps1` (détaillée, spécifique FFP5CS/FFP3) et `ffp5cs/analyze_log_exhaustive.ps1` (crashes, WDT, heap, réseau, reboots). Rapport diagnostic : `ffp5cs/generate_diagnostic_report.ps1`. Toutes les sorties (fichiers .log, _analysis.txt, rapports .md) sont dans `ffp5cs/logs/`.

Pour tout travail sur **ffp5cs**, utiliser ces scripts depuis le dossier `ffp5cs/`. Voir aussi `ffp5cs/.cursor/rules/` et `ffp5cs/docs/INVENTAIRE_SCRIPTS_FFP5CS.md` pour l'inventaire des scripts.

- **Compilation de tous les envs** (évite les builds WROOM pioarduino en parallèle, qui peuvent corrompre le cache) : `ffp5cs/scripts/build_all_envs.ps1`.
- **Tests unitaires natifs (Unity)** : `pio test -c platformio-native.ini -e native` depuis `ffp5cs/`, ou `ffp5cs/scripts/test_unit_all.ps1`.

### Scripts racine firmwires (multi-projets)

Des scripts **multi-projets** (`monitor_Nmin.ps1`, `erase_flash_monitor.ps1`, `firmwires/scripts/analyze_log_generic.ps1`) sont décrits dans les règles Cursor / conventions du projet comme cible d’unification ; **ils ne sont pas encore présents dans ce dépôt**. En attendant, utiliser les scripts **ffp5cs** ci-dessus pour le workflow erase / flash / monitor / analyse, ou lancer `pio device monitor` / `pio run -t upload` depuis chaque dossier de firmware.

| Emplacement | Rôle |
|-------------|------|
| `ffp5cs/scripts/Release-ComPort.ps1` | Libération du port COM (processus moniteur). |

**Exemples (référence ffp5cs, depuis `ffp5cs/`) :**

```powershell
# Workflow complet : erase, flash, LittleFS (sauf prod), monitor, analyse
.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-test -Port COM4

# Monitoring seul
.\monitor_5min.ps1 -Port COM4
```

### Uploadphotosserver — build multi-env

Le firmware caméra unifié ne dispose pas (dans ce dépôt) d'un script `build_all_envs.ps1` versionné.
Utiliser les commandes PlatformIO directes depuis `uploadphotosserver/` :

- `pio run -e msp1`
- `pio run -e n3pp`
- `pio run -e ffp3`

## Stack ESP-IDF et plateforme

Tous les firmwares utilisent le **framework Arduino**. La chaîne de build est : plateforme → arduino-esp32 → ESP-IDF (sous-jacent).

**Versions arduino-ESP32 par type d'env :**
- **WROOM** (ffp5cs wroom-prod/test/beta, msp, n3pp, uploadphotosserver) : **arduino-esp32 3.3.7** (ESP-IDF 5.5.2) via la **plateforme pioarduino** ([pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) release 55.03.37). Choix : stack IDF 5.x et alignement avec tous les firmwares WROOM du dépôt.
- **S3** (ffp5cs wroom-s3-*) : **plateforme platformio/espressif32@6.13.0**, arduino-esp32 2.0.17 (bundlé, ESP-IDF 4.4.7). Alignement pioarduino possible à terme (erreur linker « gap » à résoudre).
- **test psram s3** : `espressif32@6.4.0` + arduino-esp32 2.0.14 pour compatibilité S3 PSRAM OPI (voir commentaires dans son `platformio.ini`).

Le **premier build** des projets WROOM télécharge la plateforme pioarduino (~500 Mo) ; en cas d'erreur de verrouillage de fichier (WinError 32/183), fermer les processus PlatformIO/IDE puis relancer.

### Recovery PlatformIO Windows (WROOM / esp32cam)

En cas d'erreur intermittente du type `TypeError: _path_exists ... NoneType` ou `FRAMEWORK_DIR None` :

1. Fermer Cursor/VSCode, tout moniteur série et tous les terminaux qui compilent/flashing.
2. Depuis `firmwires/`, lancer un build de warmup sur un firmware WROOM stable :
   - `cd n3pp`
   - `pio run -e esp32dev`
3. Revenir au firmware cible et relancer le build :
   - `cd ../uploadphotosserver`
   - `pio run -e msp1` (ou `-e n3pp`, `-e ffp3`)
4. Si l'erreur persiste : nettoyer le cache/framework PlatformIO local puis relancer la séquence warmup + build cible.
5. Sous Windows, éviter les builds parallèles pioarduino sur plusieurs projets en même temps.

Détails et APIs ESP-IDF utilisées en direct (ffp5cs) : [RECOMMANDATIONS_IOT.md § 2.9](../RECOMMANDATIONS_IOT.md#29-stack-esp-idf-et-conventions).

## Configuration des ports série

Chaque `platformio.ini` définit `upload_port` et `monitor_port` (souvent `COM3` par défaut). Pour une machine donnée, modifier ces valeurs dans le fichier du projet concerné. Selon l’environnement, il est possible d’utiliser des variables d’environnement (ex. `UPLOAD_PORT`, `MONITOR_PORT`) si votre configuration PlatformIO ou vos scripts les prennent en charge ; à défaut, adapter directement les lignes dans le `platformio.ini` du projet.

## WiFi (ESP32-CAM unifié et projets legacy)

Le firmware **uploadphotosserver** (unifié) et les projets **uploadphotosserver_msp1** / **uploadphotosserver_n3pp_1_6_deppsleep** utilisent une logique WiFi alignée sur ffp5cs (simplifiée) :

- **Credentials** : **un seul fichier partagé** : `firmwires/credentials.h` (copier `firmwires/credentials.h.example`). Utilisé par n3pp, msp et uploadphotosserver (msp1, n3pp, ffp3), y compris les identifiants SMTP (`SMTP_HOST_ADDR`, `SMTP_PORT_NUM`, `SMTP_EMAIL`, `SMTP_PASSWORD`, `SMTP_DEST`) pour les notifications mail caméra. **ffp5cs** utilise son propre `include/secrets.h` (copier `include/secrets.h.example`). Sans le fichier de secrets correspondant, la compilation échoue.
- **Comportement** : au démarrage, scan des réseaux, association des credentials aux AP visibles, tri par RSSI (meilleur signal en premier), puis tentatives de connexion avec BSSID et canal si le réseau est visible (timeout 5 s par tentative, une retry sans BSSID en cas d’échec). Délai 250 ms entre chaque réseau. Pas d’AP de secours.
- **MSP1** : en cas de déconnexion, tentative de reconnexion périodique (toutes les 60 s) dans `loop()`.
- **N3PP** : deep sleep à chaque cycle ; au réveil, `Wificonnect()` est rappelé dans `setup()`.

## Bibliothèques partagées (n3pp, msp)

Les firmwares **n3pp** et **msp** utilisent des libs communes dans `shared/` (WiFi multi-réseaux, HTTP, mail SMTP, batterie, RTC/flash, OTA HTTP distant). Chaque lib est un module PlatformIO (dossier avec `library.json` + `src/`). Les secrets sont dans `credentials.h` à la racine de `firmwires/` (copier `credentials.h.example`).

| Lib | Rôle |
|-----|------|
| `n3_analog_sensors` | Lecture ADC filtrée (luminosité, pont diviseur, humidité sol) : multi-échantillons, médiane/moyenne, rejet outliers. Utilisée par ffp5cs, n3pp, msp. |
| `n3_battery` | Lecture batterie pont diviseur (délègue à `n3_analog_sensors`). Compatible API n3pp/msp. |
| `n3_wifi` | Connexion WiFi multi-réseaux (timeout, callbacks affichage/échec) |
| `n3_http` | GET / POST HTTP |
| `n3_mail` | Envoi email SMTP (ESP Mail Client) |
| `n3_time` | Sauvegarde/restauration heure en flash, raison de réveil |
| `n3_common` | Noyau partagé (OTA, constantes communes, helpers de base) |

## Structure

```
firmwires/
├── .gitignore
├── README.md
├── credentials.h.example       # Template (copier en credentials.h, ne pas versionner)
├── RECOMMANDATIONS.md
├── RAPPORT_ANALYSE.md
├── scripts/
│   ├── Get-PioBuildHelpers.ps1         # Résolution des artefacts (.pio/build ou C:\pio-builds)
│   ├── pio_redirect_build_dir.py       # Redirection build Windows vers C:\pio-builds
│   └── pio_patch_esp_mail_fs_spiffs.py # Patch ESP Mail pour firmwares WROOM/cam
├── shared/                     # Bibliothèques partagées n3 (n3pp, msp, ffp5cs)
│   ├── n3_analog_sensors/      # ADC filtré (luminosité, pont, humidité sol)
│   ├── n3_battery/             # Batterie pont diviseur (délègue à n3_analog_sensors)
│   ├── n3_wifi/
│   ├── n3_http/
│   ├── n3_mail/
│   ├── n3_time/
│   └── n3_common/
├── n3pp/                    # N3PhasmesProto (ESP32)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── n3pp.ino
├── msp/                     # MeteoStationPrototype (ESP32)
│   ├── platformio.ini
│   ├── src/main.cpp
│   ├── lib/, include/, test/
├── uploadphotosserver/         # ESP32-CAM unifié (envs msp1, n3pp, ffp3)
│   ├── platformio.ini
│   ├── include/config.h
│   ├── src/main.cpp
│   ├── tools/pio_ensure_credentials.py
│   └── logs/ (creé automatiquement par monitor_Nmin.ps1)
├── uploadphotosserver_msp1/    # ESP32-CAM → msp1 (legacy)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── README.md
├── uploadphotosserver_n3pp_1_6_deppsleep/  # ESP32-CAM → n3pp, deep sleep
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── README.md
├── uploadphotosserver_ffp3_1_5_deppsleep/   # ESP32-CAM → ffp3, deep sleep
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── README.md
├── ffp5cs/                    # Contrôleur aquaponie (WROOM/S3) (dossier ordinaire dans firmwires)
├── LVGL_Widgets/              # ESP32-S3 + écran LVGL (pas de serveur dédié)
└── ratata/                     # Kit ZYC0108-EN (UNO + ESP32-CAM)
    ├── platformio.ini          # 8 environnements
    ├── README.md
    ├── src/
    │   ├── 1_Auto_move/, 2_servo_Angle/, … , 6_1_ESP32_Car/, 6_2_Arduino_UNO/, test/
    │   └── (chaque sous-dossier contient main.cpp)
    └── ZYC0108-EN/             # Sources d’origine du kit
```

Les firmwares sont indépendants ; un partage de code commun pourra être ajouté plus tard si besoin. **ffp5cs** est un dossier ordinaire dans firmwires (voir [RECOMMANDATIONS_IOT.md](../RECOMMANDATIONS_IOT.md)).
