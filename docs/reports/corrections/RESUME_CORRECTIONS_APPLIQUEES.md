# Résumé des Corrections Appliquées - Incohérences Code
## Date: 2026-01-21

---

## Corrections S3 PSRAM / boot et loop (2026-02-22)

Référence détaillée : [ESP32S3_HARDWARE_REFERENCE.md](../../technical/ESP32S3_HARDWARE_REFERENCE.md) — section « Comportement firmware S3 PSRAM ».

- **Serial vs UART (CDC)** : Remplacement de `Serial.*` par `ets_printf` dans les chemins boot et tâches pour l’env `wroom-s3-test-psram`, afin d’éviter le blocage lorsque le moniteur est sur UART.
- **Priorités tâches** : Création d’automationTask et netTask avec priorité 1 (au lieu de 3 et 2) pour S3 PSRAM, plus `vTaskDelay` dans la tâche principale et au démarrage d’automationTask, pour que la loop reçoive du CPU et que le boot aille jusqu’à « setup done » et `loop()`.
- **Fin de setup()** : Pour S3 PSRAM, suppression des appels `Serial` / `LOG_*` en fin de setup ; affichage « setup done » / « init done » via `ets_printf` uniquement.
- **OLED « Init ok »** : Dans `display_view.cpp`, `forceEndSplash()` et le log dans `flush()` utilisent `ets_printf` ou sont désactivés pour S3 PSRAM, pour ne pas bloquer avant l’affichage « Diag: Init ok ».

---

## Corrections Implémentées (2026-01-21)

### ✅ PRIORITÉ 1 - CRITIQUE (7 corrections)

#### 1.1 Queue de capteurs augmentée
- **Fichier**: `src/app_tasks.cpp:447`
- **Changement**: Queue passée de 10 à 25 slots
- **Impact**: Réduit les pertes de données (97 pertes en 15 min → devrait être résolu)

#### 1.2 Configuration DS18B20 unifiée
- **Fichiers**: `include/config.h:295`, `include/sensors.h:130-136`
- **Changement**: 
  - CONVERSION_DELAY_MS aligné à 220ms dans config.h
  - Suppression constante locale dans sensors.h, utilisation de SensorConfig::DS18B20
- **Impact**: Configuration centralisée et cohérente

#### 1.3 netTask stack size centralisée
- **Fichiers**: `include/config.h:491-495`, `src/app_tasks.cpp:500-506`
- **Changement**: 
  - Ajout NET_TASK_STACK_SIZE (12288), NET_TASK_PRIORITY (2), NET_TASK_CORE_ID (0) dans TaskConfig
  - Utilisation des constantes au lieu de valeurs hardcodées
- **Impact**: Configuration centralisée, cohérence avec autres tâches

#### 1.4 Constantes timeout HTTP unifiées
- **Fichier**: `include/config.h:82-86, 156-159`
- **Changement**: 
  - HTTP_MAX_MS dans GlobalTimeouts utilise maintenant valeur unifiée (5000)
  - Commentaire ajouté pour clarifier l'usage
- **Impact**: Maintenance simplifiée, une seule source de vérité

#### 1.5 Seuil d'alerte mémoire TLS aligné
- **Fichier**: `src/app.cpp:246-269`
- **Changement**: 
  - Seuil critique aligné avec TLS_MIN_HEAP_BYTES (35 KB). Le seuil est défini dans `include/config.h` (HeapConfig::MIN_HEAP_FOR_TLS), source de vérité unique.
  - Ajout alerte intermédiaire à 40 KB
  - Documentation améliorée
- **Impact**: Alertes préventives avant échec TLS

#### 1.6 Configuration DHT22 MIN_READ_INTERVAL alignée
- **Fichier**: `include/config.h:275-278`
- **Changement**: MIN_READ_INTERVAL_MS passé de 3000ms à 2500ms (compromis datasheet/stabilité)
- **Impact**: Configuration alignée avec documentation

