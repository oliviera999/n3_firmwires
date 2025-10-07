# Correction du problème forceWakeUp après reset

## Problème identifié

Après un reset de l'ESP32, la valeur `forceWakeUp` passait de 1 à 0 au lieu de conserver sa valeur précédente. Cette valeur était envoyée à la base de données distante, causant une perte de l'état de veille forcée.

## Cause racine

Le problème venait d'un conflit de synchronisation au démarrage :

1. **Restauration depuis NVS** : `forceWakeUp = true` (valeur correcte)
2. **Synchronisation avec serveur distant** : Le serveur répondait avec une ancienne valeur (0)
3. **Écrasement** : La valeur du serveur (0) écrasait la valeur locale (1)

## Solutions implémentées

### 1. **Protection renforcée au démarrage**

- **Durée de protection augmentée** : De 10 à 15 secondes
- **Délai de synchronisation augmenté** : De 2 à 3 secondes
- **Protection contre les valeurs vides** : Ignore les valeurs 0/vides du serveur

### 2. **Logique de mise à jour améliorée**

```cpp
// AVANT (problématique)
if (wakeUpValue.is<int>()) forceWakeUp = wakeUpValue.as<int>() != 0;

// APRÈS (corrigé)
if ((wakeUpValue.is<int>() && wakeUpValue.as<int>() != 0)) {
    // Seulement si la valeur n'est pas 0
    forceWakeUp = isTrue(wakeUpValue);
}
```

### 3. **Sauvegarde immédiate**

Ajout de sauvegarde immédiate dans la NVS lors de la restauration :

```cpp
// Sauvegarde immédiate pour persistance
_config.setForceWakeUp(forceWakeUp);
```

### 4. **Logs de débogage améliorés**

```cpp
[Auto] forceWakeUp restauré depuis config: true
[Auto] forceWakeUp protégé contre écrasement (protection active jusqu'à 15000 ms)
[Auto] forceWakeUp ignoré - valeur vide/null/0 reçue du serveur
```

## Modifications apportées

### Dans `src/automatism.cpp` :

1. **Ligne 18** : Protection augmentée à 15 secondes
2. **Ligne 145** : Délai de synchronisation à 3 secondes
3. **Lignes 1051-1070** : Protection contre valeurs vides pour clé "WakeUp"
4. **Lignes 1072-1090** : Protection contre valeurs vides pour clé "115"
5. **Lignes 1250-1270** : Protection contre valeurs vides dans boucle GPIO
6. **Lignes 95-100** : Sauvegarde immédiate lors de restauration
7. **Lignes 110-115** : Sauvegarde immédiate pour clé "WakeUp"

### Corrections de compilation :

- Ajout de `nullptr` pour les appels `sendFullUpdate()` manquants
- Méthodes publiques déplacées dans la section publique

## Tests de validation

### 1. **Test de persistance**
- Activer `forceWakeUp = true`
- Redémarrer l'ESP32
- Vérifier que `forceWakeUp = true` après redémarrage

### 2. **Test de protection**
- Vérifier les logs de protection pendant les 15 premières secondes
- Confirmer que les valeurs vides sont ignorées

### 3. **Test de synchronisation**
- Vérifier que la valeur locale est bien synchronisée avec le serveur distant après protection

## Logs à surveiller

### Logs de succès :
```
[Auto] forceWakeUp restauré depuis config: true
[Auto] forceWakeUp protégé contre écrasement (protection active jusqu'à 15000 ms)
[Auto] forceWakeUp ignoré - valeur vide/null/0 reçue du serveur
[Auto] Synchronisation initiale de forceWakeUp envoyée au serveur (après délai)
```

### Logs d'erreur à détecter :
```
[Auto] forceWakeUp mis à jour: true -> false  // Pendant la protection
```

## Compilation et upload

- **Compilation** : `pio run -e esp32dev` ✅
- **Upload** : `pio run -e esp32dev -t upload` ✅
- **Firmware** : Uploadé avec succès sur ESP32-WROOM

## Résultat attendu

Après cette correction, `forceWakeUp` devrait :
1. ✅ Conserver sa valeur après reset
2. ✅ Ne pas être écrasé par des valeurs vides du serveur
3. ✅ Être correctement synchronisé avec le serveur distant
4. ✅ Maintenir l'état de veille forcée de manière persistante

## Monitoring post-déploiement

Surveiller les logs série pour confirmer :
- La restauration correcte depuis la NVS
- La protection contre l'écrasement
- L'ignorance des valeurs vides du serveur
- La synchronisation correcte après la période de protection 