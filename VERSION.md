# Historique des versions - FFP5CS

Ce fichier documente l'historique des versions du projet FFP5CS (ESP32 Aquaponie Controller).

## Format de version

Le format de version suit le schéma **MAJOR.MINOR** (ex. 12.15, deux chiffres pour la partie mineure) :
- **MAJOR** : Nouvelles fonctionnalités majeures, refactoring important, changements d'API (incrémenter le premier nombre).
- **MINOR** : Corrections de bugs, optimisations mineures, ajustements (incrémenter les décimales, ex. 12.14 → 12.15).

La version est définie dans `include/config.h` (`ProjectConfig::VERSION`). L’historique est tenu dans ce fichier `VERSION.md`.

---

## Version 12.20 - 2026-03-04

### netRPC : pool 16/10 + timeouts RPC réduits

- **Pool netRPC** : S3 12→16, WROOM 8→10 pour réduire la saturation sous charge (run 5 min 15:13 : used=12/12).
- **Timeouts RPC** : `FETCH_REMOTE_STATE_RPC_TIMEOUT_MS` 30 s→20 s ; `HTTP_POST_RPC_TIMEOUT_MS` S3 20 s→16 s, WROOM 26 s→22 s. Libération des slots plus tôt en cas de serveur lent.
- Déploiement OTA wroom-prod et wroom-s3-test.

---

## Version 12.19 - 2026-03-04

### netRPC : pool agrandi + throttle quand quasi plein

- **Pool netRPC** : taille augmentée (WROOM 6→8, S3 8→12) et file `g_netQueue` alignée pour limiter les échecs « Pool plein » et « POST échoué (file pleine…) » quand le serveur est lent ou la charge élevée.
- **Throttle Sync** : dans `AutomatismSync::update()`, si le pool est quasi plein (`used >= size - 2`), on diffère replay et sendFullUpdate pour éviter de saturer ; log « POST différé: pool netRPC quasi plein » au plus une fois par minute.
- **API** : `AppTasks::netRequestPoolUsedCount()` et `AppTasks::netRequestPoolSize()` exposées pour le throttle (et diagnostic).

---

## Version 12.18 - 2026-03-04

### Maintenance

- Incrément version pour déploiement OTA (wroom-prod, wroom-s3-test).

---

## Version 12.17 - 2026-03-04

### OTA au boot prioritaire

- **netTask** : la vérification OTA au boot s’exécute **avant** la récupération de la config serveur (première opération HTTPS après WiFi + délai TLS). Plus de dépendance à `bootServerReachable` : l’OTA est tenté dès que WiFi est connecté et la heap suffisante, ce qui renforce la faisabilité de la mise à jour au démarrage. Comportement offline-first et timeouts inchangés.

---

## Version 12.16 - 2026-03-04

### Corrections run 5 min (wroom-s3-test)

- **Stack autoTask (P2)** : `AUTOMATION_TASK_STACK_SIZE` porté à 12 KB pour BOARD_S3 uniquement (10 KB conservé pour WROOM) ; évite les alertes « Stack > 70 % » (HWM 78 % observé).
- **Mail skip heap (P3)** : `MIN_HEAP_BLOCK_FOR_MAIL_TLS` abaissé à 32000 pour S3 pour accepter un bloc observé à 32756 sans risque TLS.
- **OTA heap (P4)** : si heap insuffisante au boot, OTA reporté avec log « retry dans 60 s » ; retry automatique une fois après 60 s dans netTask (sans boucle bloquante). Log explicite dans ota_manager : « [OTA] Reporté au prochain cycle (heap) ».
- **Doc coredump** : procédure `docs/technical/COREDUMP_PANIC_S3.md` pour extraire et analyser le coredump après un PANIC sur S3 (offset partition 0xE00000).

---

## Version 12.15 - 2026-03-03

### Maintenance

- Vérification et mise à jour de la documentation (plan doc) : cohérence VERSION.md / config.h, règles projet, liens, clés NVS.

---

## Version 12.14 - 2026-03-03

### Maintenance

- Incrément version, build wroom-s3-test, publication OTA.

---

## Version 12.13 - 2026-03-03

### Maintenance

- Incrément version après flash wroom-s3-test et déploiement OTA.

---

## Version 12.12 - 2026-03-03

### OTA distant pour wroom-s3-test

- **OTA au boot et périodique (2 h) activés pour wroom-s3-test** : la condition `!defined(PROFILE_TEST)` est assouplie en `!defined(PROFILE_TEST) || defined(BOARD_S3)`, afin que les envs S3 (wroom-s3-test, wroom-s3-test-psram, etc.) bénéficient de l’OTA automatique. wroom-test (WROOM + PROFILE_TEST) conserve l’OTA manuel uniquement (évite blocage netTask > TWDT). Doc `docs/technical/OTA_PUBLISH.md` mise à jour.

