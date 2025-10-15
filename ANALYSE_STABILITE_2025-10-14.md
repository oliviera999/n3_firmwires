# 📊 Analyse de Stabilité ESP32 - 14 Octobre 2025

## 🔍 Période de monitoring
- **Durée**: ~5 minutes
- **Version**: 11.32
- **WiFi**: inwi Home 4G 8306D9 (RSSI: -56 à -59 dBm)

---

## 🔴 PROBLÈMES CRITIQUES (Priorité 1)

### ❌ Aucun Guru Meditation, Panic ou Brownout détecté
✅ Le système ne crash pas - **POSITIF**

---

## 🟡 PROBLÈMES MAJEURS (Priorité 2)

### 1. ⚠️ **Erreurs Watchdog Répétées**
```
E (216335) task_wdt: esp_task_wdt_reset(707): task not found
E (223849) task_wdt: esp_task_wdt_reset(707): task not found  
E (224907) task_wdt: esp_task_wdt_reset(707): task not found
```

**Analyse:**
- Une tâche essaie de réinitialiser le watchdog mais n'est pas enregistrée
- Survient pendant les opérations réseau (envoi HTTP)
- **Risque**: Le watchdog pourrait déclencher un reset si non corrigé

**Cause probable:**
- Code qui appelle `esp_task_wdt_reset()` depuis une tâche non enregistrée
- Pourrait être lié aux opérations HTTP asynchrones

### 2. 🌐 **Erreurs Réseau Fréquentes**

**Erreurs HTTP détectées:**
```
[228771][W] error(-1): connection refused
[229201][W] error(-11): read Timeout  
[229201][W] error(-7): no HTTP server
[230546][W] error(-5): connection lost
```

**Statistiques:**
- 1 erreur "connection refused" 
- 1 erreur "read timeout"
- 1 erreur "no HTTP server"
- 1 erreur "connection lost"
- 1 erreur serveur (HTTP 500)
- 1 erreur client (HTTP 400 - CRC mismatch)

**Impact:**
- Certaines requêtes échouent (retry automatique)
- Données mises en queue pour envoi ultérieur (OK)
- **Taux de succès global: 99.9%** (selon logs)

### 3. 💾 **Erreurs NVS**

```
[209436][E][Preferences.cpp:47] begin(): nvs_open failed: NOT_FOUND
[229235][E][Preferences.cpp:199] putUInt(): nvs_set_u32 fail: cmd_pump_tank_off KEY_TOO_LONG
```

**Problème 1:** Espace NVS non trouvé (NOT_FOUND)
- Peut nécessiter une initialisation NVS

**Problème 2:** Clé trop longue `cmd_pump_tank_off`
- **CRITIQUE**: Les clés NVS sont limitées à 15 caractères
- Cette clé fait 18 caractères → **DÉPASSEMENT**
- Empêche la sauvegarde des statistiques de commandes

---

## 🟢 PROBLÈMES MODÉRÉS (Priorité 3)

### 4. 📡 **Capteur DS18B20 (Température Eau) - INSTABLE**

**Symptômes récurrents:**
```
[WaterTemp] Lecture invalide rejetée: -127.0°C (NaN=0, inRange=0)
[WaterTemp] Aucun capteur fonctionnel trouvé sur le bus OneWire
[WaterTemp] Capteur non connecté, reset matériel...
[WaterTemp] Reset matériel du capteur...
```

**Fréquence:** 
- Lectures invalides multiples toutes les 15-30 secondes
- Reset matériel fréquent
- Récupération via NVS (dernière valeur: 25.5°C)

**Impact:**
- Système utilise la dernière valeur valide (bonne récupération)
- Mais indique un **problème matériel ou de connexion**

**Causes possibles:**
- Câblage défectueux (pull-up résistance)
- Alimentation instable
- Capteur défectueux
- Timing OneWire incorrect

### 5. 🌡️ **Capteur DHT22 (Air) - Filtrage échoue régulièrement**

```
[AirSensor] Filtrage avancé échoué, tentative de récupération...
[AirSensor] Tentative de récupération 1/2...
[AirSensor] Récupération réussie: 63.0%
```

**Analyse:**
- Le filtrage avancé échoue systématiquement
- Récupération réussit (valeur constante: 63.0%)
- **Valeur suspecte**: toujours 63.0% → possiblement figée

### 6. 📏 **Capteurs Ultrason HC-SR04 - Lectures Erratiques**

