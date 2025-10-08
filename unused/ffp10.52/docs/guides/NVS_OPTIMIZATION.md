# Optimisations NVS pour réduire l'usure de la mémoire flash

## Vue d'ensemble

Ce document décrit les optimisations implémentées pour réduire l'usure de la NVS (Non-Volatile Storage) dans le projet ESP32-S3 FFP3CS4.

## Problème initial

L'ESP32-S3 utilise la mémoire flash pour stocker les données persistantes via la NVS. Chaque écriture contribue à l'usure de la mémoire flash, qui a une durée de vie limitée (~100,000 cycles d'écriture par secteur).

### Utilisations NVS identifiées :
- **Flags de nourrissage** : ~8-10 écritures/jour
- **Horodatage RTC** : ~24-48 écritures/jour  
- **Diagnostics** : ~1-3 écritures/jour
- **Variables distantes** : occasionnel
- **Flag OTA** : rare

## Optimisations implémentées

### 1. Module ConfigManager (`config.cpp`)

#### Détection de changements
- **Cache des valeurs** : Mémorisation des dernières valeurs sauvegardées
- **Comparaison avant écriture** : Sauvegarde uniquement si changement détecté
- **Flag de changement** : `_flagsChanged` pour traquer les modifications

```cpp
void ConfigManager::setBouffeMatinOk(bool value) {
  if (_bouffeMatinOk != value) {  // Changement détecté
    _bouffeMatinOk = value;
    _flagsChanged = true;
  }
}

void ConfigManager::saveBouffeFlags() {
  if (!_flagsChanged) {  // Pas de sauvegarde si inchangé
    Serial.println(F("[Config] Aucun changement détecté - pas de sauvegarde NVS"));
    return;
  }
  // ... sauvegarde NVS
}
```

#### Optimisations spécifiques
- **Variables distantes** : Comparaison JSON avant sauvegarde
- **Flag OTA** : Sauvegarde uniquement si valeur différente
- **Sauvegarde forcée** : `forceSaveBouffeFlags()` pour cas critiques

### 2. Module PowerManager (`power.cpp`)

#### Limitation de fréquence des sauvegardes d'heure
- **Intervalle minimum** : 5 minutes entre sauvegardes
- **Différence de temps** : Sauvegarde si >5 minutes de différence
- **Sauvegarde forcée** : Si différence >1 heure

```cpp
void PowerManager::saveTimeToFlash() {
  // Vérifier si sauvegarde nécessaire
  if ((currentMillis - _lastTimeSave > MIN_TIME_SAVE_INTERVAL) && 
      (abs(currentEpoch - _lastSavedEpoch) > MIN_TIME_DIFF_FOR_SAVE)) {
    // Sauvegarde NVS
  }
}
```

#### Constantes d'optimisation
```cpp
static const unsigned long MIN_TIME_SAVE_INTERVAL = 300000UL; // 5 minutes
static const time_t MIN_TIME_DIFF_FOR_SAVE = 300; // 5 minutes
```

### 3. Module Diagnostics (`diagnostics.cpp`)

#### Optimisation des sauvegardes heap
- **Intervalle minimum** : 5 minutes entre sauvegardes
- **Différence significative** : Sauvegarde si perte >1KB
- **Sauvegarde forcée** : Si perte >10KB

```cpp
if ((now - _lastHeapSave > MIN_HEAP_SAVE_INTERVAL) && 
    (_lastSavedMinHeap - _stats.minFreeHeap > MIN_HEAP_DIFF_FOR_SAVE)) {
  // Sauvegarde NVS
}
```

#### Constantes d'optimisation
```cpp
static const unsigned long MIN_HEAP_SAVE_INTERVAL = 300000UL; // 5 minutes
static const uint32_t MIN_HEAP_DIFF_FOR_SAVE = 1024; // 1KB
```

## Impact des optimisations

### Réduction estimée des écritures NVS

| Module | Avant | Après | Réduction |
|--------|-------|-------|-----------|
| Config (flags) | ~10/jour | ~3-5/jour | 50-70% |
| Power (heure) | ~48/jour | ~12/jour | 75% |
| Diagnostics | ~3/jour | ~1/jour | 67% |
| **Total** | **~61/jour** | **~18/jour** | **70%** |

### Durée de vie estimée
- **Avant optimisation** : ~4.5 ans (100,000 / 61 écritures/jour)
- **Après optimisation** : ~15 ans (100,000 / 18 écritures/jour)

## Utilisation des nouvelles méthodes

### Sauvegarde normale (optimisée)
```cpp
_config.setBouffeMatinOk(true);  // Marque comme changé
_config.saveBouffeFlags();       // Sauvegarde seulement si changé
```

### Sauvegarde forcée (cas critiques)
```cpp
_config.forceSaveBouffeFlags();  // Sauvegarde immédiate
_power.forceSaveTimeToFlash();   // Sauvegarde heure immédiate
```

## Monitoring et debug

### Messages de debug activés
- Détection de changements
- Sauvegardes ignorées
- Intervalles de sauvegarde
- Différences de valeurs

### Exemple de logs
```
[Config] Flag bouffe matin changé: true
[Config] Flags de bouffe sauvegardés dans la flash (changements détectés)
[Power] Sauvegarde d'heure ignorée (diff: 120 s, interval: 180000 ms)
[Diagnostics] minHeap sauvegardé: 245760 bytes
```

## Recommandations

1. **Utiliser les setters optimisés** : Toujours utiliser `setBouffeMatinOk()` au lieu d'accès direct
2. **Sauvegarde forcée** : Utiliser `forceSave*()` seulement pour cas critiques
3. **Monitoring** : Surveiller les logs pour vérifier l'efficacité
4. **Tests** : Valider le comportement en conditions réelles

## Compatibilité

- **Rétrocompatible** : Les anciens appels fonctionnent toujours
- **API inchangée** : Interface publique identique
- **Migration automatique** : Aucune action requise 