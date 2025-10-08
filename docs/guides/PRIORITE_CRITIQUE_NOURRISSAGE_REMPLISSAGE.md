# 🚨 PRIORITÉ ABSOLUE : NOURRISSAGE ET REMPLISSAGE

## 📋 Objectif

Garantir que les fonctions de **nourrissage automatique/manuel** et de **remplissage automatique/manuel** ont la **priorité absolue** dans le système, avec un **respect strict des temps d'activation**.

## 🔧 Modifications Apportées

### 1. Réorganisation de l'Ordre d'Exécution

**Avant :**
```cpp
void Automatism::update(const SensorReadings& readings) {
  checkNewDay();
  handleRemoteState();
  handleRefill(readings);    // 3ème priorité
  handleMaree(readings);
  handleFeeding();           // 4ème priorité
  handleAlerts(readings);
  handleAutoSleep(readings);
  // ...
}
```

**Après :**
```cpp
void Automatism::update(const SensorReadings& readings) {
  // ========================================
  // PRIORITÉ ABSOLUE : NOURRISSAGE ET REMPLISSAGE
  // ========================================
  
  // 1. Vérification d'un nouveau jour pour reset des flags de bouffe (CRITIQUE)
  checkNewDay();

  // 2. Gestion des automatismes CRITIQUES en priorité absolue
  handleFeeding();        // PRIORITÉ 1 : Nourrissage automatique (temps critiques)
  handleRefill(readings); // PRIORITÉ 2 : Remplissage automatique (temps critiques)
  
  // 3. Gestion des automatismes secondaires
  handleMaree(readings);
  handleAlerts(readings);

  // 4. Récupération de l'état distant (priorité moindre)
  handleRemoteState();

  // 5. Mise en veille automatique (DERNIÈRE PRIORITÉ)
  handleAutoSleep(readings);
  // ...
}
```

### 2. Protection Contre l'Interférence du Sleep

**Nouvelles vérifications dans `handleAutoSleep()` :**
```cpp
// Ne pas dormir si la pompe de remplissage fonctionne
if (_acts.isTankPumpRunning()) {
  _lastWakeMs = millis();
  Serial.println(F("[Auto] Auto-sleep différé - pompe de remplissage active"));
  return;
}

// Ne pas dormir si un nourrissage est en cours
if (_currentFeedingPhase != FeedingPhase::NONE) {
  _lastWakeMs = millis();
  Serial.println(F("[Auto] Auto-sleep différé - nourrissage en cours"));
  return;
}

// Ne pas dormir si un décompte est en cours
if (millis() < _countdownEnd) {
  _lastWakeMs = millis();
  Serial.println(F("[Auto] Auto-sleep différé - décompte en cours"));
  return;
}
```

### 3. Logs Détaillés et Monitoring

**Ajout de logs critiques pour toutes les fonctions prioritaires :**

#### Nourrissage Automatique
```cpp
Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE MATIN ==="));
Serial.printf("[CRITIQUE] Heure actuelle: %02d:%02d, Heure configurée: %02d:00\n", 
             currentHour, currentMinute, feedMorning);
Serial.printf("[CRITIQUE] Durées configurées - Gros: %u s, Petits: %u s\n", 
             feedBigDur, feedSmallDur);

// EXÉCUTION IMMÉDIATE - PRIORITÉ ABSOLUE
unsigned long startTime = millis();
_acts.feedBigFish(feedBigDur);
_acts.feedSmallFish(feedSmallDur);
unsigned long executionTime = millis() - startTime;
Serial.printf("[CRITIQUE] Temps d'exécution total: %lu ms\n", executionTime);
```

#### Remplissage Automatique
```cpp
Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE AUTOMATIQUE ==="));
Serial.printf("[CRITIQUE] Niveau aquarium (distance): %d cm, Seuil: %d cm\n", r.wlAqua, aqThresholdCm);
Serial.printf("[CRITIQUE] Durée configurée: %lu s\n", refillDurationMs / 1000);

// DÉMARRAGE IMMÉDIAT - PRIORITÉ ABSOLUE
unsigned long startTime = millis();
_acts.startTankPump();
unsigned long executionTime = millis() - startTime;
Serial.printf("[CRITIQUE] Pompe démarrée en %lu ms\n", executionTime);
```

### 4. Fonctions Manuelles Prioritaires

