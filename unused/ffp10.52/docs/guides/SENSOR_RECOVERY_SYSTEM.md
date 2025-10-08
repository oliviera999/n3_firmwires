# Système de Récupération Robuste des Capteurs

## Problème initial

Le système précédent se contentait de rejeter les valeurs invalides, ce qui pouvait mener à des pertes de données importantes. Nous avons maintenant implémenté un système de récupération robuste qui tente d'obtenir des lectures valides avant de rejeter.

## Améliorations apportées

### 1. Nouvelles méthodes de récupération

#### WaterTempSensor (DS18B20)
- **`robustTemperatureC()`** : Méthode principale avec récupération automatique
- **`isSensorConnected()`** : Vérifie la connectivité du capteur
- **`resetSensor()`** : Reset matériel du capteur en cas de problème

#### AirSensor (DHT22)
- **`robustTemperatureC()`** : Récupération robuste de la température
- **`robustHumidity()`** : Récupération robuste de l'humidité
- **`isSensorConnected()`** : Vérifie la connectivité du capteur
- **`resetSensor()`** : Reset matériel du capteur

### 2. Stratégie de récupération en 4 étapes

#### Étape 1 : Filtrage avancé
- Utilise le filtrage statistique existant (médiane, historique)
- Si réussi → retourne la valeur filtrée

#### Étape 2 : Diagnostic de connectivité
- Vérifie si le capteur est physiquement connecté
- Si déconnecté → reset matériel et nouvelle tentative

#### Étape 3 : Lectures répétées
- Effectue plusieurs lectures simples avec délais
- **DS18B20** : 5 tentatives avec 200ms entre chaque
- **DHT** : 3 tentatives avec 500ms entre chaque (plus lent)

#### Étape 4 : Fallback historique
- Utilise la dernière valeur valide connue
- Évite les interruptions complètes de service

### 3. Configuration adaptée par capteur

#### DS18B20 (Température eau)
```cpp
static const uint8_t MAX_RECOVERY_ATTEMPTS = 5;
static const uint16_t RECOVERY_DELAY_MS = 200;
static const uint16_t SENSOR_RESET_DELAY_MS = 1000;
```

#### DHT (Température air + Humidité)
```cpp
static const uint8_t MAX_RECOVERY_ATTEMPTS = 3;
static const uint16_t RECOVERY_DELAY_MS = 500;
static const uint16_t SENSOR_RESET_DELAY_MS = 2000;
```

### 4. Diagnostic et monitoring

#### Tests de connectivité
- **DS18B20** : Vérification CRC, type de capteur, présence sur le bus OneWire
- **DHT** : Test de lecture pour détecter les capteurs déconnectés

#### Logs détaillés
- Tentatives de récupération avec compteur
- Raisons d'échec (connectivité, valeurs invalides)
- Succès de récupération avec valeurs obtenues

## Avantages du nouveau système

### 1. Fiabilité améliorée
- **Récupération automatique** : Le système tente de se rétablir sans intervention
- **Reset matériel** : Corrige les problèmes de communication
- **Fallback intelligent** : Utilise les dernières valeurs valides

### 2. Diagnostic proactif
- **Détection précoce** : Identifie les problèmes de connectivité
- **Logs détaillés** : Facilite le debugging
- **Monitoring** : Suivi de l'état des capteurs

### 3. Robustesse opérationnelle
- **Continuity of service** : Évite les interruptions complètes
- **Adaptation automatique** : S'adapte aux caractéristiques de chaque capteur
- **Validation finale** : Garantit qu'aucune valeur invalide n'est transmise

## Impact sur les performances

### Temps de lecture
- **Cas normal** : Pas d'impact (filtrage avancé réussi)
- **Récupération** : +1-3 secondes selon le capteur
- **Reset matériel** : +1-2 secondes supplémentaires

### Fiabilité
- **Taux de succès** : Significativement amélioré
- **Interruptions** : Réduites grâce au fallback historique
- **Maintenance** : Moins d'interventions manuelles requises

## Utilisation

Le système est maintenant utilisé automatiquement dans `SystemSensors::read()` :

```cpp
// Température eau avec récupération robuste
float val = _water.robustTemperatureC();

// Température air avec récupération robuste  
float val = _air.robustTemperatureC();

// Humidité avec récupération robuste
float val = _air.robustHumidity();
```

## Monitoring recommandé

Après déploiement, surveiller :
1. **Logs de récupération** : Fréquence des tentatives de récupération
2. **Taux de succès** : Proportion de lectures réussies
3. **Problèmes récurrents** : Capteurs nécessitant une attention particulière
4. **Performance** : Impact sur les temps de réponse

## Maintenance

Le système est autonome, mais il est recommandé de :
- Surveiller les logs pour identifier les capteurs problématiques
- Vérifier les connexions physiques si les récupérations sont fréquentes
- Considérer le remplacement des capteurs si les échecs persistent 