---

## Version 12.05 - 2026-02-21

### Maintenance

- Incrément version (config.h + VERSION.md), commit et push GitHub

---

## Version 12.02 - 2026-02-19

### Corrections

- **Publication données après plusieurs heures** : exécution de la synchro (poll + POST) aussi en branche timeout de la queue capteurs, avec les dernières lectures (`s_lastReadings`), pour ne pas couper la publication lorsque des timeouts se répètent (hypothèse H1).

---

## Version 11.193 - 2026-02-03

### Maintenance

- Incrément version (config.h + VERSION.md), commit et push GitHub

---

## Version 11.190 - 2026-02

### Points à surveiller

- HTTP GET/Heartbeat 5s ; **POST 8s** (dérogation `HTTP_POST_TIMEOUT_MS`).
- NVS remove idempotent, servo pin valide

---

## Version 11.167 - 2026-01-30

### Maintenance

- ✅ **Build tous environnements** : wroom-prod, wroom-test, wroom-beta, wroom-tls-test
- ✅ **Version documentée** : 11.167 dans config.h et VERSION.md
- ✅ **Push GitHub** : commit et push après validation build

---

## Version 11.155 - 2026-01-17

### Corrections critiques

- ✅ **Correction duplication constantes EPOCH** : Utilisation d'alias dans `SleepConfig` vers `SystemConfig` pour éviter duplication
- ✅ **Documentation version** : Ajout de la version 11.155 dans l'historique

---

## Version 11.156 - 2026-01-25

### Maintenance

- ✅ **Incrémentation version** : Passage à la version 11.156

---

## Version 11.138 - 2026-01-15

### Réseau / stabilité

- ✅ **HTTP au boot** : suppression du GET distant pendant l'init NTP pour éviter un panic LWIP

---

## Version 11.137 - 2026-01-15

### Mail / Filesystem

- ✅ **ESP Mail Client** : flash FS désactivé pour éviter les erreurs `spiffs`

---

## Version 11.136 - 2026-01-15

### Stockage LittleFS

- ✅ **Montage stable** : base path `/littlefs` explicite pour éviter l'échec d'enregistrement VFS
- ✅ **Montage partagé** : label `littlefs` conservé sur tous les points d'entrée

---

## Version 11.135 - 2026-01-15

### Stockage LittleFS

- ✅ **Montage fiable** : label `littlefs` explicite dans tous les modules
- ✅ **Format cohérent** : remontage après format avec le bon label

---

## Version 11.134 - 2026-01-14

### Remplissage manuel (pompe réserve)

- ✅ **Mode manuel fiable** : timer + décompte initialisés au démarrage
- ✅ **Commande distante robuste** : front montant/descendant pour éviter les retriggers
- ✅ **Sécurité d'état** : ignore les commandes redondantes

---

## Version 11.133 - 2026-01-14

### Nourrissage manuel

- ✅ **Reset centralisé** : fin de phase gérée dans le cœur (indépendant OLED)
- ✅ **Sync flags distants** : remise à 0 après commandes serveur (cooldown)
- ✅ **OLED** : suppression du reset métier pour éviter dépendance écran

---

## Version 11.132 - 2026-01-14

### OLED

- ✅ **Ecran relais** : ajout des heures/temps de nourrissage et des seuils (aq, tank, chauffage, flood)

---

## Version 11.130 - 2026-01-14

### Stabilité & robustesse (prod)

#### Correctifs critiques
- ✅ **WiFi** : suppression du double pilotage (un seul `wifi.loop()` dans `src/app.cpp`)
- ✅ **Capteurs** : prise en compte explicite des `NaN` (évite propagation silencieuse)
- ✅ **Serial en PROD** : désactivation sûre via stub `NullSerial` (réduction flash significative)
  - wroom-prod flash : ~96.9% → ~94.6% après compilation

