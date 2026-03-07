# Firmwires – Projets ESP32 et Arduino (PlatformIO)

*Contexte : [salle aérée n³](https://n3.olution.info) · Données : [iot.olution.info](https://iot.olution.info). Voir [README racine](../README.md), [RECOMMANDATIONS_IOT](../RECOMMANDATIONS_IOT.md), [inventaire des appareils](../docs/inventaire_appareils.md).*

Dépôt regroupant **plusieurs firmwares** : deux projets principaux ESP32 (serre/aquaponie, station météo), trois projets ESP32-CAM d’envoi de photos vers un serveur web, et le kit **ratata** (ZYC0108-EN) avec exemples Arduino UNO et ESP32-CAM. Chaque projet (ou environnement) est compilable avec **PlatformIO**.

## Projets

| Projet | Dossier | Carte | Description |
|--------|---------|-------|-------------|
| **N3PhasmesProto (n3pp)** | `n3pp4_2/` | ESP32 dev | Contrôle serre / aquaponie : température/humidité air, 4 capteurs humidité sol, pompe, luminosité, mails d’alerte, serveur web, NTP, OLED, deep sleep. |
| **MeteoStationPrototype (msp)** | `msp2_5/` | ESP32 dev | Station météo + tracker solaire : 2× DHT, humidité sol, pluie, DS18B20, 4 LDR, 2 servos, relais, mail, serveur web, NTP, OLED. |
| **Upload Photos MSP1** | `uploadphotosserver_msp1/` | ESP32-CAM | Capture photo, envoi HTTP POST vers iot.olution.info/msp1gallery/. OTA, pas de deep sleep. |
| **Upload Photos N3PP** | `uploadphotosserver_n3pp_1_6_deppsleep/` | ESP32-CAM | Envoi vers n3ppgallery (à la racine serveur) ; deep sleep 600 s ; EEPROM, SD_MMC. |
| **Upload Photos FFP3** | `uploadphotosserver_ffp3_1_5_deppsleep/` | ESP32-CAM | Envoi vers ffp3 gallery ; deep sleep 600 s. |
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
cd n3pp4_2
pio run
pio run -t upload
pio device monitor

# MeteoStation (msp)
cd msp2_5
pio run
pio run -t upload
pio device monitor

# Upload photos (ESP32-CAM) – ex. MSP1
cd uploadphotosserver_msp1
pio run
pio run -t upload
pio device monitor

# Ratata – compiler un environnement (ex. 1_auto_move ou 6_1_esp32_car)
cd ratata
pio run -e 1_auto_move
pio run -e 1_auto_move -t upload
pio device monitor -e 1_auto_move
```

Adapter `upload_port` et `monitor_port` dans chaque `platformio.ini` (ex. `COM3`, `COM4`, `/dev/ttyUSB0`) selon ta machine.

## Scripts de monitoring et erase-flash (racine firmwires)

À la racine de `firmwires/` sont disponibles des scripts **génériques** pour tous les projets (sauf **ratata** et **LVGL_Widgets**) :

| Script | Rôle |
|--------|------|
| `monitor_Nmin.ps1` | Capture du moniteur série pendant N secondes (défaut 5 min). Log créé dans le dossier du projet. |
| `erase_flash_monitor.ps1` | Workflow complet : erase flash → flash firmware (et LittleFS pour ffp5cs si applicable) → monitoring N min → analyse optionnelle si le projet le fournit (ex. ffp5cs). |
| `scripts/Release-ComPort.ps1` | Libération du port COM (processus moniteur). Sourcé par les scripts ci‑dessus. |

**Exemples (depuis `firmwires/`) :**

```powershell
# Monitoring 5 min sur n3pp4_2
.\monitor_Nmin.ps1 -Project n3pp4_2

# Erase + flash + monitor 5 min sur ffp5cs (env wroom-test), port COM4
.\erase_flash_monitor.ps1 -Project ffp5cs -Port COM4

# Erase + flash + monitor 10 min sur msp2_5, sans rebuild
.\erase_flash_monitor.ps1 -Project msp2_5 -DurationMinutes 10 -SkipBuild

# ffp5cs en prod : pas de LittleFS
.\erase_flash_monitor.ps1 -Project ffp5cs -Environment wroom-prod -SkipUploadFs
```

**Projets supportés** : `n3pp4_2`, `msp2_5`, `uploadphotosserver_msp1`, `uploadphotosserver_n3pp_1_6_deppsleep`, `uploadphotosserver_ffp3_1_5_deppsleep`, `ffp5cs`. Pour **ffp5cs**, les scripts dédiés dans `ffp5cs/` (ex. `erase_flash_fs_monitor_5min_analyze.ps1`, `monitor_5min.ps1`) restent utilisables et offrent l’analyse complète (analyse_log, rapport diagnostic).

## Configuration des ports série

Chaque `platformio.ini` définit `upload_port` et `monitor_port` (souvent `COM3` par défaut). Pour une machine donnée, modifier ces valeurs dans le fichier du projet concerné. Selon l’environnement, il est possible d’utiliser des variables d’environnement (ex. `UPLOAD_PORT`, `MONITOR_PORT`) si votre configuration PlatformIO ou vos scripts les prennent en charge ; à défaut, adapter directement les lignes dans le `platformio.ini` du projet.

## WiFi (ESP32-CAM MSP1 et N3PP)

Les firmwares **uploadphotosserver_msp1** et **uploadphotosserver_n3pp_1_6_deppsleep** utilisent une logique WiFi alignée sur ffp5cs (simplifiée) :

- **Credentials** : copier `credentials.h.example` vers `credentials.h` à la racine du projet (non versionné) et remplir la liste `WIFI_LIST[]` (paires SSID / mot de passe). Plusieurs réseaux sont supportés. Sans `credentials.h`, la compilation échoue ; le `platformio.ini` inclut `$PROJECT_DIR` pour trouver ce fichier.
- **Comportement** : au démarrage, scan des réseaux, association des credentials aux AP visibles, tri par RSSI (meilleur signal en premier), puis tentatives de connexion avec BSSID et canal si le réseau est visible (timeout 5 s par tentative, une retry sans BSSID en cas d’échec). Délai 250 ms entre chaque réseau. Pas d’AP de secours.
- **MSP1** : en cas de déconnexion, tentative de reconnexion périodique (toutes les 60 s) dans `loop()`.
- **N3PP** : deep sleep à chaque cycle ; au réveil, `Wificonnect()` est rappelé dans `setup()`.

## Structure

```
firmwires/
├── .gitignore
├── README.md
├── RECOMMANDATIONS.md
├── RAPPORT_ANALYSE.md
├── monitor_Nmin.ps1            # Monitoring N min (tous projets sauf ratata, LVGL)
├── erase_flash_monitor.ps1     # Erase + flash + monitor (tous projets sauf ratata, LVGL)
├── scripts/
│   └── Release-ComPort.ps1      # Libération port COM (partagé)
├── n3pp4_2/                    # N3PhasmesProto (ESP32)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── n3pp4_2.ino
├── msp2_5/                     # MeteoStationPrototype (ESP32)
│   ├── platformio.ini
│   ├── src/main.cpp
│   ├── lib/, include/, test/
├── uploadphotosserver_msp1/    # ESP32-CAM → msp1 gallery
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
