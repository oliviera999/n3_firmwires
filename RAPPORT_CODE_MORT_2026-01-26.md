# Rapport d'Analyse du Code Mort - Projet FFP5CS
**Date**: 26 janvier 2026  
**Version analysée**: v11.155+  
**Objectif**: Identifier et documenter tout le code mort pour proposer des recommandations de nettoyage

---

## Résumé Exécutif

L'analyse du projet FFP5CS a identifié **plusieurs éléments de code mort** qui peuvent être supprimés ou corrigés :

- **3 fichiers** avec includes obsolètes (références à fichiers supprimés)
- **2 fichiers** définis mais jamais utilisés dans le code source principal
- **1 classe complète** (`TLSPool`) jamais initialisée ni appelée
- **1 fonction** (`EventLog::add()`) utilisée mais fichier header supprimé
- **1 include redondant** (`ArduinoJson.h` inclus deux fois)

**Impact estimé** :
- **~200 lignes de code** à supprimer
- **~35KB de mémoire** potentiellement libérable (si TLSPool est supprimé)
- **Erreurs de compilation** potentielles avec `event_log.h`

---

## 1. Code Mort Critique (À Corriger Immédiatement)

### 1.1 Fichier `event_log.h` supprimé mais encore référencé

**Statut** : ❌ **CRITIQUE** - Cause des erreurs de compilation

**Problème** :
Le fichier `include/event_log.h` a été supprimé (visible dans git status), mais il est encore :
- Inclus dans `src/app_tasks.cpp` (ligne 15)
- Inclus dans `src/task_monitor.cpp` (ligne 3)
- Utilisé dans `src/system_actuators.cpp` (ligne 132) : `EventLog::add("Feed phase 2 scheduling failed")`

**Analyse** :
- `app_tasks.cpp` : Include présent mais **aucune utilisation** de `EventLog` dans le fichier
- `task_monitor.cpp` : Include présent mais **aucune utilisation** de `EventLog` dans le fichier
- `system_actuators.cpp` : **Utilisation réelle** de `EventLog::add()` qui causera une erreur de compilation

**Recommandation** :
1. **Supprimer les includes obsolètes** dans `app_tasks.cpp` et `task_monitor.cpp`
2. **Remplacer `EventLog::add()`** dans `system_actuators.cpp` par `LOG_ERROR()` ou `Serial.println()`

**Fichiers à modifier** :
- `src/app_tasks.cpp` : Supprimer ligne 15 `#include "event_log.h"`
- `src/task_monitor.cpp` : Supprimer ligne 3 `#include "event_log.h"`
- `src/system_actuators.cpp` : Remplacer ligne 132 `EventLog::add("Feed phase 2 scheduling failed");` par `LOG_ERROR("Feed phase 2 scheduling failed");`

**Gain** : Correction des erreurs de compilation, ~3 lignes supprimées

---

## 2. Code Mort Important (Fichiers Non Utilisés)

### 2.1 Classe `TLSPool` - Jamais initialisée ni utilisée

**Statut** : ⚠️ **IMPORTANT** - Code défini mais jamais appelé

**Fichiers** :
- `include/tls_pool.h` (60 lignes)
- `src/tls_pool.cpp` (66 lignes)

**Analyse** :
- Classe `TLSPool` complètement implémentée avec méthodes :
  - `initialize()` - Jamais appelée
  - `reset()` - Jamais appelée
  - `isAvailable()` - Jamais appelée
  - `getFreeSize()` - Jamais appelée
  - `getTotalSize()` - Jamais appelée
- Mentionnée dans la documentation (`RAPPORT_VALIDATION_FRAGMENTATION_TLS_2026-01-26.md`) mais **jamais intégrée** dans le code
- Selon le rapport, le pool TLS **aggrave le problème** de fragmentation (57% → 85%)

**Recommandation** :
1. **Option A (Recommandée)** : Supprimer complètement `TLSPool` car :
   - Jamais utilisé dans le code
   - Selon les rapports, aggrave la fragmentation mémoire
   - Consomme 35KB de mémoire inutilement
2. **Option B** : Intégrer si prévu pour résoudre fragmentation TLS (mais nécessite tests approfondis)

**Gain estimé** : ~126 lignes supprimées, ~35KB mémoire libérable

**Complexité** : FAIBLE (simple suppression de fichiers)

---

### 2.2 Fichier `debug_log.h` - Utilisé uniquement en documentation

**Statut** : ⚠️ **OPTIONNEL** - Code de debug non intégré

**Fichier** :
- `include/debug_log.h` (344 lignes)

**Analyse** :
- Fichier défini avec macros `DEBUG_LOG_*` et namespace `DebugLog`
- **Jamais inclus** dans le code source principal
- Utilisé uniquement dans :
  - Documentation (`GUIDE_DEBUG_MODE.md`)
  - Scripts PowerShell (`capture_debug_logs.ps1`, `extract_debug_log.ps1`)
- Fonction `registerDebugLogs()` dans `web_routes_status.cpp` n'utilise **pas** `debug_log.h`

**Recommandation** :
1. **Option A** : Conserver si prévu pour debug futur (documentation indique usage prévu)
2. **Option B** : Supprimer si non prévu d'être utilisé

**Gain estimé** : ~344 lignes si supprimé

**Complexité** : FAIBLE (simple suppression de fichier)

---

## 3. Code Mort Mineur (Includes Redondants)

### 3.1 Include redondant `ArduinoJson.h`

**Statut** : ⚠️ **MINEUR** - Include dupliqué

**Fichier** : `src/web_routes_status.cpp`

