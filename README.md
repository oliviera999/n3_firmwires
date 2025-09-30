# FFP3CS4 – ESP32 / ESP32-S3

Ce projet pilote capteurs et actionneurs (aquarium, potager, etc.) avec un ESP32.

## Nouveautés stabilité v9.98
- Hostname unique (`ffp3-XXXX`) pour DHCP/DNS et ArduinoOTA (multi-appareils plus simple).
- HTTP plus robuste: succès seulement 2xx–3xx, logs bornés.
- Reconnexion WiFi gérée manuellement, gestion du modem-sleep (ON idle / OFF I/O).
- OTA: affichage enrichi sur OLED, validation image au boot, suspension tâches pendant OTA.
- Watchdog: marges de stack loguées au boot et incluses dans le digest.
- Emails: sujets et corps enrichis avec hostname, taille bornée.
- Automatismes: sécurité aquarium trop plein + clarifications « réserve trop basse ».

Détails: voir `docs/fixes/WATCHDOG_STABILITY_V9_98.md`.

## Description
Système de contrôle automatisé pour aquarium/aquaponie basé sur ESP32-S3 avec monitoring web, alertes email et gestion intelligente des paramètres.

## Fonctionnalités principales

### 🐠 Gestion automatique
- **Alimentation programmée** : 3 repas quotidiens (matin, midi, soir) avec portions configurables
- **Contrôle de niveau** : Remplissage automatique depuis réservoir avec protection anti-débordement
- **Thermorégulation** : Chauffage automatique selon seuil configurable
- **Éclairage** : Contrôle manuel via interface web ou commandes distantes
- **Détection de marée** : Mise en veille intelligente lors des changements d'eau

### 📊 Monitoring
- **Capteurs intégrés** : Température air/eau, humidité, niveaux ultrasoniques, luminosité, tension
- **Interface web** : Dashboard temps réel avec contrôle à distance
- **Alertes email** : Notifications automatiques pour niveaux critiques et changements d'état des pompes (avec raison et origine)
- **Affichage OLED** : Informations locales avec basculement automatique

### 🔧 Configuration
- **Paramètres distants** : Tous les seuils et horaires modifiables via interface web
- **Persistance** : Sauvegarde automatique des états et configurations
- **Sécurité** : Validation des entrées, liste blanche GPIO, protection contre les injections

## Architecture technique

### Composants matériels
- **ESP32-S3** : Microcontrôleur principal avec WiFi/BLE
- **Capteurs** : DHT22 (air), DS18B20 (eau), HC-SR04 (niveaux), LDR (luminosité)
- **Actionneurs** : Relais pour pompes/chauffage/éclairage, servo pour nourriture
- **Affichage** : OLED SSD1306 128x64 I²C

### Architecture logicielle
```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Core 0        │    │   Core 1        │    │   Loop principal │
│                 │    │                 │    │                 │
│ • sensorTask    │    │ • automationTask│    │ • HTTP/OTA      │
│ • Lecture       │    │ • Logique       │    │ • Watchdog      │
│ • Queue         │    │ • HTTP          │    │ • Display       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Sécurité et robustesse
- **Mutex global** : Protection des accès concurrents aux capteurs ultrasoniques
- **Sections critiques** : Protection des variables partagées entre tâches
- **Validation JSON** : Filtrage des clés et limitation de taille
- **Gestion d'erreurs HTTP** : Codes 2xx uniquement considérés comme succès
- **Watchdog** : Protection contre les blocages (5 min timeout)

## Installation

### Prérequis
- PlatformIO IDE ou CLI
- ESP32-S3 compatible
- Bibliothèques Arduino (voir `platformio.ini`)

### Configuration
1. Copier `include/secrets.h.example` vers `include/secrets.h`
2. Configurer les identifiants WiFi et email
3. Ajuster les pins dans `include/pins.h` selon votre câblage
4. Compiler et flasher

```bash
pio run -t upload
pio device monitor
```

## Configuration réseau

### Endpoints serveur
- Profil TEST/DEV:
  - **Données** : `http://iot.olution.info/ffp3/ffp3datas/post-ffp3-data2.php`
  - **Contrôle** : `http://iot.olution.info/ffp3/ffp3control/ffp3-outputs-action2.php`
- Profil PROD:
  - **Données** : `http://iot.olution.info/ffp3/ffp3datas/post-ffp3-data.php`
  - **Contrôle** : `http://iot.olution.info/ffp3/ffp3control/ffp3-outputs-action.php`
- **Heartbeat** : `http://iot.olution.info/ffp3/ffp3datas/heartbeat.php`

### API Key
Utilisée pour l'authentification des requêtes POST (voir `ApiConfig::API_KEY` dans `include/project_config.h`).

## Optimisations récentes

### Architecture Web Dédié (v7.0) - NOUVEAU
- **Tâche web dédiée** : `webTask` avec priorité 3 pour réactivité maximale
- **Hiérarchie optimisée** : Capteurs (4) > Web (3) > Automatisme (2) > Affichage (1)
- **Interface web réactive** : Temps de réponse < 100ms
- **WebSocket temps réel** : Mises à jour instantanées
- **Isolation des responsabilités** : Chaque tâche a sa fonction spécialisée
- **Évolutivité** : Architecture préparée pour les fonctionnalités futures

### Affichage OLED (v6.2)
- **Tâche dédiée** : `displayTask` s'exécute toutes les 250ms (~4 FPS)
- **Architecture FreeRTOS optimisée** : Séparation logique métier / affichage
- **Fluidité maximale** : Décomptes parfaitement fluides en temps réel
- **Priorité basse** : Affichage en arrière-plan pour ne pas perturber les fonctions critiques
- **Cache intelligent** : Mémorisation des états pour éviter les redessins inutiles