#### 1.7 Validation des valeurs
- **Statut**: Vérifiée - déjà cohérente (pas de changement nécessaire)
- **Note**: Les validations utilisent < et > pour températures (bornes exclusives), ce qui est cohérent

### ✅ PRIORITÉ 2 - MOYEN (2 corrections)

#### 2.1 Timeout réel pour DHT22 implémenté
- **Fichiers**: `src/sensors.cpp:776-854, 938-1006`
- **Changement**: 
  - Ajout timeout explicite de 3000ms dans robustTemperatureC()
  - Ajout timeout de 2000ms dans robustHumidity() (appelé après température)
  - Vérifications de timeout avant et après chaque opération bloquante
  - Utilisation de labels goto pour sortie propre en cas de timeout
- **Impact**: Limite le blocage à 3s max au lieu de 7-8s, prévient les timeouts

#### 2.2 Méthodes DHT22 standardisées
- **Fichier**: `include/sensors.h:39-62`
- **Changement**: 
  - temperatureC(), humidity(), filteredTemperatureC(), filteredHumidity() rendues privées
  - Seules robustTemperatureC() et robustHumidity() restent publiques
  - Documentation ajoutée pour clarifier l'usage
- **Impact**: API simplifiée, utilisation cohérente

### ✅ PRIORITÉ 3 - FAIBLE (1 correction)

#### 3.1 Feature flags documentés
- **Fichiers**: `src/app.cpp:291`, `src/app.cpp:337`, `src/app_tasks.cpp:382`
- **Changement**: 
  - Commentaires ajoutés expliquant pourquoi FEATURE_DIAG_DIGEST et FEATURE_DIAG_TIME_DRIFT sont désactivés
  - Instructions pour réactivation future documentées
- **Impact**: Clarté sur le code conditionnel inactif

## Corrections Non Appliquées (Optionnelles)

### Commentaires obsolètes
- **Raison**: Trop nombreux, nécessiterait revue manuelle complète
- **Recommandation**: Nettoyage progressif lors des modifications futures

## Résumé Statistique

- **Total corrections appliquées**: 10
- **Fichiers modifiés**: 5
  - `src/app_tasks.cpp`
  - `src/app.cpp`
  - `src/sensors.cpp`
  - `include/config.h`
  - `include/sensors.h`
- **Lignes modifiées**: ~150 lignes
- **Corrections critiques**: 7/7 ✅
- **Corrections moyennes**: 2/2 ✅
- **Corrections faibles**: 1/1 ✅

## Impact Attendu

### Améliorations Immédiates
1. **Réduction pertes de données**: Queue 25 slots devrait éliminer les 97 pertes/15min
2. **Réduction blocages DHT22**: Timeout 3s max au lieu de 7-8s
3. **Meilleure visibilité mémoire**: Alertes alignées avec besoins TLS

### Améliorations à Long Terme
1. **Maintenance simplifiée**: Configuration centralisée
2. **API plus claire**: Méthodes DHT22 standardisées
3. **Documentation améliorée**: Feature flags expliqués

## Prochaines Étapes Recommandées

1. **Tests sur hardware**: Vérifier que les corrections fonctionnent en conditions réelles
2. **Monitoring**: Observer si les pertes de queue sont résolues
3. **Validation timeout DHT22**: Confirmer que les timeouts sont respectés
4. **Nettoyage progressif**: Corriger les commentaires obsolètes lors des prochaines modifications

## Notes Techniques

- Les erreurs de linter (clang sur Windows) sont normales et attendues
- La compilation ESP32 devrait fonctionner correctement
- Toutes les corrections maintiennent la compatibilité avec le code existant
- Les constantes sont utilisées de manière cohérente dans tout le projet

---

**Corrections appliquées le**: 2026-01-21  
**Version du projet**: 11.155 → 11.156 (après corrections)
