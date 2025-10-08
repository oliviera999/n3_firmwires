# Logging Détaillé de la Pompe de Réserve

## 📋 Objectif

Ajouter un système de logging détaillé pour la pompe de réserve afin de surveiller :
- **Quand** la pompe démarre (timestamp)
- **Quand** la pompe s'arrête (timestamp)
- **Durée** d'activation de la pompe
- **Temps total** d'activité depuis le démarrage
- **Nombre d'arrêts** effectués

## 🔧 Modifications Apportées

### 1. Variables de Tracking (`include/system_actuators.h`)

```cpp
// Variables pour le tracking du timing de la pompe de réserve
unsigned long tankPumpStartTime = 0;        // Timestamp du dernier démarrage
unsigned long tankPumpLastStopTime = 0;     // Timestamp du dernier arrêt
unsigned long tankPumpTotalRuntime = 0;     // Temps total d'activité cumulé
unsigned long tankPumpTotalStops = 0;       // Nombre total d'arrêts
```

### 2. Logs Détaillés de Démarrage

**Avant :**
```cpp
void startTankPump() { pumpTank.on(); LOG(LOG_INFO, "Tank pump ON"); }
```

**Après :**
```cpp
void startTankPump() { 
  pumpTank.on(); 
  tankPumpStartTime = millis();
  tankPumpTotalStops++;
  LOG(LOG_INFO, "[POMPE_RESERV] DÉMARRAGE - Timestamp: %lu ms", tankPumpStartTime);
  Serial.printf("[POMPE_RESERV] DÉMARRAGE - Timestamp: %lu ms\n", tankPumpStartTime);
}
```

**Exemple de sortie :**
```
[POMPE_RESERV] DÉMARRAGE - Timestamp: 1234567 ms
```

### 3. Logs Détaillés d'Arrêt

**Avant :**
```cpp
void stopTankPump() { pumpTank.off(); LOG(LOG_INFO, "Tank pump OFF"); }
```

**Après :**
```cpp
void stopTankPump() { 
  pumpTank.off(); 
  if (tankPumpStartTime > 0) {
    unsigned long runtime = millis() - tankPumpStartTime;
    tankPumpTotalRuntime += runtime;
    tankPumpLastStopTime = millis();
    LOG(LOG_INFO, "[POMPE_RESERV] ARRÊT - Durée: %lu ms, Total runtime: %lu ms, Arrêts: %lu", 
        runtime, tankPumpTotalRuntime, tankPumpTotalStops);
    Serial.printf("[POMPE_RESERV] ARRÊT - Durée: %lu ms, Total runtime: %lu ms, Arrêts: %lu\n", 
                 runtime, tankPumpTotalRuntime, tankPumpTotalStops);
  }
}
```

**Exemple de sortie :**
```
[POMPE_RESERV] ARRÊT - Durée: 120000 ms, Total runtime: 360000 ms, Arrêts: 3
```

### 4. Statistiques Périodiques (`src/app.cpp`)

Affichage automatique toutes les 5 minutes dans le moniteur série :

```
=== STATISTIQUES POMPE DE RÉSERVE ===
État actuel: ARRÊTÉE
Temps total d'activité: 360000 ms (360 s)
Nombre total d'arrêts: 3
Dernier arrêt: il y a 180000 ms (180 s)
=====================================
```

### 5. Endpoint Web (`/pumpstats`)

Nouvel endpoint JSON pour récupérer les statistiques via HTTP :

**URL :** `http://[IP_ESP32]/pumpstats`

**Réponse JSON :**
```json
{
  "isRunning": false,
  "currentRuntime": 0,
  "currentRuntimeSec": 0,
  "totalRuntime": 360000,
  "totalRuntimeSec": 360,
  "totalStops": 3,
  "lastStopTime": 1234567,
  "timeSinceLastStop": 180000,
  "timeSinceLastStopSec": 180
}
```

## 📊 Méthodes de Récupération des Statistiques

### 1. Via Moniteur Série

Les logs apparaissent automatiquement dans le moniteur série :
- **Démarrage/Arrêt** : Logs immédiats avec timestamps
- **Statistiques** : Affichage périodique toutes les 5 minutes

### 2. Via Endpoint Web

```bash
curl http://192.168.1.100/pumpstats
```

### 3. Via Script Python

Utilisez le script `test_pump_timing.py` pour automatiser les tests :

```bash
python test_pump_timing.py
```

## 🔍 Informations Disponibles

### Temps de Démarrage
- **Timestamp absolu** : Moment exact du démarrage (millis())
- **Contexte** : Automatique ou manuel

### Temps d'Arrêt
- **Timestamp absolu** : Moment exact de l'arrêt (millis())
- **Durée d'activation** : Temps écoulé depuis le démarrage
- **Temps total cumulé** : Somme de toutes les durées d'activation
- **Nombre d'arrêts** : Compteur total des arrêts

### Statistiques en Temps Réel
- **État actuel** : En cours ou arrêtée
- **Durée actuelle** : Si en cours, temps depuis le démarrage
- **Temps depuis dernier arrêt** : Si arrêtée, temps écoulé

## 🎯 Cas d'Usage

### 1. Diagnostic de Performance
- Vérifier la durée moyenne d'activation
- Identifier les cycles de remplissage anormaux
- Surveiller la fréquence d'utilisation

### 2. Maintenance Préventive
- Suivre le temps total d'utilisation
- Détecter les usures prématurées
- Planifier les maintenances
- Éviter la marche à sec: vérifier les logs de verrouillage "réserve trop basse" (distance `wlTank` élevée)

### 3. Optimisation
- Analyser l'efficacité du remplissage
- Ajuster les durées d'activation
- Optimiser les seuils de déclenchement

## 🚀 Utilisation

1. **Compilation** : Les modifications sont automatiquement incluses
2. **Déploiement** : Flash de l'ESP32 avec le nouveau firmware
3. **Surveillance** : Les logs apparaissent automatiquement dans le moniteur série
4. **API** : Utilisation de l'endpoint `/pumpstats` pour intégration externe

## 📝 Notes Techniques

- **Précision** : Timestamps en millisecondes (millis())
- **Persistance** : Les statistiques sont réinitialisées au redémarrage
- **Performance** : Impact minimal sur les performances du système
- **Compatibilité** : Rétrocompatible avec le code existant
- **Sémantique capteurs** : Les capteurs ultrason mesurent une distance. Plus la distance est grande, plus le niveau est bas. "Aquarium trop plein" ⇢ `wlAqua < limFlood`. "Réserve trop basse" ⇢ `wlTank > tankThresholdCm`.