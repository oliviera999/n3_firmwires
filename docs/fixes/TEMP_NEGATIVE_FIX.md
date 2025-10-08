# Correction des Températures Négatives

## Problème identifié

Des valeurs négatives pour la température de l'eau étaient parfois prises en compte et envoyées au serveur, ce qui pouvait causer des problèmes dans les automatismes et l'affichage.

## Causes possibles

1. **Lectures invalides du capteur DS18B20** : Le capteur peut retourner des valeurs aberrantes
2. **Problèmes de conversion** : Les valeurs NaN peuvent être converties en valeurs négatives
3. **Fallback sans validation** : Quand le filtrage avancé échoue, l'ancienne méthode n'avait pas de validation suffisante
4. **Manque de validation finale** : Aucune validation n'était faite juste avant l'envoi des données

## Corrections apportées

### 1. Renforcement de la validation dans `sensors.cpp`

**Fichier : `src/sensors.cpp`**

- **WaterTempSensor::filteredTemperatureC()** :
  - Ajout de logs détaillés pour les lectures invalides
  - Validation finale renforcée après calcul de la médiane
  - Vérification que l'historique ne contient que des valeurs positives

- **WaterTempSensor::temperatureC()** :
  - Ajout d'une validation pour éviter les valeurs négatives
  - Retour de NaN si la valeur est invalide

### 2. Validation dans `system_sensors.cpp`

**Fichier : `src/system_sensors.cpp`**

- **SystemSensors::read()** :
  - Validation finale renforcée avant d'assigner la température à la structure
  - Force NaN si la valeur est invalide

### 3. Validation avant envoi dans `web_client.cpp`

**Fichier : `src/web_client.cpp`**

- **WebClient::sendMeasurements()** :
  - Validation des températures juste avant l'envoi
  - Force NaN si les valeurs sont invalides

### 4. Validation dans `automatism.cpp`

**Fichier : `src/automatism.cpp`**

- **Automatism::sendFullUpdate()** :
  - Validation des températures avant l'envoi complet
  - Force NaN si les valeurs sont invalides

## Critères de validation

### Température de l'eau
- **Valeurs acceptées** : ]0.0°C ; 60.0°C[
- **Valeurs rejetées** : ≤ 0.0°C, ≥ 60.0°C, NaN
- **Action** : Remplacement par NaN

### Température de l'air
- **Valeurs acceptées** : ]5.0°C ; 60.0°C[
- **Valeurs rejetées** : ≤ 5.0°C, ≥ 60.0°C, NaN
- **Action** : Remplacement par NaN

## Logs ajoutés

Des logs détaillés ont été ajoutés pour tracer :
- Les lectures invalides rejetées
- Les raisons du rejet (NaN, négatif, hors limites)
- Les validations finales avant envoi

## Impact

- **Sécurité** : Aucune température négative ne sera plus envoyée au serveur
- **Fiabilité** : Les automatismes basés sur la température ne seront plus perturbés
- **Debugging** : Logs détaillés pour identifier les problèmes de capteur
- **Compatibilité** : Les valeurs NaN sont gérées correctement par le serveur

## Test recommandé

Après déploiement, surveiller les logs pour vérifier :
1. Qu'aucune température négative n'est plus envoyée
2. Que les logs de validation fonctionnent correctement
3. Que les automatismes fonctionnent normalement avec les valeurs NaN 