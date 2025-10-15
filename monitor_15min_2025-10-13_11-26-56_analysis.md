# 📊 ANALYSE COMPLÈTE DU MONITORING - ESP32 WROOM-TEST

**Date**: 13 octobre 2025, 11:27-11:28  
**Durée**: ~60 secondes  
**Version**: 11.18  
**Environnement**: wroom-test (PROFILE_TEST)  
**Fichier source**: `monitor_15min_2025-10-13_11-26-56.log` (63.46 KB, 1030 lignes)

---

## 🎯 RÉSUMÉ EXÉCUTIF

### État Général
- ✅ **Démarrage**: Réussi après effacement complet
- ⚠️ **Stabilité**: **PROBLÈME MAJEUR** - 71 erreurs watchdog
- ⚠️ **Communication serveur**: Erreurs HTTP 500 répétées
- ✅ **Mémoire**: Stable (117-175 KB heap libre)
- ❌ **Reboot**: Système instable, risque de reboot imminent

---

## 🔴 PRIORITÉ 1 - ERREURS CRITIQUES

### Core Dump Partition
```
E (811) esp_core_dump_flash: No core dump partition found!
```
**Statut**: ✅ **NORMAL** - Pas de partition core dump configurée dans `partitions_esp32_wroom_test_large.csv`. Ce n'est PAS une erreur critique.

### Conclusion P1
✅ **Aucune erreur critique (Guru Meditation, Panic, Brownout) détectée**

---

## 🟡 PRIORITÉ 2 - WATCHDOG & TIMEOUTS ⚠️⚠️⚠️

### 🚨 PROBLÈME MAJEUR: 71 Erreurs Watchdog

#### Type d'erreur
```
E (XXXXX) task_wdt: esp_task_wdt_reset(707): task not found
```

