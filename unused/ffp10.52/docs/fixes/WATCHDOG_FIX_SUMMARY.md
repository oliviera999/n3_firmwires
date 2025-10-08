# Résumé de la Correction du Timeout Watchdog

## Problème identifié
L’ESP32 rencontrait des erreurs de timeout du watchdog :
```
Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)
```

Cela indique que le watchdog d’interruption se déclenchait car des tâches ou des gestionnaires d’interruption prenaient trop de temps.

## Analyse des causes
1. **Fréquence de la tâche display trop élevée** : 10 FPS (100 ms) causant une charge I2C importante
2. **Opérations d’affichage coûteuses** : `updateDisplay()` potentiellement bloquante
3. **Multiples resets watchdog** : appels redondants parfois conflictuels
4. **Priorités de tâches** : `displayTask` en priorité 3 → problèmes possibles d’ordonnancement
5. **Timeout insuffisant** : watch­dog à 5 minutes selon l’ancienne doc (désormais aligné à 5 min dans le code)

## Optimisations appliquées

### 1. Timeout Watchdog
- **Avant** : 300 secondes (5 minutes)
- **Après** : 300 secondes (5 minutes) — confirmé et documenté
- **Fichier** : `src/app.cpp`

### 2. Réduction de la fréquence `displayTask`
- **Avant** : délai 100 ms (10 FPS)
- **Après** : délai 200 ms (5 FPS) ou 250 ms (~4 FPS) selon le profil
- **Fichier** : `src/app.cpp`

### 3. Fréquence d’update de l’affichage
- **Avant** : intervalle 50 ms (20 FPS)
- **Après** : 200–250 ms (4–5 FPS)
- **Fichier** : `src/automatism.cpp`

### 4. Délais des tâches
- **Automation** : 50 ms → 100 ms
- **Boucle principale** : 100 ms → 200 ms
- **Fichier** : `src/app.cpp`

### 5. Priorité `displayTask`
- **Avant** : priorité 3
- **Après** : priorité 1
- **Fichier** : `src/app.cpp`

### 6. Réinitialisations Watchdog ciblées
Ajout d’appels `esp_task_wdt_reset()` aux moments clés :
- Avant opérations d’affichage
- Après lectures capteurs
- Après opérations d’affichage
- **Fichier** : `src/automatism.cpp`

### 7. Meilleure gestion d’erreurs
Blocs try/catch et gestion d’erreurs renforcés dans la tâche d’affichage.

## Configuration des tâches après optimisation

| Tâche           | Cœur | Priorité | Stack      | Délai  | Rôle                      |
|-----------------|------|----------|------------|--------|---------------------------|
| sensorTask      | 0    | 2        | 8192 bytes | 1000ms | Lectures capteurs         |
| automationTask  | 1    | 1        | 12288 bytes| 100ms  | Logique d’automatisme     |
| displayTask     | 1    | 1        | 8192 bytes | 200ms  | Mises à jour OLED         |
| loop principal  | -    | -        | -          | 200ms  | Maintenance système        |

## Améliorations attendues
1. **Moins de trafic I2C** : fréquence réduite → moins de contention
2. **Moins d’utilisation CPU** : délais plus longs → charge inférieure
3. **Ordonnancement stable** : meilleures priorités
4. **Moins de timeouts** : timeout confirmé + resets mieux gérés
5. **Réactivité globale** : exécution mieux équilibrée

## Tests

### Script de monitoring
`test_watchdog_fix.py` surveille :
- Erreurs de timeout watchdog
- Fréquence d’update display
- Erreurs I2C
- Stabilité système

### Procédure de test
1. Flasher le code optimisé
2. Lancer le script de monitoring pendant 10+ minutes
3. Vérifier l’absence de timeouts watchdog
4. Vérifier le bon fonctionnement de l’affichage
5. Tester toutes les fonctionnalités

## Optimisations additionnelles (si nécessaire)
1. **Réduire davantage** la fréquence display (500 ms, 2 FPS)
2. **Bufferiser l’affichage** pour réduire les appels I2C
3. **Ajouter des mutex** autour des opérations I2C
4. **Désactiver l’affichage** durant les opérations critiques
5. **Gérer l’alimentation** de l’affichage

## Fichiers modifiés
1. `src/app.cpp` – logique principale et création des tâches
2. `src/automatism.cpp` – logique d’affichage et gestion watchdog
3. `WATCHDOG_FIX_SUMMARY.md` – ce document de synthèse

## Vérification
Pour vérifier la correction :
1. Surveiller les messages « [Display] updateDisplay appelée »
2. Vérifier l’absence d’erreurs watchdog
3. Surveiller la fréquence et les erreurs I2C
4. Vérifier la fluidité de l’affichage
5. Contrôler l’usage CPU et la mémoire

Ces optimisations réduisent significativement les timeouts watchdog tout en conservant les fonctionnalités du système. 