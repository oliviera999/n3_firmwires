<!-- f5fcd6f8-368d-4963-b67f-5206a841c643 497da672-41df-4a17-a51a-63d51c250078 -->
# Flash, Monitor & Analyze wroom-test

## Overview

Déployer la version 11.09 sur le wroom-test, capturer 12 minutes de logs, et analyser pour identifier bugs et comportements anormaux (en excluant les données DHT).

## Steps

### 1. Build and Flash Firmware

- Compiler le firmware pour l'environnement `wroom-test`
- Flasher le firmware via PlatformIO
- Commande: `pio run -e wroom-test --target upload`

### 2. Flash Filesystem

- Flasher le système de fichiers LittleFS avec les assets web
- Commande: `pio run -e wroom-test --target uploadfs`

### 3. Monitor for 12 Minutes

- Démarrer le monitoring série (115200 baud)
- Capturer les logs pendant 720 secondes (12 minutes)
- Sauvegarder dans un fichier timestampé
- Commande: Script PowerShell custom pour monitoring de 12 minutes

### 4. Analyze Logs

- Analyser les logs capturés en excluant les données DHT
- Identifier et documenter:
- Erreurs critiques (Guru Meditation, Panic, Brownout)
- Warnings watchdog et timeouts
- Problèmes mémoire (heap/stack)
- Anomalies réseau (WiFi, WebSocket, HTTP)
- Comportements inhabituels des capteurs (sauf DHT)
- Éléments remarquables dans le fonctionnement
- Produire un rapport d'analyse détaillé

### To-dos

- [ ] Build and flash firmware to wroom-test
- [ ] Flash LittleFS filesystem with web assets
- [ ] Monitor serial output for 12 minutes and save logs
- [ ] Analyze logs excluding DHT data, identify bugs and anomalies