# Correction du problème forceWakeUp après reset

## Problème identifié

Après un reset de l'ESP32, la valeur `forceWakeUp` passait de 1 à 0 au lieu de conserver sa valeur précédente. Cette valeur était envoyée à la base de données distante, causant une perte de l'état de veille forcée.

## Cause racine

Le problème venait d'un **conflit de synchronisation au démarrage** :

1. **Restauration depuis NVS** : `forceWakeUp = true` (valeur correcte)
2. **Synchronisation avec serveur distant** : Le serveur répondait avec une ancienne valeur (0)
3. **Écrasement** : La valeur du serveur (0) écrasait la valeur locale (1)

### Séquence problématique avant correction :

```
1. Démarrage ESP32
   ↓
2. Restauration depuis NVS : forceWakeUp = true
   ↓
3. Synchronisation sortante : sendFullUpdate() → forceWakeUp=1
   ↓
4. Premier handleRemoteState() : Serveur répond avec ancienne valeur (0)
   ↓
5. CONFLIT : Valeur serveur (0) écrase valeur locale (1)
```

## Solution implémentée

### 1. **Logique de mise à jour plus stricte**

**Problème** : La logique acceptait toutes les valeurs non-vides du serveur, y compris les valeurs par défaut (0, false, null).

**Solution** : Modification pour n'accepter que les valeurs **explicitement "true"** :

```cpp
// AVANT (problématique)
if ((wakeUpValue.is<bool>() || 
     (wakeUpValue.is<int>() && wakeUpValue.as<int>() != 0) ||
     (wakeUpValue.is<const char*>() && strlen(wakeUpValue.as<const char*>()) > 0))) {
  forceWakeUp = isTrue(wakeUpValue); // Acceptait 0, false, etc.
}

// APRÈS (corrigé)
if ((wakeUpValue.is<bool>() && wakeUpValue.as<bool>()) || 
     (wakeUpValue.is<int>() && wakeUpValue.as<int>() == 1) ||
     (wakeUpValue.is<const char*>() && strlen(wakeUpValue.as<const char*>()) > 0 && 
      (String(wakeUpValue.as<const char*>()) == "1" || 
       String(wakeUpValue.as<const char*>()) == "true" || 
       String(wakeUpValue.as<const char*>()) == "on" || 
       String(wakeUpValue.as<const char*>()) == "checked"))) {
  forceWakeUp = true; // Seulement si explicitement "true"
}
```

### 2. **Protection renforcée au démarrage**

- **Durée de protection** : 15 secondes après le démarrage
- **Délai de synchronisation** : 3 secondes avant la première synchronisation
- **Protection contre les valeurs non-explicites** : Ignore les valeurs 0/false/null du serveur

### 3. **Cohérence dans toutes les sections**

La même logique stricte a été appliquée dans :
- `handleRemoteState()` pour la clé "WakeUp"
- `handleRemoteState()` pour la clé numérique "115"
- Chargement depuis NVS dans `begin()`
- Gestion GPIO 115 dans la boucle des commandes

## Modifications apportées

### Dans `src/automatism.cpp` :

1. **Lignes 1055-1080** : Logique stricte pour la clé "WakeUp"
2. **Lignes 1082-1100** : Logique stricte pour la clé "115"
3. **Lignes 115-125** : Logique stricte dans le chargement NVS
4. **Lignes 1280-1300** : Logique stricte pour GPIO 115

## Nouvelle séquence corrigée

```
1. Démarrage ESP32
   ↓
2. Restauration depuis NVS : forceWakeUp = true
   ↓
3. Protection activée : _wakeupProtectionEnd = millis() + 15000
   ↓
4. Délai de 3 secondes
   ↓
5. Synchronisation différée : sendFullUpdate() → forceWakeUp=1
   ↓
6. handleRemoteState() appelé avec protection active
   ↓
7. ✅ Valeur locale protégée contre écrasement
```

## Tests de validation

### 1. **Test de persistance après reset**
- Activer `forceWakeUp = 1`
- Effectuer un reset (GPIO 110)
- Vérifier que `forceWakeUp` reste à 1

### 2. **Test de protection contre écrasement**
- Activer `forceWakeUp = 1`
- Simuler une réponse serveur avec `forceWakeUp = 0`
- Vérifier que la valeur locale reste à 1

### 3. **Logs à surveiller**
```
[Auto] forceWakeUp restauré depuis config: true
[Auto] forceWakeUp protégé contre écrasement (protection active jusqu'à 15000 ms)
[Auto] forceWakeUp ignoré - valeur non-explicite reçue du serveur
```

## Points d'attention

### 1. **Compatibilité**
- Les modifications sont rétrocompatibles
- Pas de changement d'API
- Conservation des clés existantes

### 2. **Comportement attendu**
- `forceWakeUp = 1` : Mode veille forcée activé
- `forceWakeUp = 0` : Mode veille forcée désactivé (par défaut)
- Seules les valeurs explicitement "true" activent le mode

### 3. **Monitoring**
- Logs détaillés pour le débogage
- Protection temporaire visible dans les logs
- Détection des valeurs ignorées

## Conclusion

Le problème de réinitialisation du mode wakeup était causé par une logique trop permissive qui acceptait les valeurs par défaut du serveur distant.

La solution implémentée garantit :
- ✅ Persistance du mode wakeup après reset
- ✅ Protection contre l'écrasement par des valeurs non-explicites
- ✅ Synchronisation correcte avec le serveur distant
- ✅ Logs détaillés pour le débogage 