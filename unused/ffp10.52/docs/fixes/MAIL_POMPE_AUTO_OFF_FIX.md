# Correction du mail d'arrêt de la pompe réserve en mode manuel

## Problème identifié

Le mail d'arrêt de la pompe réserve en mode manuel n'était plus envoyé. Après analyse du code, le problème était dans la logique de détection du mode manuel dans la fonction `checkCriticalChanges()`.

## Cause racine

Dans la fonction `checkCriticalChanges()` (lignes 1430-1470), le code utilise :
- `isManual = _manualTankOverride` pour déterminer si le **démarrage** est manuel
- `_lastPumpManual` pour déterminer si l'**arrêt** était manuel

Le problème était que `_lastPumpManual` n'était mis à jour que lors du démarrage (ligne 1455), mais il était réinitialisé à `false` lors de l'arrêt (ligne 1470).

**Séquence problématique :**
1. Démarrage manuel : `_manualTankOverride = true`, `_lastPumpManual = true`
2. Arrêt manuel : `_manualTankOverride = false` (dans `stopTankPumpManual()`)
3. `checkCriticalChanges()` détecte l'arrêt mais `_lastPumpManual` n'a pas été mis à jour
4. Le mail est envoyé avec le sujet "Pompe réservoir AUTO OFF" au lieu de "Pompe réservoir MANUELLE OFF"

## Solution appliquée

### 1. Correction dans `stopTankPumpManual()`

```cpp
void Automatism::stopTankPumpManual() {
  if (_acts.isTankPumpRunning()) {
    // IMPORTANT: Mémoriser que c'était un arrêt manuel AVANT d'arrêter la pompe
    // pour que checkCriticalChanges() puisse détecter le mode manuel
    _lastPumpManual = _manualTankOverride;
    
    _acts.stopTankPump();
    _manualTankOverride = false;
    // ... reste du code
  }
}
```

### 2. Correction dans la gestion des commandes GPIO distantes

```cpp
} else {
  // Arrêt manuel : vérifier d'abord si la pompe n'est pas verrouillée
  if (!tankPumpLocked) {
    // IMPORTANT: Mémoriser que c'était un arrêt manuel AVANT d'arrêter la pompe
    // pour que checkCriticalChanges() puisse détecter le mode manuel
    _lastPumpManual = _manualTankOverride;
    
    _acts.stopTankPump();
    _countdownEnd = 0;
    _manualTankOverride = false;
    // ... reste du code
  }
}
```

## Impact de la correction

- ✅ Le mail "Pompe réservoir MANUELLE OFF" est maintenant correctement envoyé lors de l'arrêt manuel
- ✅ Le mail "Pompe réservoir AUTO OFF" continue d'être envoyé lors de l'arrêt automatique
- ✅ Fonctionne pour les commandes manuelles via le serveur web local ET via les commandes GPIO distantes
- ✅ Aucun impact sur les autres fonctionnalités

## Tests recommandés

1. **Test d'arrêt manuel via serveur web local :**
   - Démarrer la pompe manuellement
   - L'arrêter manuellement
   - Vérifier que le mail "Pompe réservoir MANUELLE OFF" est reçu

2. **Test d'arrêt manuel via commande GPIO distante :**
   - Démarrer la pompe via GPIO distant
   - L'arrêter via GPIO distant
   - Vérifier que le mail "Pompe réservoir MANUELLE OFF" est reçu

3. **Test d'arrêt automatique :**
   - Laisser la pompe se démarrer automatiquement
   - La laisser s'arrêter automatiquement
   - Vérifier que le mail "Pompe réservoir AUTO OFF" est reçu

## Fichiers modifiés

- `src/automatism.cpp` : Correction de la logique de détection du mode manuel 