**Problème** :
- Ligne 5 : `#include <ArduinoJson.h>`
- Ligne 22 : `#include <ArduinoJson.h>` (redondant)

**Recommandation** : Supprimer l'include redondant ligne 22

**Gain** : 1 ligne supprimée, compilation légèrement plus rapide

---

## 4. Fichiers de Test (Légitimes)

### 4.1 `tls_minimal_test.cpp`

**Statut** : ✅ **LÉGITIME** - Fichier de test

**Fichier** : `src/tls_minimal_test.cpp` (174 lignes)

**Analyse** :
- Exclu du build normal via `platformio.ini` (ligne 26 : `-<tls_minimal_test.cpp>`)
- Utilisé uniquement pour l'environnement de test `env:wroom-tls-test` (ligne 176 : `+<tls_minimal_test.cpp>`)
- **Pas du code mort** - fichier de test légitime

**Recommandation** : **Conserver** - Fichier de test nécessaire

---

## 5. Recommandations par Priorité

### Priorité CRITIQUE (À corriger immédiatement)

1. **Supprimer includes obsolètes `event_log.h`**
   - `src/app_tasks.cpp` ligne 15
   - `src/task_monitor.cpp` ligne 3
   - Remplacer `EventLog::add()` dans `src/system_actuators.cpp` ligne 132

**Impact** : Corrige les erreurs de compilation potentielles

---

### Priorité IMPORTANTE (Réduction mémoire/complexité)

2. **Supprimer `TLSPool`** (si non prévu d'être utilisé)
   - `include/tls_pool.h`
   - `src/tls_pool.cpp`
   - Vérifier références dans documentation

**Impact** : ~126 lignes supprimées, ~35KB mémoire libérable

---

### Priorité OPTIONNELLE (Nettoyage code)

3. **Décider du sort de `debug_log.h`**
   - Conserver si prévu pour debug futur
   - Supprimer si non prévu

**Impact** : ~344 lignes si supprimé

4. **Supprimer include redondant**
   - `src/web_routes_status.cpp` ligne 22

**Impact** : 1 ligne supprimée

---

## 6. Estimation des Gains

### Lignes de code à supprimer

| Catégorie | Fichiers | Lignes estimées |
|-----------|----------|-----------------|
| Includes obsolètes | 2 fichiers | ~2 lignes |
| Remplacement EventLog | 1 fichier | ~1 ligne |
| TLSPool (si supprimé) | 2 fichiers | ~126 lignes |
| debug_log.h (si supprimé) | 1 fichier | ~344 lignes |
| Include redondant | 1 fichier | ~1 ligne |
| **TOTAL** | | **~474 lignes** |

### Mémoire libérable

| Élément | Mémoire estimée |
|---------|-----------------|
| TLSPool (si supprimé) | ~35KB heap |
| **TOTAL** | **~35KB** |

### Complexité de nettoyage

| Action | Complexité | Temps estimé |
|--------|------------|-------------|
| Supprimer includes obsolètes | FAIBLE | 5 min |
| Remplacer EventLog::add() | FAIBLE | 5 min |
| Supprimer TLSPool | FAIBLE | 10 min |
| Supprimer debug_log.h | FAIBLE | 5 min |
| Supprimer include redondant | TRÈS FAIBLE | 1 min |
| **TOTAL** | | **~30 minutes** |

---

## 7. Plan d'Action Recommandé

### Phase 1 : Corrections Critiques (Immédiat)

1. ✅ Supprimer `#include "event_log.h"` dans `src/app_tasks.cpp`
2. ✅ Supprimer `#include "event_log.h"` dans `src/task_monitor.cpp`
3. ✅ Remplacer `EventLog::add("Feed phase 2 scheduling failed");` par `LOG_ERROR("Feed phase 2 scheduling failed");` dans `src/system_actuators.cpp`
4. ✅ Vérifier compilation sans erreurs

**Durée estimée** : 15 minutes

---

### Phase 2 : Nettoyage Important (Court terme)

5. ✅ Décider si `TLSPool` doit être conservé ou supprimé
6. ✅ Si suppression : Supprimer `include/tls_pool.h` et `src/tls_pool.cpp`
7. ✅ Vérifier compilation sans erreurs
8. ✅ Tester fonctionnement normal

**Durée estimée** : 20 minutes

---

### Phase 3 : Nettoyage Optionnel (Moyen terme)

9. ✅ Décider si `debug_log.h` doit être conservé ou supprimé
10. ✅ Si suppression : Supprimer `include/debug_log.h`
11. ✅ Supprimer include redondant dans `src/web_routes_status.cpp` ligne 22
12. ✅ Vérifier compilation sans erreurs

**Durée estimée** : 10 minutes

---

## 8. Validation Post-Nettoyage

Après chaque phase, vérifier :

- ✅ Compilation sans erreurs (`pio run -e wroom-test`)
- ✅ Fonctionnalités intactes (tests basiques)
- ✅ Pas de régressions
- ✅ Documentation à jour (si fichiers supprimés)

---

## 9. Conclusion

Le projet contient **du code mort significatif** qui peut être nettoyé sans impact fonctionnel :

- **Code critique** : 3 corrections nécessaires pour éviter erreurs de compilation
- **Code important** : ~126 lignes + 35KB mémoire libérables (TLSPool)
- **Code optionnel** : ~345 lignes supplémentaires (debug_log.h)

**Recommandation globale** : Procéder au nettoyage par phases, en commençant par les corrections critiques, puis le nettoyage important, et enfin le nettoyage optionnel selon les besoins du projet.

---

**Rapport généré le** : 26 janvier 2026  
**Prochaine révision recommandée** : Après nettoyage Phase 1
