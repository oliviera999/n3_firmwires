# VERSION.md - GPIO Parsing Unifié v11.68

## Version 11.103 - Cache Cleanup Non Mesuré

**Date**: 2025-11-12  
**Type**: Nettoyage optimisations

### ✨ Points clés
- Suppression des modules `PumpStatsCache`, `EmailBufferPool` et `RuleEvaluationCache` jugés sans impact mesuré.
- Simplification de l’endpoint `/pumpstats` : calcul direct à partir de `SystemActuators` sans couche de cache supplémentaire.
- Nettoyage des dépendances associées (`pump_stats_cache.h/cpp`, `email_buffer_pool.h/cpp`, `rule_cache.h`) et du bootstrap `app.cpp`.

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.103`
- Fichiers modifiés : `src/web_routes_status.cpp`, `src/web_server.cpp`, `src/app.cpp`, `include/project_config.h`
- Fichiers supprimés : `include/pump_stats_cache.h`, `src/pump_stats_cache.cpp`, `include/email_buffer_pool.h`, `src/email_buffer_pool.cpp`, `include/rule_cache.h`

## Version 11.102 - Alert Controller Extraction

**Date**: 2025-11-12  
**Type**: Refactoring automatismes (phase 2)

### ✨ Points clés
- Migration complète de `Automatism::handleAlertsInternal()` vers `AutomatismAlertController::process()` pour clarifier les responsabilités.
- Suppression des `static` locaux remplacés par un état interne du contrôleur (flags de notifications Aqua/Tank) afin d’éviter les effets de bord lors des tests.
- Conservation des protections anti-spam (NVS `floodLast`) et des commandes chauffage directement orchestrées par le contrôleur.

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.102`
- Fichiers modifiés : `include/automatism/automatism_alert_controller.h`, `src/automatism/automatism_alert_controller.cpp`, `include/automatism.h`, `include/project_config.h`

## Version 11.100 - Correction persistance variables config

**Date**: 2025-01-16  
**Type**: Correction bug critique (persistance configuration)

### ✨ Points clés
- Correction du filtrage des valeurs à 0 dans `AutomatismNetwork::applyConfigFromJson()` : les valeurs à 0 ne sont plus ignorées, seules les valeurs négatives sont rejetées
- Parsing amélioré qui gère correctement les strings (comme `"0"` ou `"18"`) en plus des nombres
- Ajout du traitement des clés textuelles dans `GPIOParser::parseAndApply()` pour compatibilité avec `/dbvars/update` et autres sources
- Sauvegarde NVS automatique après application des configurations dans `Automatism::applyConfigFromJson()` pour garantir la persistance

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.100`
- Fichiers modifiés : `src/automatism/automatism_network.cpp`, `src/gpio_parser.cpp`, `src/automatism.cpp`, `include/project_config.h`

### 🐛 Problèmes corrigés
- Variables modifiées à distance (température min, aquarium bas, réserve basse, temps de remplissage, limite débordement) retournaient à leurs valeurs initiales après quelques minutes
- Les valeurs à 0 étaient ignorées, empêchant certaines configurations légitimes
- Les clés textuelles n'étaient pas traitées si elles arrivaient directement (sans passer par GPIO numériques)

## Version 11.92 - Automatism Boot Split

**Date**: 2025-10-29  
**Type**: Refactoring automatismes / stabilisation démarrage

### ✨ Points clés
- `Automatism::begin()` est désormais orchestrée par des helpers privés (`initializeNetworkModule`, `restoreActuatorState`, etc.) pour clarifier les responsabilités et faciliter les futures extractions de services.
- `restoreRemoteConfigFromCache()` retourne un booléen : en cas de données invalides (clé `WakeUp` sans valeur), la synchronisation initiale forceWakeUp est court-circuitée pour reproduire l'ancien comportement de sortie anticipée.
- Ajout des dépendances explicites (`automatism.h`, `config_manager.h`, `power.h`) dans `web_routes_status.cpp` afin de fiabiliser les builds complets.

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.92`
- Fichiers modifiés : `include/automatism.h`, `src/automatism.cpp`, `src/web_routes_status.cpp`, `include/project_config.h`

## Version 11.91 - WebServer Context Helpers

**Date**: 2025-10-29  
**Type**: Refactoring serveur web (phase 2 - groundwork)