### Performance
- **Buffer fixe** : Remplacement des concaténations String par `snprintf()` pour éviter la fragmentation heap
- **Lecture optimisée** : Suppression des doubles lectures de capteurs ultrasoniques
- **Délais CPU** : Ajout de `delayMicroseconds(2)` dans les boucles d'attente pour réduire la charge

### Sécurité
- **Validation JSON** : Filtrage des clés (alphanumérique + underscore/tiret) et limitation de taille (4KB)
- **Protection GPIO** : Liste blanche stricte des pins autorisés
- **Gestion HTTP** : Validation des codes de retour et journalisation des erreurs

### Robustesse
- **Mutex ultrasoniques** : Protection contre les accès concurrents
- **Variables volatiles** : Protection des flags d'alerte partagés entre tâches
- **Sections critiques** : `portENTER_CRITICAL()` pour les opérations atomiques

## Documentation des alertes email

Voir `docs/guides/ALERTES_EMAIL_POMPES.md` pour les sujets, le contenu détaillé, les raisons d'arrêt et l'origine des commandes manuelles (serveur distant ou local).

## Nouveautés et changements (2025-09)

### Web embarqué (API locale)
- Serveur web asynchrone activé avec endpoints:
  - `/` (dashboard LittleFS)
  - `/json` (mesures courantes)
  - `/diag` (diagnostics: heap, uptime, tâches)
  - `/pumpstats` (statistiques pompe réservoir)
  - `/action?relay=light|heater|pumpAqua|pumpTank` (bascule actionneurs)
  - `/dbvars` (variables distantes normalisées côté device)
  - `/dbvars/form` (formulaire simple pour éditer les variables et pousser vers la BDD)
  - `/nvs` et `/nvs.json` (inspecteur NVS; attention: JSON volumineux → possibles timeouts en zone radio chargée)

### OTA modernisé
- Gestionnaire OTA refondu avec `esp_http_client` (buffers 4KB, callbacks de progression/erreur, validation et marquage explicite de la partition de boot).
- Sélection d'artefact à partir de `metadata.json` via schéma `channels[env][model]` avec fallbacks.
- Note: en test, `OTA_UNSAFE_FORCE = true` (dans `include/ota_config.h`) pour tolérer des tailles inconnues; désactiver en production.

### Conformité des envois HTTP
- Le client Web construit un payload POST complet et ordonné (tous les champs attendus par le PHP, champs manquants envoyés vides, ordre stable).

### WiFi et modes
- Mode AP+STA possible (AP de secours SSID `FFP3_XXXX` IP `192.168.4.1`). Le STA tente les SSID connus (BSSID/canal favorisés) avec reconnexion périodique.

### Persistance NVS
- États critiques et variables distantes sont persistés sous les namespaces: `bouffe`, `ota`, `remoteVars`, `rtc`, `diagnostics`, `alerts`, `digest`.
- Écriture optimisée (cache et détection de changements) pour limiter l'usure.

### Partitions et FS
- Environnements (voir `platformio.ini`):
  - `wroom-test` et `wroom-prod`: table `partitions_esp32_wroom_ota_dual.csv` avec LittleFS.
  - `wroom-dev`: table `partitions_esp32_wroom_ota.csv` sans FS (normal → `buildfs` échoue pour dev).
- Astuce: `wroom-prod` laisse plus de marge flash que `wroom-test`.

## Procédure de test rapide

### Flash & FS
```bash
pio run -e wroom-test -t upload --upload-port COMx
pio run -e wroom-test -t uploadfs --upload-port COMx
```

### Vérifications réseau
- STA: IP via logs ou routeur (ex: `10.45.34.32`). AP: `192.168.4.1`.

### API locale (exemples)
```bash
curl http://<ip>/            # dashboard (200 attendu)
curl http://<ip>/json        # mesures
curl http://<ip>/diag        # diagnostics
curl http://<ip>/pumpstats   # stats pompe réservoir
curl "http://<ip>/action?relay=light"   # bascule lumière
curl http://<ip>/dbvars      # variables distantes normalisées
# NVS (attention: volumineux)
curl http://<ip>/nvs.json | head -c 4096
```

### Persistance NVS (exemple ciblé)
```bash
# Activer une case (ex: notifications email) via /dbvars/update
curl -X POST -H "Content-Type: application/x-www-form-urlencoded" \
     -d "mailNotif=checked" http://<ip>/dbvars/update
# Reboot (flash/reboot physique) puis vérifier
curl http://<ip>/dbvars
```

## Notes production
- Préférer `wroom-prod` (marge flash plus confortable) et mettre `OTA_UNSAFE_FORCE = false`.
- Conserver l’ordre et l’exhaustivité des champs lors des POST vers le serveur distant.

## Dépannage
- `/nvs.json` peut expirer si le lien radio est faible; utiliser `/dbvars` ou consulter les logs série pour des clés spécifiques.
- Si `buildfs` échoue en dev: normal (pas de partition FS dans `wroom-dev`).

## Développement

### Structure des fichiers
```
ffp3cs4/
├── include/          # Headers et configurations
├── src/             # Code source principal
├── test/            # Tests unitaires
├── boards/          # Configurations board spécifiques
└── docs/            # Documentation
```

### Tests
```bash
pio test
```

### Debug
- Logs via `Serial` (115200 bauds)
- Niveaux configurables : ERROR, WARN, INFO, DEBUG, VERBOSE
- Monitoring mémoire et performance

## Licence
Projet privé - Tous droits réservés

## Support
Pour toute question ou problème, consulter les logs série et vérifier la connectivité réseau. 