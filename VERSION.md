# Historique des versions - FFP5CS

Ce fichier documente l'historique des versions du projet FFP5CS (ESP32 Aquaponie Controller).

## Format de version

Le format de version suit le schéma `MAJOR.MINOR` :
- **MAJOR** (+1.00) : Nouvelles fonctionnalités majeures, refactoring important, changements d'API
- **MINOR** (+0.01) : Corrections de bugs, optimisations mineures, ajustements

La version est définie dans `include/config.h` dans `ProjectConfig::VERSION`.

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
- Règles de développement : `.cursorrules`
- Rapport d'adéquation : `docs/reports/RAPPORT_ADEQUATION_CURSORRULES_CODE.md`