**Nourrissage manuel :**
```cpp
void Automatism::manualFeedSmall(){
  // ========================================
  // FONCTION CRITIQUE : NOURRISSAGE MANUEL PETITS POISSONS
  // PRIORITÉ ABSOLUE - EXÉCUTION IMMÉDIATE
  // ========================================
  
  Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE MANUEL PETITS ==="));
  
  // EXÉCUTION IMMÉDIATE - PRIORITÉ ABSOLUE
  unsigned long startTime = millis();
  _acts.feedSmallFish(feedSmallDur);
  _acts.feedBigFish(feedBigDur);
  unsigned long executionTime = millis() - startTime;
  Serial.printf("[CRITIQUE] Temps d'exécution total: %lu ms\n", executionTime);
}
```

**Remplissage manuel :**
```cpp
void Automatism::startTankPumpManual() {
  // ========================================
  // FONCTION CRITIQUE : DÉMARRAGE MANUEL POMPE REMPLISSAGE
  // PRIORITÉ ABSOLUE - EXÉCUTION IMMÉDIATE
  // ========================================
  
  Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE MANUEL ==="));
  
  // DÉMARRAGE IMMÉDIAT - PRIORITÉ ABSOLUE
  unsigned long startTime = millis();
  _acts.startTankPump();
  unsigned long executionTime = millis() - startTime;
  Serial.printf("[CRITIQUE] Pompe démarrée en %lu ms\n", executionTime);
}
```

## 🎯 Bénéfices

### 1. **Priorité Absolue Garantie**
- Les fonctions de nourrissage et remplissage s'exécutent **en premier**
- Aucune interférence possible avec les autres automatismes
- Respect strict des temps d'activation

### 2. **Protection Contre le Sleep**
- Le système ne peut pas entrer en veille pendant les opérations critiques
- Réinitialisation automatique du chronomètre de veille
- Logs détaillés pour le monitoring

### 3. **Monitoring Avancé**
- Logs détaillés avec préfixe `[CRITIQUE]`
- Mesure des temps d'exécution
- Traçabilité complète des opérations

### 4. **Robustesse**
- Vérifications multiples avant le sleep
- Protection contre les interruptions
- Gestion d'erreurs renforcée

## 📊 Ordre de Priorité Final

| Priorité | Fonction | Description |
|----------|----------|-------------|
| **1** | `checkNewDay()` | Reset des flags de nourrissage |
| **2** | `handleFeeding()` | Nourrissage automatique (matin/midi/soir) |
| **3** | `handleRefill()` | Remplissage automatique |
| **4** | `handleMaree()` | Gestion de la marée |
| **5** | `handleAlerts()` | Gestion des alertes |
| **6** | `handleRemoteState()` | Communication avec le serveur |
| **7** | `handleAutoSleep()` | Mise en veille (avec protections) |

## 🔍 Monitoring

### Logs à Surveiller
```
[CRITIQUE] === DÉBUT NOURRISSAGE MATIN ===
[CRITIQUE] Heure actuelle: 08:00, Heure configurée: 08:00
[CRITIQUE] Durées configurées - Gros: 10 s, Petits: 10 s
[CRITIQUE] Gros poissons nourris - durée: 10 s
[CRITIQUE] Petits poissons nourris - durée: 10 s
[CRITIQUE] Temps d'exécution total: 45 ms
[CRITIQUE] Bouffe matin MARQUÉE COMME EFFECTUÉE
[CRITIQUE] === FIN NOURRISSAGE MATIN ===
```

### Vérifications
- ✅ Temps d'exécution < 100ms
- ✅ Respect des heures configurées
- ✅ Respect des durées configurées
- ✅ Marquage correct des flags
- ✅ Pas d'interférence du sleep

## ⚠️ Points d'Attention

1. **Temps d'exécution** : Les fonctions critiques doivent s'exécuter rapidement (< 100ms)
2. **Logs** : Surveiller les logs `[CRITIQUE]` pour détecter les problèmes
3. **Sleep** : Vérifier que le sleep ne s'active pas pendant les opérations critiques
4. **Priorité** : L'ordre d'exécution est maintenant garanti

## 🚀 Résultat

Les fonctions de **nourrissage et remplissage** ont maintenant la **priorité absolue** dans le système, avec un **respect strict des temps d'activation** et une **protection complète contre les interférences**. 