#### Correctifs complémentaires
- ✅ **Digest email** : clés NVS harmonisées (`digest_last_seq`, `digest_last_ms`) pour éviter la perte d'état
- ✅ **/dbvars** : séparation des heures de nourrissage et des flags d'état (`bouffeMatinOk`, etc.)
- ✅ **Pompe réservoir** : statistiques fiabilisées (runtime courant + compteur d'arrêts)
- ✅ **NVS** : `forceFlush()` écrit directement pour éviter la ré-entrée
- ✅ **WiFi** : connexion manuelle centralisée via `WifiManager::connectTo()`

#### Nettoyage / cohérence
- ✅ **WatchdogManager** : suppression du module non utilisé (réduction surface de bugs + taille)
- ✅ **Config** : headers historiques `include/config/*` convertis en wrappers vers `include/config.h` (évite divergence)

#### Tests
- ✅ **Tests natifs (PlatformIO env:native)** : remise en état des suites unitaires + mocks Arduino minimalistes
  - `RateLimiter` et `TimerManager` passent sur hôte

---

## Version 11.129 - 2026-01-13

### Core Dump - Outils d'extraction et analyse

**Amélioration du système de debugging avec core dump**

#### Nouvelles fonctionnalités
- ✅ **Outils d'extraction** : Création de scripts Python pour extraire les core dumps depuis la flash
  - `tools/coredump/extract_coredump.py` - Extraction depuis la partition flash
  - `tools/coredump/analyze_coredump.py` - Analyse avec stack trace
  - `tools/coredump/coredump_tool.py` - Outil intégré (extraction + analyse)
  - Documentation complète dans `tools/coredump/README.md`

#### Corrections de configuration
- ✅ **Build flags harmonisés** : Ajout de `CONFIG_ESP_COREDUMP_ENABLE=1` dans `wroom-prod`
  - Cohérence entre environnements test et production
  - Fichier modifié : `platformio.ini`
  
- ✅ **Offsets de partition corrigés** : Remplacement des offsets automatiques par valeurs explicites
  - Calcul manuel des offsets pour éviter les erreurs
  - Fichier modifié : `partitions_esp32_wroom_ota_coredump.csv`
  - Offsets: app1=0x1B0000, littlefs=0x350000, coredump=0x3F0000

#### Documentation
- ✅ **Guide d'utilisation** : Création de `docs/guides/COREDUMP_USAGE.md`
  - Procédures d'extraction et d'analyse
  - Interprétation des résultats
  - Dépannage
- ✅ **Rapport d'analyse** : Création de `docs/reports/RAPPORT_ANALYSE_PARTITION_COREDUMP.md`
  - Analyse complète de la configuration core dump
  - Problèmes identifiés et solutions
  - Plan d'action et recommandations

---

## Version 11.128 - 2026-01-13

### Corrections de conformité .cursorrules

**Conformité améliorée de 75% à 90%+**

#### Corrections critiques
- ✅ **WebSocket** : Ajout de vérification `connectedClients() > 0` avant chaque envoi (8 occurrences)
  - Évite les envois inutiles quand aucun client connecté
  - Fichier modifié : `include/realtime_websocket.h`
  
- ✅ **Gestion mémoire** : Refactorisation de `buildSystemInfoFooter()` dans `mailer.cpp`
  - Remplacement de `String` par buffer statique `char[]` avec `snprintf()`
  - Réduction de la fragmentation mémoire
  - Fichier modifié : `src/mailer.cpp`

#### Corrections importantes
- ✅ **Conventions de nommage** : Uniformisation du préfixe `g_` pour variables globales
  - `realtimeWebSocket` → `g_realtimeWebSocket`
  - `autoCtrl` → `g_autoCtrl`
  - Fichiers modifiés : Tous les fichiers utilisant ces variables
  
- ✅ **Watchdog et stabilité** : Remplacement de `delay(5000)` par `vTaskDelay()` dans `power.cpp`
  - Conforme à la règle : "Utiliser `vTaskDelay()` au lieu de `delay()` dans les tâches"
  - Fichier modifié : `src/power.cpp`

#### Documentation
- ✅ Création du fichier `VERSION.md` pour documenter les changements de version

---

## Template pour les prochaines versions

```markdown
## Version X.XX - YYYY-MM-DD

### Nouvelles fonctionnalités
- Description de la nouvelle fonctionnalité

### Corrections
- Description de la correction de bug

### Améliorations
- Description de l'amélioration/optimisation

### Changements techniques
- Description des changements techniques (refactoring, architecture, etc.)

### Notes
- Notes importantes pour cette version
```

---

## Notes importantes

- **OBLIGATOIRE** : Incrémenter la version dans `include/config.h` avant chaque déploiement
- **OBLIGATOIRE** : Faire un monitoring de 90 secondes après chaque déploiement
- **OBLIGATOIRE** : Analyser les logs complets après chaque déploiement
- Documenter chaque changement de version dans ce fichier
- Faire des commits atomiques avec numéro de version (ex: "v11.127: Fix WebSocket checks")

---

## Références

- Configuration de version : `include/config.h` → `ProjectConfig::VERSION`
- Règles de développement : `.cursor/rules/` (ex. règles cœur FFP5CS)
- Rapport d'adéquation : `docs/reports/RAPPORT_ADEQUATION_CURSORRULES_CODE.md`
