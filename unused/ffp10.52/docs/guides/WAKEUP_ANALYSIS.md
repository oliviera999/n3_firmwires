# Analyse détaillée du problème de réinitialisation du mode wakeup

## Problème identifié

Le mode wakeup forcé (`forceWakeUp`) se réinitialise après un reset de l'ESP32, alors qu'il devrait conserver son état persistant.

## Architecture du système wakeup

### 1. Variables impliquées

- **`forceWakeUp`** (bool) : Variable principale contrôlant le mode wakeup forcé
- **GPIO 115** : Clé numérique correspondant à `forceWakeUp` dans la base de données
- **"WakeUp"** : Clé textuelle alternative pour `forceWakeUp`
- **NVS** : Stockage persistant pour sauvegarder `forceWakeUp`

### 2. Flux de données

```
Base de données distante
    ↓ (clés "WakeUp" ou "115")
ESP32 handleRemoteState()
    ↓ (mise à jour forceWakeUp)
ConfigManager.setForceWakeUp()
    ↓ (sauvegarde NVS)
NVS Flash Memory
    ↓ (restauration au démarrage)
ConfigManager.getForceWakeUp()
    ↓ (initialisation)
Automatism.begin()
```

## Problèmes identifiés

### 1. **Conflit de synchronisation au démarrage**

**Problème** : Lors du démarrage, l'ESP32 :
1. Restaure `forceWakeUp` depuis le NVS (valeur correcte)
2. Appelle `handleRemoteState()` qui peut écraser cette valeur avec celle du serveur distant

**Code problématique** (lignes 139-144 dans `automatism.cpp`) :
```cpp
// Synchronisation initiale de forceWakeUp avec le serveur distant
if (WiFi.status() == WL_CONNECTED) {
  SensorReadings initialReadings = _sensors.read();
  sendFullUpdate(initialReadings, nullptr); // Envoie forceWakeUp via le champ WakeUp
  Serial.println(F("[Auto] Synchronisation initiale de forceWakeUp envoyée au serveur"));
}
```

### 2. **Logique de mise à jour dans `handleRemoteState()`**

**Problème** : La fonction `handleRemoteState()` met à jour `forceWakeUp` sans vérifier si c'est un démarrage récent.

**Code problématique** (lignes 768-786) :
```cpp
if (doc.containsKey("WakeUp")) {
  auto wakeUpValue = doc["WakeUp"];
  if (wakeUpValue.is<bool>() || 
      wakeUpValue.is<int>() ||
      (wakeUpValue.is<const char*>() && strlen(wakeUpValue.as<const char*>()) > 0)) {
    bool oldValue = forceWakeUp;
    forceWakeUp = isTrue(wakeUpValue);
    // Écrasement possible de la valeur restaurée depuis NVS
  }
}
```

### 3. **Gestion des clés numériques (GPIO 115)**

**Problème** : Même problème avec la clé numérique 115 (lignes 789-800) :
```cpp
if (doc.containsKey("115")) {
  auto v = doc["115"];
  if (v.is<bool>() || v.is<int>() || (v.is<const char*>() && strlen(v.as<const char*>()) > 0)) {
    forceWakeUp = isTrue(v);
    // Écrasement possible
  }
}
```

## Solutions implémentées

### 1. **Protection temporaire au démarrage**

**Solution** : Ajout d'une période de protection de 10 secondes après le démarrage.

**Code ajouté** :
```cpp
// Dans Automatism::begin()
_wakeupProtectionEnd = millis() + 10000; // 10 secondes de protection

// Dans handleRemoteState()
if ((wakeUpValue.is<bool>() || 
     wakeUpValue.is<int>() ||
     (wakeUpValue.is<const char*>() && strlen(wakeUpValue.as<const char*>()) > 0)) &&
    millis() > _wakeupProtectionEnd) {
  // Mise à jour autorisée seulement après la période de protection
}
```

### 2. **Logs de débogage améliorés**

**Ajout** : Logs détaillés pour tracer les changements de `forceWakeUp` :
```cpp
Serial.printf("[Auto] forceWakeUp protégé contre écrasement (protection active jusqu'à %lu ms)\n", _wakeupProtectionEnd);
```

### 3. **Gestion cohérente des clés**

**Amélioration** : Application de la même protection pour toutes les clés :
- Clé textuelle "WakeUp"
- Clé numérique "115"
- Gestion dans la boucle des GPIO

## Tests de validation

### 1. **Script de test automatisé**

Création de `test_wakeup_persistence.py` pour :
- Activer le mode wakeup
- Simuler un reset
- Vérifier la persistance
- Tester la désactivation

### 2. **Scénarios de test**

1. **Test de persistance** :
   - Activer `forceWakeUp = true`
   - Redémarrer l'ESP32
   - Vérifier que `forceWakeUp = true` après redémarrage

2. **Test de protection** :
   - Vérifier que les valeurs du serveur distant n'écrasent pas la valeur locale pendant les 10 premières secondes

3. **Test de synchronisation** :
   - Vérifier que la valeur locale est bien synchronisée avec le serveur distant après la période de protection

## Points d'attention

### 1. **Ordre d'exécution critique**

L'ordre des opérations au démarrage est crucial :
1. Restauration depuis NVS
2. Protection temporaire
3. Synchronisation avec serveur distant (après protection)

### 2. **Gestion des timeouts**

La protection de 10 secondes doit être suffisante pour :
- Permettre la restauration depuis NVS
- Éviter les conflits avec les premières requêtes distantes
- Permettre la synchronisation ultérieure

### 3. **Compatibilité avec l'existant**

Les modifications sont rétrocompatibles :
- Pas de changement d'API
- Conservation des clés existantes
- Même comportement après la période de protection

## Monitoring et débogage

### 1. **Logs à surveiller**

```
[Auto] forceWakeUp restauré depuis config: true
[Auto] forceWakeUp protégé contre écrasement (protection active jusqu'à 10000 ms)
[Auto] forceWakeUp mis à jour: false -> true
```

### 2. **Indicateurs de succès**

- `forceWakeUp` conserve sa valeur après reset
- Pas d'écrasement pendant la période de protection
- Synchronisation correcte après la protection

## Conclusion

Le problème de réinitialisation du mode wakeup était causé par un conflit de synchronisation au démarrage. La solution implémentée ajoute une protection temporaire qui empêche l'écrasement de la valeur restaurée depuis le NVS par les données du serveur distant pendant les premières secondes après le démarrage.

Cette approche garantit la persistance du mode wakeup tout en maintenant la synchronisation avec le serveur distant une fois l'ESP32 complètement initialisé. 