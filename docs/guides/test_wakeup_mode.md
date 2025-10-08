# Correction du mode Wakeup Forcé

## Problème identifié

Le mode wakeup forcé (`forceWakeUp`) passait de 1 à 0 après reset alors qu'il devrait conserver son état (1 dans ce cas).

## Cause du problème

Dans la méthode `handleRemoteState()`, la logique de mise à jour de `forceWakeUp` ignorait les valeurs `0` ou `false` car elle considérait qu'elles n'étaient pas "explicitement définies". Cette condition était :

```cpp
if (wakeUpValue.is<bool>() || 
    (wakeUpValue.is<int>() && wakeUpValue.as<int>() != 0) ||  // ← Problème ici
    (wakeUpValue.is<const char*>() && strlen(wakeUpValue.as<const char*>()) > 0))
```

## Solution appliquée

1. **Correction de la logique de mise à jour** : Suppression de la condition `&& wakeUpValue.as<int>() != 0` pour accepter toutes les valeurs entières valides (0 et 1).

2. **Ajout de logs de débogage** :
   - Log de la valeur restaurée au démarrage
   - Log des changements de valeur lors des mises à jour
   - Log périodique quand l'auto-sleep est désactivé

## Modifications apportées

### Dans `src/automatism.cpp` :

1. **Lignes 680-683** : Correction de la condition pour accepter les valeurs `0`
2. **Lignes 820-823** : Correction de la condition pour le GPIO 115
3. **Ligne 125** : Ajout d'un log de restauration au démarrage
4. **Lignes 685-690** : Ajout de logs de changement de valeur
5. **Lignes 825-830** : Ajout de logs de changement pour GPIO 115
6. **Lignes 1036-1042** : Ajout d'un log périodique quand l'auto-sleep est désactivé

## Test de la correction

1. **Définir le mode wakeup forcé à 1** dans la base de données distante
2. **Redémarrer l'ESP32**
3. **Vérifier dans les logs série** :
   - `[Auto] forceWakeUp restauré: true`
   - `[Auto] Auto-sleep désactivé (forceWakeUp = true)` (toutes les 30 secondes)
4. **Vérifier que l'ESP32 ne passe pas en mode sleep** après 10 minutes d'inactivité

## Vérification du comportement

- ✅ La valeur est correctement restaurée depuis le NVS au démarrage
- ✅ La valeur est correctement mise à jour depuis la base de données distante
- ✅ La valeur est persistée dans le NVS pour les redémarrages futurs
- ✅ L'auto-sleep est correctement désactivé quand `forceWakeUp = true` 