### ✨ Points clés
- Introduction de `WebServerContext` pour centraliser les dépendances (Automatism, Mailer, WiFi, capteurs/actionneurs) et les seuils mémoire (`streamMinHeapBytes`, `jsonMinHeapBytes`, `emailMinHeapBytes`).
- Migration des helpers `safeSendJson` et `sendManualActionEmail` vers `WebServerContext` (`ctx.sendJson`, `ctx.sendManualActionEmail`).
- Remplacement progressif des envois JSON (`req->send(...)`) dans `web_server.cpp` par `ctx.sendJson` et usage de `ctx.ensureHeap()`.
- Gestion d'un pointeur global `g_webServerContext` pour les tâches FreeRTOS asynchrones (`/action`) afin d'éviter l'accès direct aux singletons.

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.91`
- Nouveaux fichiers : `include/web_server_context.h`, `src/web_server_context.cpp`
- Fichiers modifiés : `src/web_server.cpp`, `include/web_server.h`

## Version 11.90 - Display Renderers & Session RAII

**Date**: 2025-10-29  
**Type**: Refactoring structure affichage

### ✨ Points clés
- Ajout des modules `MainScreenRenderer`, `CountdownRenderer`, `InfoScreenRenderer` pour isoler le dessin des écrans principaux/secondaires.
- Introduction de `DisplayCache` et `StatusBarRenderer` pour encadrer le cache UI et la barre de statut.
- Nouveau mécanisme RAII `DisplaySession` pour gérer modes immédiats, verrouillage et `_isDisplaying` en toute sécurité.

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.90`
- Nouveaux fichiers : `include/display_cache.h`, `include/status_bar_renderer.h`, `include/display_renderers.h`, `src/status_bar_renderer.cpp`, `src/display_renderers.cpp`
- Fichiers modifiés : `src/display_view.cpp`, `include/display_view.h`, `src/display_text_utils.cpp`

## Version 11.89 - Display Text Helpers

**Date**: 2025-10-29  
**Type**: Préparation refactor / hygiène affichage

### ✨ Points clés
- Extraction des helpers de conversion UTF-8 → CP437 et clipping texte dans `DisplayTextUtils`
- Réutilisation dans `DisplayView::printClipped` et dans les écrans OTA/sleep pour unifier la logique
- Base préparatoire avant découpage du module d’affichage (moins de code statique dans `display_view.cpp`)

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.89`
- Nouveaux fichiers : `include/display_text_utils.h`, `src/display_text_utils.cpp`
- Fichiers modifiés : `src/display_view.cpp`

## Version 11.88 - Feeding Email Buffers

**Date**: 2025-10-29  
**Type**: Optimisation mémoire / hygiène heap

### ✨ Points clés
- `AutomatismFeeding` et `Automatism` construisent désormais les emails de nourrissage dans des buffers `char[]` fixes
- `web_server` et `AutomatismNetwork` réutilisent ces helpers pour éviter les concaténations `String` dans les tâches FreeRTOS
- Logs additionnels sur la taille des messages pour le diagnostic mémoire lors des envois

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.88`
- Fichiers modifiés : `include/automatism.h`, `src/automatism.cpp`, `src/automatism/automatism_feeding.{h,cpp}`,
  `src/web_server.cpp`, `src/automatism/automatism_network.cpp`

## Version 11.87 - Static Timer Pool

**Date**: 2025-10-29  
**Type**: Optimisation runtime / mémoire

