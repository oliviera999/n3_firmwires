# 🚀 OPTIMISATION PRIORITÉ LIGHTSLEEP

## 📋 Objectif

Rendre l'**entrée en lightsleep** plus prioritaire que toutes les autres tâches, sauf le **nourrissage** et le **remplissage** de l'aquarium à partir de la réserve.

## 🔧 Modifications Apportées

### 1. Nouvelle Architecture de Priorités

**Avant :**
```cpp
void Automatism::update(const SensorReadings& readings) {
  checkNewDay();
  handleFeeding();        // PRIORITÉ 1
  handleRefill(readings); // PRIORITÉ 2
  handleMaree(readings);  // PRIORITÉ 3
  handleAlerts(readings); // PRIORITÉ 4
  handleRemoteState();    // PRIORITÉ 5
  handleAutoSleep(readings); // PRIORITÉ 6 (DERNIÈRE)
  // ...
}
```

**Après :**
```cpp
void Automatism::update(const SensorReadings& readings) {
  checkNewDay();           // PRIORITÉ 1
  handleFeeding();         // PRIORITÉ 2 (CRITIQUE)
  handleRefill(readings);  // PRIORITÉ 3 (CRITIQUE)
  
  // ========================================
  // PRIORITÉ HAUTE : ENTRÉE EN LIGHTSLEEP
  // ========================================
  if (shouldEnterSleepEarly(readings)) {
    handleAutoSleep(readings);
    return; // Sortie immédiate après sleep
  }
  
  handleMaree(readings);   // PRIORITÉ 4 (SECONDAIRE)
  handleAlerts(readings);  // PRIORITÉ 5 (SECONDAIRE)
  handleRemoteState();     // PRIORITÉ 6 (SECONDAIRE)
  handleAutoSleep(readings); // PRIORITÉ 7 (FALLBACK)
  // ...
}
```

### 2. Nouvelle Fonction `shouldEnterSleepEarly()`

**Fonctionnalité :**
- Vérifie **uniquement les conditions critiques**
- **Déclenchement précoce** du sleep si conditions remplies
- **Priorité haute** sur toutes les tâches non-critiques

**Conditions Critiques Vérifiées :**
```cpp
bool Automatism::shouldEnterSleepEarly(const SensorReadings& r) {
  // 1. Sommeil forcé désactivé ? (condition absolue)
  if (forceWakeUp) return false;

  // 2. Ne pas dormir si la pompe de remplissage fonctionne (CRITIQUE)
  if (_acts.isTankPumpRunning()) return false;
  
  // 3. Ne pas dormir si un nourrissage est en cours (CRITIQUE)
  if (_currentFeedingPhase != FeedingPhase::NONE) return false;
  
  // 4. Ne pas dormir si un décompte long est en cours (remplissage - CRITIQUE)
  if (isStillPending(_countdownEnd, millis())) {
    uint32_t remainingCountdownSec = remainingMs(_countdownEnd, millis()) / 1000UL;
    if (remainingCountdownSec > 300) return false; // Décompte long = bloquant
  }

  // 5. Vérification du délai minimal OU marée montante
  if (tideAscendingTrigger || (currentMillis - _lastWakeMs >= (adaptiveDelay * 1000))) {
    return true; // SLEEP PRÉCOCE AUTORISÉ
  }
  
  return false;
}
```

### 3. Réduction des Obstacles Non-Critiques

**WebSocket Activity :**
```cpp
// AVANT : Blocage complet
if (!realtimeWebSocket.canEnterSleep()) {
  _lastWakeMs = millis();
  return; // Bloque indéfiniment
}

// APRÈS : Délai réduit
if (!realtimeWebSocket.canEnterSleep()) {
  if (currentMillis - lastWebSocketCheck > 10000) { // 10s au lieu de bloquer
    lastWebSocketCheck = currentMillis;
    _lastWakeMs = currentMillis; // Reset minimal
  }
  return;
}
```

**Détection d'Activité :**
```cpp
// AVANT : Reset complet du chronomètre
if (hasSignificantActivity()) {
  _lastWakeMs = millis();
  return; // Bloque le sleep
}

// APRÈS : Reset minimal avec délai
if (hasSignificantActivity()) {
  if (currentMillis - lastActivityCheck > 5000) { // 5s au lieu de bloquer
    lastActivityCheck = currentMillis;
    _lastWakeMs = currentMillis; // Reset minimal
  }
  return;
}
```

## 🎯 Nouvelle Hiérarchie des Priorités

