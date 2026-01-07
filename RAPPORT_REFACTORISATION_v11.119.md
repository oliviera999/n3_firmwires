# Rapport de Refactorisation - Simplification & Nettoyage (v11.119)

**Date** : 7 Janvier 2026
**Objectif** : Éliminer la suringénierie, le code mort et moderniser la base de code suite à l'analyse du 13/11/2025.

---

## 1. Synthèse des Actions

| Catégorie | État | Détails des modifications |
| :--- | :---: | :--- |
| **Code Mort** | ✅ Terminé | Suppression de `JsonPool`, `PSRAMOptimizer` et méthodes capteurs obsolètes. |
| **Monitoring** | ✅ Terminé | Simplification drastique de `TimeDriftMonitor` et `TaskMonitor`. |
| **Configuration** | ✅ Terminé | Migration complète vers `config.h`. Disparition de `project_config.h`. |
| **Modernisation** | ✅ Terminé | Passage à `ArduinoJson 7` (`JsonDocument`) sur l'ensemble du projet. |

## 2. Détails Techniques

### A. Suppression des "Optimizers" Inutiles
L'analyse a révélé que certaines classes d'optimisation ajoutaient de la complexité sans gain réel sur l'ESP32-WROOM.
- **`JsonPool`** : Supprimé. Remplacé par une allocation standard (`new`/`delete`) qui est gérée efficacement par l'OS. Gain en lisibilité et fiabilité (plus de risques de deadlock sur le pool).
- **`PSRAMOptimizer`** : Supprimé/Vidée. Transformé en stub minimal pour compatibilité, car le WROOM n'a pas de PSRAM.

### B. Simplification du Monitoring
Le monitoring était trop verbeux et complexe pour un système de production.
- **`TimeDriftMonitor`** : Réduit de ~470 à ~90 lignes. Suppression des calculs de dérive PPM et de l'historique NVS complexe. Ne garde que la synchronisation NTP essentielle.
- **`TaskMonitor`** : Réduit de ~150 à ~70 lignes. Se concentre uniquement sur le relevé du "High Water Mark" (consommation max de pile) sans logique de diff complexe.

### C. Nettoyage des Capteurs (`sensors.cpp`)
Suppression de la "sédimentation" des méthodes de filtrage.
- Suppression : `robustTemperatureC()`, `ultraRobustTemperatureC()`, `readRobustFiltered()`.
- Standardisation : Utilisation unique de `getTemperatureWithFallback()` et `readReactiveFiltered()`.

### D. Modernisation ArduinoJson
Migration de la version 6 à la version 7 pour éliminer les avertissements de compilation et assurer la pérennité.
- Remplacement de `DynamicJsonDocument` par `JsonDocument`.
- Adaptation des appels (`containsKey()` remplacé par `doc["key"].is<T>()` ou checks implicites).

## 3. Impact sur les Ressources

| Métrique | Avant | Après | Gain |
| :--- | :--- | :--- | :--- |
| **Flash** | ~2.26 MB | ~2.25 MB | **~10 KB** |
| **RAM (Build)** | ~74.7 KB | ~74.6 KB | **Stable** |
| **Lignes de code** | - | - | **~800 lignes supprimées** |

## 4. Conclusion

Le firmware **FFP5CS v11.119** est maintenant plus sain. La dette technique liée aux expérimentations passées (optimisations prématurées, multiples stratégies de capteurs) a été remboursée. Le système repose sur des bases standards et éprouvées, facilitant la maintenance future.

---
*Généré par l'Assistant IA - Session de Refactorisation*

