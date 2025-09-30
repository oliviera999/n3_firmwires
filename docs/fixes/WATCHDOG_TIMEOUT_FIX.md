# Correction du Timeout Watchdog

## Problème
L’ESP32-S3 rencontrait des erreurs de timeout du watchdog :
```
Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)
```

Ceci arrivait car les tâches de lecture des capteurs prenaient trop de temps, dépassant le timeout watchdog par défaut de 120 secondes.

## Analyse de la cause racine
Le processus de lecture des capteurs implique plusieurs opérations longues :

1. **Water Temperature Sensor (DS18B20)**:
   - 2 stabilization readings × 75ms = 150ms
   - 5 filtered readings × 150ms = 750ms
   - Total: ~900ms per reading cycle
   - Plus recovery attempts if sensor fails

2. **Ultrasonic Sensors**:
   - 7 readings × 15ms delay = 105ms
   - Plus conversion time for each reading

3. **Air Temperature/Humidity (DHT22)**:
   - Multiple readings with delays
   - Recovery attempts if sensor fails

4. **Multiple sensors read sequentially** in the `SystemSensors::read()` method

The total time for one complete sensor reading cycle could easily exceed the 120-second watchdog timeout, especially when sensors were failing and recovery mechanisms were triggered.

## Solution mise en œuvre

### 1. Augmentation du timeout watchdog
- **Avant** : 120 secondes (2 minutes)
- **Après** : 300 secondes (5 minutes) — aligné avec `TimingConfig::WATCHDOG_TIMEOUT_MS`
- Emplacement : `src/app.cpp` dans `setup()`

### 2. Réinitialisations régulières du watchdog
Ajout d’appels `esp_task_wdt_reset()` aux points stratégiques :

#### Dans `sensorTask()` (`src/app.cpp`):
- Avant de démarrer la lecture capteurs
- Après la fin de lecture capteurs
- Avant chaque délai `vTaskDelay`

#### Dans `SystemSensors::read()` (`src/system_sensors.cpp`):
- Au début de la lecture
- Avant chaque capteur (eau, air/humidité, ultrason)
- À la fin de la lecture

#### Dans les méthodes spécifiques (`src/sensors.cpp`):
- **Ultrason** : Avant chaque mesure et avant les délais
- **Température eau** : Avant chaque lecture, avant délais, et pendant les tentatives de récupération
- **Température air** : Même principe

### 3. Inclusion requise
Ajout de `#include <esp_task_wdt.h>` dans `src/system_sensors.cpp` pour la réinitialisation watchdog.

## Bénéfices
1. **Évite les timeouts** : Timeout plus long + resets réguliers
2. **Fiabilité capteurs** : Mécanismes de récupération conservés
3. **Impact faible** : Resets très rapides
4. **Meilleure robustesse** : Moins de crashs en cas d’échecs

## Tests
Après implémentation :
- Plus d’erreurs de timeout watchdog
- Lectures capteurs fiables
- Récupérations actives en cas d’échec
- Stabilité globale améliorée

## Monitoring
Surveiller le port série :
- Plus de messages « Guru Meditation Error »
- Schémas de lecture normaux
- Récupération réussie lors d’échecs temporaires

## Pistes futures
Si des timeouts subsistent :
1. Optimiser les algorithmes de lecture (réduire les délais)
2. Lire certains capteurs dans des tâches séparées avec timeouts dédiés
3. Ajouter une gestion d’erreur plus granulaire par capteur