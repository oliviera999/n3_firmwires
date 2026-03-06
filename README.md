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

## Configuration des ports série

Chaque `platformio.ini` définit `upload_port` et `monitor_port` (souvent `COM3` par défaut). Pour une machine donnée, modifier ces valeurs dans le fichier du projet concerné. Selon l’environnement, il est possible d’utiliser des variables d’environnement (ex. `UPLOAD_PORT`, `MONITOR_PORT`) si votre configuration PlatformIO ou vos scripts les prennent en charge ; à défaut, adapter directement les lignes dans le `platformio.ini` du projet.

## Structure

```
firmwires/
├── .gitignore
├── README.md
├── RECOMMANDATIONS.md
├── RAPPORT_ANALYSE.md
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
├── ffp5cs/                    # Contrôleur aquaponie (WROOM/S3) — submodule Git
└── ratata/                     # Kit ZYC0108-EN (UNO + ESP32-CAM)
    ├── platformio.ini          # 8 environnements
    ├── README.md
    ├── src/
    │   ├── 1_Auto_move/, 2_servo_Angle/, … , 6_1_ESP32_Car/, 6_2_Arduino_UNO/, test/
    │   └── (chaque sous-dossier contient main.cpp)
    └── ZYC0108-EN/             # Sources d’origine du kit
```

Les firmwares sont indépendants ; un partage de code commun pourra être ajouté plus tard si besoin. **ffp5cs** est un submodule Git (voir [RECOMMANDATIONS_IOT.md](../RECOMMANDATIONS_IOT.md)).
