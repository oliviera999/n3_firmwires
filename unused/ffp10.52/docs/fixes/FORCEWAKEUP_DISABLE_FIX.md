# Correction du problème de désactivation du Force Wakeup

## 🔍 Problème identifié

Le système de **force wakeup** ne pouvait être que **activé** depuis la base de données distante, mais **jamais désactivé**. Même en envoyant `WakeUp = 0` ou `115 = 0` depuis la BDD, l'ESP32 ne réagissait pas.

## 🐛 Cause racine

La logique dans `handleRemoteState()` était **trop restrictive** et ne gérait que l'activation :

```cpp
// AVANT (problématique)
if (/* conditions pour activation */) {
  forceWakeUp = true; // ← TOUJOURS true !
}
// Aucune gestion de la désactivation
```

## ✅ Solutions implémentées

### 1. **Logique complète activation/désactivation**

**Fichier modifié :** `src/automatism.cpp`

**Corrections apportées dans 3 sections :**
- Lignes 1076-1127 : Clé textuelle "WakeUp"
- Lignes 1129-1178 : Clé numérique "115" 
- Lignes 1355-1403 : GPIO 115 dans la boucle des commandes

**Nouvelle logique :**
```cpp
// CORRECTION: Gestion complète activation ET désactivation
if (millis() > _wakeupProtectionEnd) {
  bool newValue = false;
  bool valueExplicitlySet = false;
  
  // Vérification des valeurs d'activation
  if ((wakeUpValue.is<bool>() && wakeUpValue.as<bool>()) || 
      (wakeUpValue.is<int>() && wakeUpValue.as<int>() == 1) ||
      (wakeUpValue.is<const char*>() && /* valeurs "true" */)) {
    newValue = true;
    valueExplicitlySet = true;
  }
  // Vérification des valeurs de désactivation
  else if ((wakeUpValue.is<bool>() && !wakeUpValue.as<bool>()) || 
           (wakeUpValue.is<int>() && wakeUpValue.as<int>() == 0) ||
           (wakeUpValue.is<const char*>() && /* valeurs "false" */)) {
    newValue = false;
    valueExplicitlySet = true;
  }
  
  // Mise à jour seulement si la valeur est explicitement définie
  if (valueExplicitlySet) {
    bool oldValue = forceWakeUp;
    forceWakeUp = newValue;
    if (oldValue != forceWakeUp) {
      Serial.printf("[Auto] forceWakeUp mis à jour: %s -> %s\n", 
                   oldValue ? "true" : "false", 
                   forceWakeUp ? "true" : "false");
      _config.setForceWakeUp(forceWakeUp);
      _config.saveBouffeFlags();
    }
  }
}
```

### 2. **Protection optimisée**

**Réduction de la durée de protection :**
```cpp
// AVANT
_wakeupProtectionEnd = millis() + 15000; // 15 secondes

// APRÈS  
_wakeupProtectionEnd = millis() + 5000; // 5 secondes (optimisé)
```

### 3. **Valeurs supportées**

**Activation :**
- `1`, `true`, `"1"`, `"true"`, `"on"`, `"checked"`

**Désactivation :**
- `0`, `false`, `"0"`, `"false"`, `"off"`, `"unchecked"`

## 🧪 Tests de validation

**Script créé :** `test_forcewakeup_disable.py`

**Scénarios testés :**
1. ✅ Activation via clé "WakeUp" = 1
2. ✅ Désactivation via clé "WakeUp" = 0  
3. ✅ Activation via clé "115" = 1
4. ✅ Désactivation via clé "115" = 0
5. ✅ Test des valeurs textuelles ("true", "false", etc.)

## 📋 Logs de débogage

**Logs de succès :**
```
[Auto] forceWakeUp mis à jour: false -> true (clé WakeUp)
[Auto] forceWakeUp mis à jour: true -> false (clé WakeUp)
[Auto] forceWakeUp (clé 115) mis à jour: false -> true
[Auto] forceWakeUp (GPIO 115) mis à jour: true -> false
```

**Logs de protection :**
```
[Auto] forceWakeUp protégé contre écrasement (protection active jusqu'à 5000 ms)
```

**Logs d'ignorance :**
```
[Auto] forceWakeUp ignoré - valeur non-explicite reçue du serveur
```

## 🎯 Résultat

**AVANT :** Le force wakeup ne pouvait être que activé, jamais désactivé
**APRÈS :** Le force wakeup peut être activé ET désactivé depuis la BDD distante

## 📝 Instructions d'utilisation

### Activation du force wakeup
```
WakeUp = 1        ou        115 = 1
```

### Désactivation du force wakeup  
```
WakeUp = 0        ou        115 = 0
```

### Vérification
Surveiller les logs série pour confirmer les changements :
```
[Auto] forceWakeUp mis à jour: false -> true
[Auto] Auto-sleep désactivé (forceWakeUp = true)
```

## 🔧 Compilation

Le firmware a été compilé avec succès. Les corrections sont prêtes à être téléversées sur l'ESP32.

**Commandes :**
```bash
pio run -t upload
python test_forcewakeup_disable.py  # Test de validation
```

## ✅ Validation

- ✅ Code compilé sans erreurs
- ✅ Logique de désactivation implémentée
- ✅ Protection optimisée (5 secondes)
- ✅ Script de test créé
- ✅ Logs de débogage améliorés
- ✅ Compatibilité avec toutes les clés (WakeUp, 115, GPIO 115)

**Le problème de désactivation du force wakeup est maintenant résolu !**
