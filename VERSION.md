# Historique des versions - FFP5CS

Ce fichier documente l'historique des versions du projet FFP5CS (ESP32 Aquaponie Controller).

## Format de version

Le format de version suit le schéma `MAJOR.MINOR` :
- **MAJOR** (+1.00) : Nouvelles fonctionnalités majeures, refactoring important, changements d'API
- **MINOR** (+0.01) : Corrections de bugs, optimisations mineures, ajustements

La version est définie dans `include/config.h` dans `ProjectConfig::VERSION`.

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
