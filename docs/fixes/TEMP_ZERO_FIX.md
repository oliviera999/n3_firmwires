# Correction des Températures à 0°C

## Problème identifié

Des mesures de température de l'eau à exactement 0°C étaient parfois enregistrées dans la base de données, ce qui est physiquement impossible pour de l'eau dans un aquarium.

## Cause racine

Le problème venait de **deux sources** :

1. **Valeur par défaut incorrecte** dans `src/automatism.cpp` ligne 222 :
   ```cpp
   readings.tempWater = 0;  // ❌ 0°C est impossible
   ```

2. **Validation insuffisante** : La validation `<= 0.0f` rejette les valeurs négatives mais **accepte 0.0°C**, ce qui est physiquement impossible.

## Corrections apportées

### 1. Correction de la valeur par défaut dans `automatism.cpp`

**Fichier : `src/automatism.cpp`**

```cpp
// AVANT (incorrect)
readings.tempWater = 0;
readings.tempAir = 0;
readings.humidity = 0;

// APRÈS (correct)
readings.tempWater = NAN;  // ✅ NaN indique une erreur de lecture
readings.tempAir = NAN;    // ✅ NaN indique une erreur de lecture
readings.humidity = NAN;   // ✅ NaN indique une erreur de lecture
```

### 2. Amélioration des commentaires de validation

**Fichiers : `src/system_sensors.cpp` et `src/web_client.cpp`**

- Ajout de commentaires explicites indiquant que 0°C est rejeté car physiquement impossible
- Clarification que la validation `<= 0.0f` rejette à la fois les valeurs négatives ET 0°C

## Logique de validation

### Température de l'eau
- **Valeurs acceptées** : ]0.0°C ; 60.0°C[
- **Valeurs rejetées** : ≤ 0.0°C (inclut 0°C), ≥ 60.0°C, NaN
- **Action** : Remplacement par NaN

### Température de l'air
- **Valeurs acceptées** : ]5.0°C ; 60.0°C[
- **Valeurs rejetées** : ≤ 5.0°C, ≥ 60.0°C, NaN
- **Action** : Remplacement par NaN

## Impact

- **Sécurité** : Aucune température à 0°C ne sera plus envoyée au serveur
- **Fiabilité** : Les automatismes basés sur la température ne seront plus perturbés par des valeurs impossibles
- **Cohérence** : Les erreurs de lecture sont maintenant correctement signalées par NaN
- **Debugging** : Les logs indiquent clairement quand une valeur 0°C est rejetée

## Test recommandé

Après déploiement, surveiller les logs pour vérifier :
1. Qu'aucune température à 0°C n'est plus envoyée
2. Que les erreurs de lecture des capteurs sont correctement gérées avec NaN
3. Que les automatismes fonctionnent normalement avec les valeurs NaN
4. Que les logs de validation fonctionnent correctement

## Exemples de logs attendus

```
[SystemSensors] Température eau invalide finale: 0.0°C, force NaN
[WebClient] Température eau invalide avant envoi: 0.0°C, force NaN
[Automatism] Température eau invalide avant envoi: 0.0°C, force NaN
```

## Compatibilité avec le serveur

Le serveur PHP `post-ffp3-data2.php` gère correctement les valeurs NaN en les stockant comme NULL dans la base de données, ce qui est le comportement attendu pour des erreurs de lecture.
