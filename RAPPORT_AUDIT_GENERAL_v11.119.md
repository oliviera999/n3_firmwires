# Rapport d'Audit Général - Projet FFP5CS (v11.119)

**Date** : 7 Janvier 2026  
**Version analysée** : v11.119

Cet audit fait suite à une série de refactorisations majeures (suppression de code mort, simplification du monitoring, migration `ArduinoJson 7`). Il vise à valider la cohérence actuelle du projet.

---

## 1. Synthèse de l'État du Projet

Le projet est dans un état **stable et assaini**. La dette technique identifiée en Novembre 2025 a été largement remboursée. L'architecture est plus claire et respecte mieux les contraintes de l'ESP32-WROOM.

### Points Forts
- ✅ **Configuration Unifiée** : `include/config.h` centralise désormais toute la configuration statique (namespaces `SystemConfig`, `NetworkConfig`, `SleepConfig`, etc.). `project_config.h` a été éliminé.
- ✅ **Modernisation JSON** : Utilisation exclusive de `ArduinoJson 7` (`JsonDocument`) avec allocation standard sur le heap, remplaçant le `JsonPool` complexe et obsolète.
- ✅ **Monitoring Allégé** : `TimeDriftMonitor` et `TaskMonitor` sont maintenant des utilitaires légers, pertinents pour la production.
- ✅ **Nettoyage Capteurs** : Suppression des multiples stratégies de filtrage concurrentes dans `sensors.cpp`.

---

## 2. Analyse Structurelle & Code

### Arborescence
- Structure claire `src/` (implémentation) et `include/` (interfaces).
- Le dossier `src/automatism/` regroupe logiquement les sous-modules de l'automatisme, bien que le fichier `src/automatism.cpp` reste volumineux (~2300 lignes).

### Inclusions & Dépendances
- **Cohérence** : Tous les fichiers sources inspectés utilisent correctement `#include "config.h"`. Aucune trace de l'ancien `project_config.h` dans le code compilé.
- **Dépendances** : Le couplage reste fort entre `Automatism` et les autres managers (`WebClient`, `PowerManager`, `SystemSensors`), ce qui est typique pour ce genre de firmware monolithique, mais géré via injection de dépendances dans le constructeur.

### Qualité du Code (Échantillonnage)
- **`web_server.cpp`** : Nettoyé de la logique `JsonPool`. Les endpoints sont clairs. Utilise correctement `new JsonDocument` / `delete`.
- **`automatism.cpp`** : Reste le point le plus complexe. La logique de sommeil (`handleAutoSleep`) et de gestion des tâches est dense.
- **Headers** : Propres, avec `#pragma once`.

---

## 3. Problèmes Identifiés

### 🟢 Mineur
- **Commentaires Obsolètes** : Quelques commentaires font encore référence à des versions anciennes ou des TODOs mineurs (ex: dans `web_server.cpp`, des commentaires sur des optimisations supprimées).
- **Fichiers de Documentation** : Certains fichiers `.md` dans `docs/` ou à la racine (`README.md` dans `src` et `include`) pourraient ne plus être parfaitement à jour avec les changements récents (ex: mention de `JsonPool` ou `project_config.h`).

### 🟡 Majeur
- **Taille de `automatism.cpp`** : Avec plus de 2300 lignes, ce fichier reste difficile à maintenir. Bien que modulaire via les "Controllers", la logique centrale est très concentrée.
- **Gestion Mémoire HTTP** : Bien que `JsonPool` soit parti, l'allocation dynamique `new JsonDocument` dans les handlers HTTP doit être surveillée sur le long terme pour éviter la fragmentation, même si le WROOM redémarre régulièrement.

### 🔴 Critique
*Aucun problème critique bloquant n'a été détecté lors de cet audit statique.*

---

## 4. Recommandations

1.  **Documentation** : Mettre à jour les `README` techniques pour refléter la disparition de `JsonPool` et l'usage de `config.h`.
2.  **Refactoring Automatism (Futur)** : Envisager de découper `automatism.cpp` davantage, par exemple en extrayant la logique de gestion du sommeil (`handleAutoSleep`) dans une classe `SleepManager` dédiée plus autonome.
3.  **Tests Longue Durée** : Surveiller la stabilité mémoire sur une période > 24h pour confirmer que le remplacement de `JsonPool` par `new/delete` ne cause pas de fragmentation excessive.

---
*Audit réalisé par l'Assistant IA*