### ✨ Points clés
- `TimerManager` migre vers un pool statique de `TimerCallback` C sans `std::function`
- Suppression des allocations dynamiques dans la boucle temps réel pour éviter la fragmentation
- Conservation des métriques (temps d'exécution, compteurs) et des logs de debugging

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.87`
- Fichiers modifiés : `include/timer_manager.h`, `src/timer_manager.cpp`

## Version 11.86 - Modular Bootstrap Refactor

**Date**: 2025-10-29  
**Type**: Refactoring maintenabilité & découplage

### ✨ Points clés
- Extraction d'`app.cpp` en sous-modules `bootstrap_*` et `app_tasks` dédiés
- `AppContext` partagé pour orchestrer l'initialisation depuis `setup()`
- Tâches FreeRTOS et `TaskMonitor` externalisés, instrumentation inchangée
- Préparation à des tests unitaires ciblés sur l'initialisation

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.86`
- Nouveaux fichiers : `include/app_context.h`, `include/app_tasks.h`,
  `include/bootstrap_{network,services,storage}.h`, `src/app_tasks.cpp`,
  `src/bootstrap_{network,services,storage}.cpp`, `src/task_monitor.cpp`
- `src/app.cpp` simplifié à une séquence d'orchestration modulaire

## Version 11.85 - Dual-Core Task Affinity Split

**Date**: 2025-10-29  
**Type**: Stabilité temps réel & watchdog

### ✨ Points clés
- Répartition explicite des tâches FreeRTOS sur les deux cœurs (capteurs/automatisme → core 1, web/OLED → core 0)
- Vérification d'échec `xTaskCreatePinnedToCore` avec log critique en cas d'anomalie au boot
- Suppression du flag `CONFIG_FREERTOS_UNICORE` pour l'environnement `wroom-test`

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.85`
- Fichiers modifiés : `platformio.ini`, `include/project_config.h`, `src/app.cpp`

## Version 11.84 - Sleep Telemetry & Bounded Email Buffers

**Date**: 2025-10-29  
**Type**: Stabilité runtime & hygiène mémoire

### ✨ Points clés
- Instrumentation complète du cycle light-sleep : snapshots `TaskMonitor`, logs `logSleepTransitionStart/End`, wakeup cause, surveillance HWM
- OTA (serveur + ArduinoOTA) enrichis avec métriques tâches et événements `EventLog`
- Emails stockés en buffers fixes (`Automatism`, `AutomatismNetwork`, `AutomatismFeeding`, helpers web) → suppression des `String` dynamiques en parsing config
- `applyConfigFromJson`, `parseRemoteConfig`, `parseTruthy` réécrits en C-style (trim lowercase, `strnlen`) pour réduire la fragmentation

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.84`
- Nouveaux utilitaires : `include/task_monitor.h`, instrumentation dans `src/app.cpp`
- Refactor email : `Automatism`, `AutomatismNetwork`, `AutomatismFeeding`, `web_server`

## Version 11.83 - Network Instrumentation & Controller Split

**Date**: 2025-10-29  
**Type**: Amélioration de stabilité & tooling CI

### ✨ Points clés
- Instrumentation `sendFullUpdate` : backoff exponentiel, métriques heap/latence, garde payload 960B
- Suivi DataQueue : logs heap poussés/poppés, watermark high-water, purge oldest automatique
- Refactor `Automatism` : contrôleurs Refill/Alert/Display dédiés avec `AutomatismRuntimeContext`
- Scripts CI (`scripts/run_ci_checks.ps1`, `scripts/verify_version.ps1`) + schéma JSON documenté

### 🔧 Impact
- Version firmware : `include/project_config.h` → `11.83`
- Documentation : `docs/technical/API_JSON_CONTRACT.md`
- Nouveaux fichiers : scripts CI, contrôleurs dans `src/automatism/`

## Version 11.68 - GPIO Parsing Unifié - Système Simplifié

**Date**: 2025-01-16  
**Type**: Refactoring majeur - Simplification architecture GPIO

### 🎯 Objectif
Remplacer le système complexe de parsing GPIO par une architecture simple et robuste basée sur une source unique de vérité.

### ✨ Nouvelles Fonctionnalités

#### 1. Table de Mapping Centralisée
- **Fichier**: `include/gpio_mapping.h`
- **Responsabilité**: Source unique de vérité pour tous les GPIO
- **Couverture**: GPIO physiques (0-39) + virtuels (100-116)
- **Types**: Actuator, Config (Int/Float/String/Bool)

#### 2. Parsing Unifié
- **Fichier**: `src/gpio_parser.cpp` + `include/gpio_parser.h`
- **Responsabilité**: Parse JSON serveur + applique changements + sauvegarde NVS
- **Simplification**: 6 étapes → 1 appel `GPIOParser::parseAndApply()`

#### 3. Notifications Instantanées
- **Fichier**: `src/gpio_notifier.cpp` + `include/gpio_notifier.h`
- **Responsabilité**: POST instantané des changements locaux
- **Format**: Payload partiel avec GPIO numérique uniquement

#### 4. Serveur Simplifié
- **Fichier**: `ffp3/src/Controller/OutputController.php`
- **Changement**: Format GPIO numérique uniquement (plus de doublons)
- **Compatibilité**: Interface web non impactée (lit BDD directement)

### 🔧 Modifications Techniques

#### ESP32 - Fichiers Modifiés
- `src/automatism.cpp` - **Simplifié** (6 étapes → 1 appel)
- `include/project_config.h` - Version incrémentée à 11.68

#### ESP32 - Fichiers Supprimés
- Code GPIO dynamiques (250 lignes)
- Appels aux fonctions obsolètes (380 lignes)

#### Serveur - Fichiers Modifiés  
- `ffp3/src/Controller/OutputController.php` - Format simplifié

### 📊 Métriques

#### Code Supprimé (~630 lignes)
- `normalizeServerKeys()` - 80 lignes
- `parseRemoteConfig()` - 50 lignes
- `handleRemoteFeedingCommands()` - 70 lignes
- `handleRemoteActuators()` - 180 lignes
- GPIO dynamiques boucle - 250 lignes

#### Code Ajouté (~350 lignes)
- `gpio_mapping.h` - 120 lignes
- `gpio_parser.cpp/h` - 150 lignes
- `gpio_notifier.cpp/h` - 80 lignes

#### Gain Net
- **-280 lignes** de code
- **Architecture simplifiée** (1 parser au lieu de 6 étapes)
- **Maintenabilité améliorée** (source unique de vérité)

### 🚀 Avantages

#### Robustesse
- ✅ Source unique de vérité (pas de désynchronisation)
- ✅ Sauvegarde NVS automatique pour tous les GPIO
- ✅ Parsing simple et direct

#### Performance  
- ✅ POST instantané des changements locaux
- ✅ Priorité locale simplifiée (fenêtre fixe 1 seconde)
- ✅ Pas de normalisation redondante

#### Maintenabilité
- ✅ Table centralisée pour ajout GPIO
- ✅ Types explicites avec validation
- ✅ Code réduit et plus lisible

### 🧪 Tests et Validation

#### Tests Automatisés
- `test_gpio_sync.py`