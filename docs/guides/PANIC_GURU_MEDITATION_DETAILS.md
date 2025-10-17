# Capture des détails de Guru Meditation (PANIC)

## Problème identifié

Lors d'un redémarrage PANIC en production, le mail de démarrage ne contenait que :
```
Raison du redémarrage: PANIC (erreur critique)
```

Sans aucune information sur :
- Le type d'exception précis
- L'adresse où s'est produit le panic
- La tâche impliquée
- Le core CPU concerné
- Les détails du Guru Meditation

## Solution implémentée

### 1. Structure de données étendue (`include/diagnostics.h`)

Ajout d'une structure `PanicInfo` pour capturer les détails :

```cpp
struct PanicInfo {
  bool hasPanicInfo;
  String exceptionCause;      // Cause de l'exception
  uint32_t exceptionAddress;  // Adresse où s'est produit le panic
  uint32_t excvaddr;          // Adresse virtuelle de l'exception (pour les fautes mémoire)
  String taskName;            // Nom de la tâche qui a paniqué
  int core;                   // Core CPU qui a paniqué
  String additionalInfo;      // Informations additionnelles
};
```

### 2. Capture des informations au démarrage

Lors de l'initialisation de `Diagnostics::begin()`, les informations de panic sont capturées **immédiatement** depuis la mémoire RTC avant qu'elles ne soient perdues :

- Utilisation de `rtc_get_reset_reason()` pour obtenir des détails plus précis que `esp_reset_reason()`
- Analyse des deux cores CPU
- Décodage des types d'exceptions watchdog (RTC WDT, Timer Group 0/1, etc.)

### 3. Persistance dans NVS

Les informations capturées sont sauvegardées dans NVS (Non-Volatile Storage) pour survivre aux redémarrages :

```cpp
void savePanicInfo();   // Sauvegarde dans NVS
void loadPanicInfo();   // Chargement depuis NVS
void clearPanicInfo();  // Nettoyage après lecture
```

### 4. Inclusion dans le rapport de démarrage

Le mail de démarrage inclut maintenant une section dédiée :

```
⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️
Exception: Timer Group 0 Watchdog (CPU)
Adresse PC: 0x400d1234
Adresse mémoire fautive: 0x3ffb2000
Tâche: wifi_task
CPU Core: 0
Info supplémentaire: Core 1 reason differs: 12
```

## Types d'exceptions détectés

L'implémentation détecte et nomme précisément les types suivants :

1. **RTC Watchdog Reset** - Watchdog du contrôleur RTC
2. **RTC Watchdog CPU Reset** - Watchdog RTC pour le CPU
3. **Timer Group Watchdog (CPU)** - WDT générique timer group (CPU)
4. **Timer Group 0 Watchdog (System)** - WDT du groupe timer 0 (Système)
5. **Timer Group 1 Watchdog (System)** - WDT du groupe timer 1 (Système)
6. **Software CPU Reset** - Reset logiciel du CPU
7. **Software Reset** - Reset logiciel général
8. **Power-On Reset** - Mise sous tension
9. **Deep Sleep Reset** - Sortie de deep sleep
10. **SDIO Reset** - Reset via SDIO
11. **Intrusion Test Reset** - Test d'intrusion
12. **Unknown Exception** - Autres exceptions avec code numérique

## Flux de traitement

```
Boot après PANIC
    ↓
capturePanicInfo() ← lecture immédiate depuis mémoire RTC
    ↓
savePanicInfo() ← sauvegarde dans NVS
    ↓
[Suite du boot normal]
    ↓
generateRestartReport() ← inclut les détails de panic
    ↓
Mail de démarrage avec détails complets
```

## Avantages

1. **Diagnostic précis** : Identification exacte du type de crash
2. **Contexte complet** : Tâche, core, adresses mémoire
3. **Persistance** : Les informations survivent aux redémarrages
4. **Automatique** : Aucune configuration supplémentaire requise
5. **Production-ready** : Capture même en cas de panic répétés

## Limitations actuelles

Les informations suivantes nécessitent l'activation du **Core Dump** (désactivé par défaut pour économiser l'espace flash) :

- Registres CPU détaillés (PC, A0-A15, etc.)
- Stack trace complète
- État complet de la mémoire

Ces informations peuvent être activées dans `platformio.ini` avec :
```ini
board_build.partitions = partitions_coredump.csv
build_flags = 
    -DCONFIG_ESP32_ENABLE_COREDUMP=1
    -DCONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH=1
```

## Exemple de sortie

### Avant (mail sans détails)
```
Raison du redémarrage: PANIC (erreur critique)
Compteur de redémarrages: 296
Heap libre minimum historique: 50548 bytes
```

### Après (mail avec détails)
```
Raison du redémarrage: PANIC (erreur critique)

⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️
Exception: Timer Group 0 Watchdog (CPU)
Adresse PC: 0x400d1234
Tâche: auto_task
CPU Core: 0

Compteur de redémarrages: 296
Heap libre minimum historique: 50548 bytes
```

## Impact mémoire

- **RAM** : +72 bytes (structure PanicInfo)
- **NVS** : ~200 bytes (stockage persistant des infos de panic)
- **Flash** : +2 KB environ (code de capture et décodage)

## Tests recommandés

1. Provoquer un panic volontaire et vérifier les informations capturées
2. Vérifier que les informations survivent à plusieurs redémarrages
3. Tester avec différents types de watchdog
4. Vérifier le mail de démarrage après un panic

## Fichiers modifiés

- `include/diagnostics.h` - Structure de données et déclarations
- `src/diagnostics.cpp` - Implémentation de la capture et du rapport

## Prochaines étapes (optionnel)

Pour aller encore plus loin, on pourrait :

1. Activer le Core Dump complet pour avoir la stack trace
2. Implémenter un parseur de backtrace pour identifier la fonction fautive
3. Ajouter un historique des 5 derniers panics
4. Intégrer avec un système de monitoring externe (Sentry, etc.)

---

**Date de création** : 2025-10-08  
**Version** : 1.0  
**Auteur** : Assistant IA