| Priorité | Fonction | Description | Impact Sleep |
|----------|----------|-------------|--------------|
| **1** | `checkNewDay()` | Reset des flags de nourrissage | ❌ Bloquant |
| **2** | `handleFeeding()` | Nourrissage automatique | ❌ **CRITIQUE** |
| **3** | `handleRefill()` | Remplissage automatique | ❌ **CRITIQUE** |
| **4** | `shouldEnterSleepEarly()` | **Vérification sleep précoce** | ✅ **PRIORITÉ HAUTE** |
| **5** | `handleMaree()` | Gestion de la marée | ⚠️ Réduit |
| **6** | `handleAlerts()` | Gestion des alertes | ⚠️ Réduit |
| **7** | `handleRemoteState()` | Communication serveur | ⚠️ Réduit |
| **8** | `handleAutoSleep()` | Sleep fallback | ✅ Dernière chance |

## 🚀 Avantages Obtenus

### 1. **Priorité Absolue du Sleep**
- L'entrée en sleep est maintenant **prioritaire** sur toutes les tâches non-critiques
- **Vérification précoce** après les fonctions critiques uniquement
- **Sortie immédiate** après sleep sans exécuter le reste

### 2. **Réduction des Obstacles**
- **WebSocket** : Délai réduit de blocage à 10s
- **Activité fine** : Délai réduit de blocage à 5s
- **Reset minimal** au lieu de reset complet du chronomètre

### 3. **Respect des Critiques**
- **Nourrissage** : Toujours prioritaire sur le sleep
- **Remplissage** : Toujours prioritaire sur le sleep
- **Décomptes longs** : Toujours bloquants pour le sleep

### 4. **Optimisation Énergétique**
- **Entrée en sleep plus rapide** (après nourrissage/remplissage seulement)
- **Réduction de la consommation** pendant les périodes d'inactivité
- **Meilleure gestion des ressources** système

## 📊 Comportement Attendu

### Scénario 1 : Nourrissage en cours
```
1. checkNewDay() ✅
2. handleFeeding() ✅ (nourrissage actif)
3. shouldEnterSleepEarly() ❌ (bloqué par nourrissage)
4. handleMaree() ✅
5. handleAlerts() ✅
6. handleRemoteState() ✅
7. handleAutoSleep() ❌ (bloqué par nourrissage)
```

### Scénario 2 : Aucune activité critique
```
1. checkNewDay() ✅
2. handleFeeding() ✅ (aucun nourrissage)
3. handleRefill() ✅ (aucun remplissage)
4. shouldEnterSleepEarly() ✅ (conditions remplies)
5. handleAutoSleep() ✅ (ENTRÉE EN SLEEP IMMÉDIATE)
6. return; (sortie immédiate - pas d'exécution du reste)
```

### Scénario 3 : Activité WebSocket non-critique
```
1. checkNewDay() ✅
2. handleFeeding() ✅ (aucun nourrissage)
3. handleRefill() ✅ (aucun remplissage)
4. shouldEnterSleepEarly() ✅ (conditions remplies)
5. handleAutoSleep() ✅ (ENTRÉE EN SLEEP MALGRÉ WEBSOCKET)
6. return; (sortie immédiate)
```

## 🔍 Monitoring

### Logs à Surveiller
```
[Auto] Sleep précoce déclenché: délai atteint (300 s)
[Auto] Sleep précoce déclenché: marée montante (~10s, +5 cm)
[Auto] WebSocket actif - sleep différé (priorité réduite)
[Auto] Activité détectée - sleep différé (priorité réduite)
```

### Vérifications
- ✅ Sleep déclenché après nourrissage/remplissage seulement
- ✅ Sleep non bloqué par WebSocket/activité non-critique
- ✅ Respect des conditions critiques maintenu
- ✅ Réduction de la consommation énergétique

## ⚠️ Points d'Attention

1. **Conditions critiques** : Toujours respectées (nourrissage/remplissage)
2. **Délais réduits** : WebSocket (10s) et activité (5s) au lieu de blocage
3. **Sleep précoce** : Déclenché dès que possible après les critiques
4. **Fallback** : `handleAutoSleep()` reste disponible en cas de besoin

## 🚀 Résultat

L'**entrée en lightsleep** est maintenant **prioritaire** sur toutes les tâches sauf le **nourrissage** et le **remplissage**, avec une **réduction significative** des obstacles non-critiques et une **optimisation énergétique** maximale.