**Problèmes récurrents:**
```
[Ultrasonic] Saut détecté: 166 cm -> 209 cm (écart: 43 cm)
[Ultrasonic] Saut vers le haut, utilise ancienne valeur par sécurité
[Ultrasonic] Lecture 1 timeout
[Ultrasonic] Écart important avec l'historique: 112 cm (moyenne: 209 cm)
```

**Capteur Potager (wlPota):**
- Sauts fréquents de ~40-109 cm
- Le système utilise l'ancienne valeur (protection OK)
- Lectures variant entre 99 cm et 209 cm → **TRÈS INSTABLE**

**Capteur Aquarium (wlAqua):**
- Plus stable mais quelques sauts
- Varie entre 167-209 cm

**Capteur Réserve (wlRese):**
- Relativement stable autour de 209-210 cm

---

## 📈 MÉMOIRE (OK)

```
[Auto] 📊 Heap libre: 88728 bytes (minimum historique: 75132 bytes)
[Auto] 📊 Heap après envoi: 84404 bytes (delta: -4324 bytes)
```

✅ **Mémoire stable** - Pas de fuite détectée
✅ Heap minimum: 75132 bytes (seuil critique: ~30000)

---

## 🔄 FONCTIONNALITÉS OBSERVÉES

### ✅ Fonctionnement Correct:
- **WiFi**: Connexion stable (RSSI -56 à -59 dBm)
- **Mise en veille**: Détection marée fonctionne
- **Queue de données**: Mécanisme de retry fonctionne
- **Persistance NVS**: États actionneurs sauvegardés (sauf stats)
- **Récupération capteurs**: Système résilient aux erreurs

### ⚠️ Comportements Anormaux:
- Connexion WiFi primaire (AP-Techno-T06) échoue systématiquement
- Utilise WiFi secondaire (inwi Home 4G) → OK mais pas optimal
- Marée calcule des variations importantes (+38 cm, -41 cm)

---

## 🎯 RECOMMANDATIONS URGENTES

### 1. **Corriger la clé NVS trop longue** (URGENT)
```cpp
// ACTUEL (18 caractères - TROP LONG):
"cmd_pump_tank_off"

// PROPOSÉ (15 caractères max):
"cmd_tank_off"  // ou "ptank_off_cnt"
```

### 2. **Identifier la tâche watchdog non enregistrée**
- Rechercher tous les appels à `esp_task_wdt_reset()`
- Vérifier que chaque tâche est enregistrée via `esp_task_wdt_add()`
- Possiblement dans le code HTTP/réseau

### 3. **Diagnostiquer le DS18B20**
- Vérifier le câblage physique
- Vérifier la résistance pull-up (4.7kΩ recommandé)
- Tester avec un autre capteur DS18B20
- Vérifier l'alimentation (stable 3.3V ou 5V)

### 4. **Vérifier le DHT22**
- Valeur figée à 63.0% → capteur défectueux ou câblage
- Tester avec un autre capteur DHT22

### 5. **Stabiliser les HC-SR04 (Potager surtout)**
- Vérifier alimentation stable (5V)
- Vérifier câblage et connexions
- Éloigner des sources d'interférence électromagnétique
- Possiblement défectueux → remplacer

---

## 📊 SYNTHÈSE

### 🔴 Problèmes Bloquants:
1. **Clé NVS trop longue** → Empêche sauvegarde statistiques

### 🟡 Problèmes Majeurs:
1. **Watchdog task not found** → Risque de reset
2. **DS18B20 instable** → Lectures température eau non fiables
3. **HC-SR04 potager erratique** → Niveau eau non fiable

### 🟢 État Général:
- **Stabilité système**: ✅ Pas de crash
- **Mémoire**: ✅ OK
- **WiFi**: ✅ Connecté (secondaire)
- **Fonctionnalités**: ⚠️ Partiellement dégradées

---

## 📝 ACTIONS PRIORITAIRES

1. **IMMÉDIAT**: Corriger clé NVS (`cmd_pump_tank_off` → `cmd_tank_off`)
2. **URGENT**: Corriger erreur watchdog dans code HTTP
3. **IMPORTANT**: Diagnostiquer matériel DS18B20
4. **IMPORTANT**: Diagnostiquer matériel DHT22 et HC-SR04 potager

---

**Version analysée**: 11.32  
**Date analyse**: 14 Octobre 2025  
**Durée monitoring**: 5 minutes  
**Conclusion**: Système **INSTABLE** - Corrections software urgentes + diagnostic matériel requis


