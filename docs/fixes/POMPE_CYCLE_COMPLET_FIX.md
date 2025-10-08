# 🔧 Correction du Comportement de la Pompe de Remplissage

## 🎯 Problème Identifié

**Symptôme :** Quand la pompe commence son cycle de remplissage, elle s'arrête prématurément si le niveau d'eau de l'aquarium repasse au-dessus de la limite de remplissage.

**Cause :** La fonction `handleRefill()` contenait une condition d'arrêt anticipé qui s'exécutait même quand le niveau d'eau redevenait acceptable pendant le cycle de remplissage.

## 🔍 Analyse du Code

### Code Problématique (Avant)
```cpp
// ========================================
// 3. ARRÊT ANTICIPÉ SI NIVEAU OK - OPTIMISATION
// ========================================
if (!_manualTankOverride && _acts.isTankPumpRunning() && r.wlAqua <= aqThresholdCm) {
  Serial.println(F("[CRITIQUE] === ARRÊT ANTICIPÉ REMPLISSAGE ==="));
  // ... arrêt de la pompe
}
```

**Problème :** Cette condition arrêtait la pompe dès que le niveau d'eau repassait sous le seuil, même si le cycle n'était pas terminé.

## ✅ Solution Appliquée

### Code Corrigé (Après)
```cpp
// ========================================
// 3. ARRÊT ANTICIPÉ DÉSACTIVÉ - GARANTIE DE CYCLE COMPLET
// ========================================
// COMPORTEMENT ATTENDU: Une fois la pompe démarrée (auto ou manuel), 
// elle doit terminer son cycle complet (refillDurationMs) même si le niveau 
// d'eau repasse au-dessus de la limite de remplissage.
// 
// RAISON: Éviter les cycles courts qui peuvent causer des problèmes de stabilité
// et garantir un remplissage efficace même si le niveau fluctue.
// 
// NOTE: En mode manuel, la pompe doit terminer son cycle complet
// NOTE: En mode auto, la pompe doit aussi terminer son cycle complet une fois démarrée
// L'arrêt anticipé n'est plus autorisé pour garantir la stabilité du cycle
if (false && !_manualTankOverride && _acts.isTankPumpRunning() && r.wlAqua <= aqThresholdCm) {
  // Cette condition est désactivée pour garantir que la pompe termine toujours son cycle complet
  // ... code d'arrêt (jamais exécuté)
}
```

## 🎯 Comportement Garanti

### ✅ Nouveau Comportement
1. **Démarrage automatique :** La pompe démarre quand `r.wlAqua > aqThresholdCm`
2. **Cycle complet :** Une fois démarrée, la pompe tourne pendant exactement `refillDurationMs` (120s par défaut)
3. **Arrêt forcé :** La pompe s'arrête uniquement après la durée complète du cycle
4. **Pas d'arrêt anticipé :** Même si le niveau repasse sous le seuil pendant le cycle, la pompe continue

### 🛡️ Sécurités Conservées
- **Sécurité réserve basse :** Si la distance réservoir est supérieure au seuil (`r.wlTank > tankThresholdCm`), la pompe est verrouillée pour éviter la marche à sec. Déverrouillage automatique quand la distance réservoir repasse sous `tankThresholdCm - 5` avec 3 confirmations.
- **Sécurité aquarium trop plein :** Si l'aquarium est trop plein (`r.wlAqua < limFlood`), la pompe est verrouillée et ne peut pas démarrer; déverrouillage automatique quand l'état de trop-plein disparaît (hystérésis/temporisations appliquées).
- **Arrêt après durée max :** Après `refillDurationMs` (sécurité critique)
- **Gestion des erreurs :** Compteur de tentatives et verrouillage en cas d'échec

## 📊 Avantages de cette Correction

### 1. **Stabilité du Remplissage**
- Évite les cycles courts qui peuvent causer des problèmes
- Garantit un remplissage efficace même avec des fluctuations de niveau

### 2. **Prévisibilité**
- Durée de cycle fixe et connue
- Comportement déterministe

### 3. **Robustesse**
- Protection contre les arrêts intempestifs
- Meilleure gestion des variations de niveau d'eau

## 🔧 Fichiers Modifiés

- `src/automatism.cpp` : Fonction `handleRefill()` - Désactivation de l'arrêt anticipé

## 🧪 Tests Recommandés

1. **Test de cycle complet :** Vérifier que la pompe tourne pendant exactement `refillDurationMs`
2. **Test de fluctuation :** Simuler des variations de niveau pendant le cycle
3. **Test de sécurité :** Vérifier que les sécurités (débordement réservoir) fonctionnent toujours

## 📝 Notes Techniques

- La condition d'arrêt anticipé est désactivée avec `if (false && ...)` pour conserver le code en commentaire
- Le comportement est maintenant identique en mode automatique et manuel
- La durée de cycle est configurée via `refillDurationMs` (120 secondes par défaut) 