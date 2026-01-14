# Audit stabilité code — v11.130 (2026-01-14)

## Objectif

Identifier et corriger des sources probables d’instabilité (reboots, watchdog, concurrence, valeurs invalides) sans dégrader la prod (WROOM) — en particulier avec une contrainte flash proche de la limite de partition.

## Problèmes identifiés (avant correction)

### 1) Concurrence WiFi (multi-tâches)
- `wifi.loop()` était appelé dans **deux contextes** (loop Arduino + tâche automation), ce qui peut provoquer des appels concurrents à `WiFi.*` et des comportements non déterministes (déconnexions, crashes).

### 2) Valeurs capteurs invalides (NaN) non détectées
- `SystemSensors::read()` force `NAN` pour certaines mesures invalides.
- Les validations basées uniquement sur comparaisons `<`/`>` ne détectent pas `NaN` (toutes les comparaisons retournent false) → propagation silencieuse dans la logique d’automatisme.

### 3) Logs série en production + taille flash
- Beaucoup de modules utilisent `Serial.*` directement.
- Sur WROOM prod, la taille était **très proche** de la limite de partition, ce qui réduit la marge et augmente le risque d’échec d’upload / OTA et de maintenance.

### 4) Code mort / surface de bugs
- `WatchdogManager` (implémentation custom) n’était pas utilisé par le code actif (TWDT natif déjà configuré) mais contenait un risque de deadlock interne.

## Correctifs appliqués

### A) WiFi: un seul point de pilotage
- Centralisation des opérations WiFi dans `src/app.cpp` (loop Arduino).
- Suppression des appels WiFi depuis `src/app_tasks.cpp` (tâche automation).

### B) Capteurs: détection explicite de NaN
- Ajout d’une validation `isnan()` avant fallback sur valeurs par défaut dans `src/app_tasks.cpp`.

### C) Production WROOM: désactivation sûre de Serial + réduction flash
- Ajout d’un stub `NullSerial` activé en prod (ou `ENABLE_SERIAL_MONITOR=0`) dans `include/config.h` pour éliminer à la compilation les appels `Serial.*`.
- Résultat: **réduction flash wroom-prod ~96.9% → ~94.6%** (mesuré via `pio run -e wroom-prod`).

### D) WatchdogManager: suppression du module non utilisé
- Suppression des fichiers `src/watchdog_manager.cpp` et `include/watchdog_manager.h` (réduction surface de bugs).

### E) Tests natifs (host)
- Restructuration des suites PlatformIO (`test/test_rate_limiter`, `test/test_timer_manager`) et mocks minimalistes (`test/unit/Arduino.h`, amélioration `test_mocks.h`).
- Ajout d’un helper `TimerManager::resetForTests()` pour isoler l’état statique entre tests.

## Validation effectuée

### Build
- `pio run -e wroom-test` : OK
- `pio run -e wroom-prod` : OK

### Tests unitaires natifs
- `pio test -e native` : OK (24 tests)

## Test plan recommandé sur hardware

- Flash `wroom-prod` + `uploadfs`, puis **monitoring 90s obligatoire** (voir `.cursorrules`)
- Vérifier en priorité:
  - absence de panic / WDT reset
  - stabilité WiFi (reconnect)
  - absence de NaN visibles dans les logs/états
  - marge flash/OTA OK