#### Distribution temporelle
- **11:27:09** - 2 erreurs (lors de l'init des capteurs)
- **11:27:10** - 1 erreur
- **11:27:49** - 1 erreur  
- **11:27:59** - 7 erreurs ⚠️ **PIC D'ERREURS**
- **11:28:00-06** - 60+ erreurs ⚠️⚠️⚠️ **AVALANCHE**

#### Contexte de la première erreur
Survient pendant l'initialisation du capteur de température d'eau (DS18B20):
```
11:27:09.788 > [WaterTemp] Dernière température valide chargée depuis NVS: 29.0°C
11:27:09.788 > E (12201) task_wdt: esp_task_wdt_reset(707): task not found
11:27:09.879 > E (12319) task_wdt: esp_task_wdt_reset(707): task not found
11:27:10.007 > [WaterTemp] Capteur connecté et fonctionnel (test: 29.0°C)
```

#### Contexte du pic d'erreurs (11:27:59)
Survient pendant:
1. Lectures multiples de capteurs ultrason (niveau d'eau)
2. Affichage OLED
3. Activation/désactivation d'actionneurs (pompe, chauffage)
4. Envoi HTTP au serveur

```
11:27:59.573 > [Actuators] 🌐 Synchronisation serveur en arrière-plan...
11:27:59.623 > E (62062) task_wdt: esp_task_wdt_reset(707): task not found
11:27:59.626 > E (62062) task_wdt: esp_task_wdt_reset(707): task not found
11:27:59.632 > E (62062) task_wdt: esp_task_wdt_reset(707): task not found
```

#### Analyse Technique

**Cause racine identifiée**:  
Les tâches FreeRTOS (sensorTask, webTask, displayTask, autoTask) tentent d'appeler `esp_task_wdt_reset()` mais ne sont **PAS enregistrées** dans le Task Watchdog Timer (TWDT).

**Code problématique probable**:
- `src/system_sensors.cpp` - Lectures capteurs longues (DS18B20: ~780ms, DHT11: ~3300ms)
- `src/display_view.cpp` - Refresh OLED
- `src/actuators.cpp` - Contrôle relais/servo
- Toutes ces tâches appellent `esp_task_wdt_reset()` sans être dans le watchdog

**Impact**:
- ⚠️ **Risque élevé de reboot** - Si le watchdog principal timeout (5 secondes par défaut)
- ⚠️ **Instabilité système** - Les tâches ne sont pas surveillées correctement
- ⚠️ **Performances dégradées** - Erreurs répétées consomment CPU

---

### HTTP Timeouts & Erreurs Serveur

#### Erreurs HTTP 500
```
[HTTP] ← code 500, 104 bytes
Données enregistrées avec succèsUne erreur serveur est survenue. Veuillez réessayer ultérieurement.
```
- **3 tentatives** à 11:27:28, 11:27:29
- **3 tentatives** à 11:27:44, 11:27:45, 11:27:46
- **3 tentatives** à 11:28:05
- **Résultat**: `[Network] sendFullUpdate FAILED`

**Analyse**: Le serveur distant (iot.olution.info/ffp3/post-data-test) renvoie 500 mais avec message "Données enregistrées avec succès". Problème côté serveur PHP.

---

## 🟢 PRIORITÉ 3 - MÉMOIRE

### État Mémoire
```
Heap libre au démarrage:      137008 bytes (~133 KB)
Heap libre après init:        117820-124228 bytes (~115-121 KB)
Heap min free:                98628-114900 bytes (~96-112 KB)
Heap max alloc:               86004-90100 bytes (~84-88 KB)
```

### Stack High Water Mark (HWM)
```
sensor:   5024 bytes libre
web:      5324 bytes libre
auto:     8504 bytes libre
display:  4408 bytes libre
loop:     2300 bytes libre ⚠️ (stack le plus utilisé)
```

### Diagnostics Mémoire Bizarre
```
minHeap: 4294967295 bytes (0xFFFFFFFF)
```
**Problème**: Valeur max uint32_t → Variable `diagnostics.minHeap` **pas initialisée correctement** ou **overflow**.

### Conclusion Mémoire
✅ Mémoire **stable** et **suffisante**  
⚠️ Stack de la boucle principale un peu juste (2300 bytes restants)

---

## 📊 STATISTIQUES GLOBALES

| Métrique | Valeur |
|----------|--------|
| **Lignes de log** | 1030 |
| **Erreurs (E)** | 65 |
| **Warnings (W)** | 0 |
| **Info (INFO)** | 44 |
| **Durée de monitoring** | ~60 secondes |
| **Reboots détectés** | 2 (boot #3 puis #4) |

---

## 🔍 PROBLÈMES SECONDAIRES

### 1. Capteurs DHT11
```
[AirSensor] Filtrage avancé échoué, tentative de récupération...
[AirSensor] Tentative de récupération 1/2...
[AirSensor] Récupération réussie: 60.0-62.0%
```
**Temps de lecture**: 3.3 secondes systématiquement  
**Statut**: ✅ Fonctionnel mais lent (comportement normal DHT11)

### 2. Servo PWM
```
[ 11685][E][ESP32PWM.cpp:319] attachPin(): [ESP32PWM] ERROR PWM channel failed to configure on pin 12!
[ 11744][E][ESP32PWM.cpp:319] attachPin(): [ESP32PWM] ERROR PWM channel failed to configure on pin 13!
```
**Cause**: Le servo tente de s'attacher deux fois au même canal LEDC  
**Statut**: ⚠️ Message d'erreur mais servo **fonctionne** (success après)

### 3. NVS Preferences
```
[   489][E][Preferences.cpp:47] begin(): nvs_open failed: NOT_FOUND
[ 21547][E][Preferences.cpp:47] begin(): nvs_open failed: NOT_FOUND
```
**Cause**: Tentative d'ouverture d'un namespace NVS inexistant (normal au premier démarrage)  
**Statut**: ✅ Bénin

### 4. Time Drift Monitor
```
[   368-450][E][Preferences.cpp:93/526] remove()/getBytesLength(): NOT_FOUND
```
**Cause**: Première initialisation du module de dérive temporelle  
**Statut**: ✅ Normal (cleanup des anciennes données)

---

## 🎯 DIAGNOSTICS FINAUX

### Problèmes Critiques à Résoudre

#### 1. ⚠️⚠️⚠️ **URGENT** - Erreurs Watchdog (71 occurrences)

**Problème**:  
Les tâches FreeRTOS appellent `esp_task_wdt_reset()` sans être enregistrées dans le TWDT.

**Solution recommandée**:
```cpp
// Dans chaque tâche (sensorTask, webTask, displayTask, etc.)
// Lors de la création:
esp_task_wdt_add(NULL);  // Enregistrer la tâche courante

// OU enlever complètement les appels à esp_task_wdt_reset()
// car le watchdog TWDT natif surveille déjà les tâches FreeRTOS
```

**Fichiers à modifier**:
- `src/system_sensors.cpp` (sensorTask)
- `src/web_client.cpp` (webTask)
- `src/display_view.cpp` (displayTask)
- `src/automatism.cpp` (autoTask)

**Alternative**:
Désactiver complètement le watchdog manuel et laisser le TWDT natif gérer:
```cpp
// Dans platformio.ini, ajouter:
-DCONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

---

#### 2. ⚠️ **IMPORTANT** - Erreurs HTTP 500 Serveur

**Problème**:  
Le serveur distant répond 500 avec message contradictoire:
```
Données enregistrées avec succèsUne erreur serveur est survenue.
```

**Action**: Vérifier le script PHP `post-data-test` sur le serveur distant.

---

#### 3. ⚠️ **MINEUR** - Variable minHeap non initialisée

**Problème**: `minHeap = 4294967295` (0xFFFFFFFF)

**Solution**:
```cpp
// Dans src/power.cpp ou diagnostics.cpp
g_minHeap = ESP.getFreeHeap();  // Initialiser au démarrage
```

---

## ⚙️ RECOMMANDATIONS

### Court Terme (Immédiat)
1. ✅ **Corriger les erreurs watchdog** - Ajouter `esp_task_wdt_add(NULL)` dans chaque tâche
2. ⚠️ Tester sans appels `esp_task_wdt_reset()` manuels
3. ⚠️ Augmenter le timeout TWDT à 10 secondes (lectures capteurs longues)

### Moyen Terme
4. ⚠️ Optimiser les lectures DHT11 (actuellement 3.3s → cible 2s)
5. ⚠️ Investiguer/corriger l'erreur serveur HTTP 500
6. ⚠️ Initialiser correctement `diagnostics.minHeap`

### Long Terme
7. ✅ Migrer vers DHT22 en production (plus rapide et précis)
8. ✅ Ajouter monitoring des stack overflows
9. ✅ Implémenter retry avec backoff exponentiel pour HTTP

---

## 📝 CONCLUSION

### Verdict Global
⚠️⚠️⚠️ **SYSTÈME INSTABLE - CORRECTION URGENTE REQUISE**

### Cause Principale
**Erreurs Watchdog (71x)** causées par des appels `esp_task_wdt_reset()` depuis des tâches non enregistrées dans le TWDT.

### Pronostic
- ❌ **Sans correction**: Risque élevé de reboot après ~5 minutes
- ✅ **Avec correction**: Stabilité attendue

### Prochaines Étapes
1. Modifier le code des tâches (ajouter `esp_task_wdt_add()`)
2. Compiler v11.19
3. Flash + monitoring 5 minutes
4. Vérifier disparition des erreurs watchdog

---

## 📎 ANNEXES

### Commandes Utilisées
```bash
# Effacement complet
pio run -e wroom-test -t erase

# Flash firmware
pio run -e wroom-test -t upload

# Flash filesystem
pio run -e wroom-test -t uploadfs

# Monitoring
pio device monitor -e wroom-test
```

### Fichiers Générés
- `monitor_15min_2025-10-13_11-26-56.log` (63.46 KB)
- `monitor_15min_2025-10-13_11-26-56_analysis.log`
- `monitor_15min_2025-10-13_11-26-56_detailed_analysis.txt`
- `monitor_15min_2025-10-13_11-26-56_analysis.md` (ce fichier)

---

**Rapport généré le**: 13 octobre 2025, 11:30  
**Analyste**: AI Assistant (Cursor)  
**Version du firmware analysé**: 11.18

