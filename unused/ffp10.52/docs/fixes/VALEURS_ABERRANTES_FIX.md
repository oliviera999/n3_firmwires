# Correction de la Validation des Valeurs Aberrantes

## Problème identifié

Des valeurs aberrantes étaient parfois envoyées au serveur, ce qui pouvait causer des problèmes dans les automatismes et l'affichage. Par exemple :
- Températures négatives ou hors limites
- Humidité invalide (0% ou >100%)
- Niveaux d'eau aberrants (0cm ou >500cm)
- Luminosité hors limites
- Tension invalide

## Solution implémentée

### 1. Validation complète dans `system_sensors.cpp`

**Fichier : `src/system_sensors.cpp`**

- **Validation des niveaux d'eau** :
  - Niveaux ultrasoniques : rejet si 0cm (invalide) ou >500cm (aberrant)
  - Force la valeur à 0 si invalide

- **Validation de la luminosité** :
  - Rejet si >4095 (limite ADC 12-bit ESP32)
  - Force la valeur à 0 si invalide

- **Validation de la tension** :
  - Rejet si raw < 0, raw > 4095, ou val > 3300mV
  - Force la valeur à 0 si invalide

### 2. Validation renforcée dans `web_client.cpp`

**Fichier : `src/web_client.cpp`**

- **WebClient::sendMeasurements()** :
  - Validation de l'humidité : rejet si <5% ou >100%
  - Force NaN si la valeur est invalide
  - Logs détaillés pour le debugging

### 3. Validation complète dans `automatism.cpp`

**Fichier : `src/automatism.cpp`**

- **Automatism::sendFullUpdate()** :
  - Validation de l'humidité avant l'envoi complet
  - Force NaN si la valeur est invalide
  - Logs détaillés pour le debugging

## Critères de validation

### Température de l'eau
- **Valeurs acceptées** : ]0.0°C ; 60.0°C[
- **Valeurs rejetées** : ≤ 0.0°C, ≥ 60.0°C, NaN
- **Action** : Remplacement par NaN

### Température de l'air
- **Valeurs acceptées** : ]5.0°C ; 60.0°C[
- **Valeurs rejetées** : ≤ 5.0°C, ≥ 60.0°C, NaN
- **Action** : Remplacement par NaN

### Humidité
- **Valeurs acceptées** : [5.0% ; 100.0%]
- **Valeurs rejetées** : < 5.0%, > 100.0%, NaN
- **Action** : Remplacement par NaN

### Niveaux d'eau (ultrasoniques)
- **Valeurs acceptées** : ]0cm ; 500cm[
- **Valeurs rejetées** : 0cm (invalide), >500cm (aberrant)
- **Action** : Remplacement par 0

### Luminosité
- **Valeurs acceptées** : [0 ; 4095] (ADC 12-bit)
- **Valeurs rejetées** : >4095
- **Action** : Remplacement par 0

### Tension
- **Valeurs acceptées** : [0mV ; 3300mV]
- **Valeurs rejetées** : <0mV, >3300mV, raw hors limites
- **Action** : Remplacement par 0

## Logs ajoutés

Des logs détaillés ont été ajoutés pour tracer :
- Les valeurs invalides détectées
- Les raisons du rejet (hors limites, aberrant, etc.)
- Les actions prises (remplacement par NaN ou 0)
- Les validations finales avant envoi

## Impact

- **Sécurité** : Aucune valeur aberrante ne sera plus envoyée au serveur
- **Fiabilité** : Les automatismes basés sur les capteurs ne seront plus perturbés
- **Debugging** : Logs détaillés pour identifier les problèmes de capteur
- **Compatibilité** : Les valeurs NaN et 0 sont gérées correctement par le serveur
- **Robustesse** : Validation à plusieurs niveaux (capteur, système, envoi)

## Test recommandé

Après déploiement, surveiller les logs pour vérifier :
1. Qu'aucune valeur aberrante n'est plus envoyée
2. Que les logs de validation fonctionnent correctement
3. Que les automatismes fonctionnent normalement avec les valeurs NaN/0
4. Que les capteurs défaillants sont correctement détectés

## Exemples de logs attendus

```
[SystemSensors] Niveau aquarium invalide: 0 cm, force 0
[SystemSensors] Humidité invalide finale: 0.0%, force NaN
[WebClient] Température eau invalide avant envoi: -2.5°C, force NaN
[Automatism] Humidité invalide avant envoi: 150.0%, force NaN
``` 