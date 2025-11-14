# Cartographie `automatism.cpp` - etat au 2025-10-29

## Vue d'ensemble

- Classe pivot qui orchestre tous les automatismes: initialisation reseau, lecture
  capteurs, calculs marree, nourrissage, remplissage, alertes, veille et synchronisation
  serveur.
- Les modules specialises (`AutomatismFeeding`, `AutomatismNetwork`, `AutomatismSleep`,
  `AutomatismActuators`, `AutomatismAlertController`, `AutomatismRefillController`,
  `AutomatismDisplayController`) sont instancies ici et relies a l'etat global
  `_acts`, `_sensors`, `_config`, `_power`, `_mailer`, `_disp`, `_web`.
- De nombreux champs d'etat internes (_lastReadings, compteurs d'activite, flags
  manuels, caches pour affichage) restent locaux a `Automatism`, ce qui limite la
  separation en sous-modules.
- La methode `begin()` (>300 lignes) restaure l'etat persistant (NVS, snapshots,
  variables distantes), arme les callbacks (nourrissage, mailer) et lance des taches
  FreeRTOS (`syncResetModeTask`).

## Boucle principale `update()`

- Sequence stricte: `checkNewDay()` -> `handleFeeding()` -> `handleRefill()` ->
  `handleMaree()` -> `handleAlerts()` -> sync distante (`handleRemoteState()`)
  -> retry d'envoi (`sendFullUpdate`) -> logique de sleep (`shouldEnterSleepEarly`,
  `handleAutoSleep`) -> `checkCriticalChanges()` -> sauvegarde periodique.
- `handleRemoteState()` pilote l'affichage et delegue a `AutomatismNetwork` pour
  le polling JSON, l'analyse GPIO et la persistence des variables distantes.
- `sendFullUpdate()` passe toujours par `AutomatismNetwork::sendFullUpdate()` mais
  doit encore preparer toutes les donnees (capteurs, timers, flags) et gere
  l'affichage lors des envois.
- `handleAutoSleep()` et `shouldEnterSleepEarly()` croisent etats capteurs,
  activites recentes, alimentation et task monitor pour declencher le light sleep
  en respectant les protections de nourrissage/remplissage.

## Interfaces exposees

- Commandes manuelles (`manualFeedSmall/Big`, `start/stopTankPumpManual`, toggles
  chauffage/lumiere/pompe) servent le serveur HTTP local et remettent en forme les
  notifications (affichage, e-mails, sync immediate si WiFi OK).
- Lecture/trace (`readSensors`, `traceFeedingEvent`, `traceFeedingEventSelective`)
  fournissent des callbacks aux modules et au serveur distant.
- Routines de veille: `logSleepTransitionStart/End`, `detectSleepAnomalies`,
  `validateSystemStateBeforeSleep`, `verifySystemStateAfterWakeup`.
- Gestion de configuration distante: `applyConfigFromJson`, `fetchRemoteState`,
  `storeEmailAddress`, `toggleForceWakeup`.

## Dependances cles

- `SystemSensors`, `SystemActuators`, `ConfigManager`, `PowerManager`, `Mailer`,
  `DisplayView`, `WebClient`, `TaskMonitor`, `EventLog`, `GPIOParser`, `Preferences`
  (NVS legacy), `WiFi`, `esp_task_wdt`, `ArduinoJson`.
- Modules internes: `AutomatismFeeding`, `AutomatismNetwork`, `AutomatismSleep`,
  `AutomatismPersistence`, `AutomatismActuators`, `AutomatismRefillController`,
  `AutomatismAlertController`, `AutomatismDisplayController`.
- Ressources partagees: `DataQueue` (via module Network), `LittleFS`, `jsonPool`.

## Points de fragilite

- Taille monolithique (>2 000 lignes) avec logique conditionnelle dense dans
  `begin()`, `update()`, `handleAutoSleep()` et les methodes manuelles.
- Couplage fort aux singletons globaux et a l'affichage: chaque envoi ou polling
  manipule directement `_disp`, `_sensors`, `_mailer`, `_acts`, `_config`.
- Utilisation recurrente de `String`, `serializeJson` et buffers locaux dans les
  notifications (emails, web) qui augmente la fragmentation heap.
- Multiples sections critiques time-sensitives (servo, pompes) melangees avec
  appels blocants (`vTaskDelay`, `Preferences`, HTTP via `sendFullUpdate`).
- Gestion watchdog via resets ponctuels (`_power.resetWatchdog`, `esp_task_wdt_reset`)
  dispersee; risque de watchdog si un blocage survient lors des loops d'envoi.
- Complexite autour des snapshots/actionneurs: `saveActuatorSnapshotToNVS`,
  `clearActuatorSnapshotInNVS`, gestion anti-boucle reset, multiples flags internes
  (`_manualFeedingActive`, `_countdownEnd`, `_wakeupProtectionEnd`).

## Pistes de refactorisation

- Transformer `Automatism` en orchestrateur mince qui delegue les actions a des
  services specialises (ex: `FeedingService`, `SleepService`, `RemoteSyncService`)
  en leur passant un contexte immutable.
- Extraire `begin()` en sous-etapes: restauration NVS, initialisation callbacks,
  demarrage taches, diagnostics. Chaque etape pourrait vivre dans un helper
  documentaire/testable.
- Encapsuler les commandes manuelles et leurs side-effects (logs, e-mails,
  synchronisation) dans un module `ManualActions` partage entre HTTP et automatisme.
- Centraliser la gestion des envois distants (backoff, affichage, DataQueue) dans
  `AutomatismNetwork` ou un `RemoteDispatcher` afin de supprimer les doublons
  `sendFullUpdate` / `handleRemoteState` / `checkCriticalChanges`.
- Remplacer l'usage massif de `String` par des buffers `char[]` partages (mails,
  payloads JSON) ou des helpers RAII pour limiter la fragmentation.
- Formaliser un objet `AutomatismContext` expose en lecture seule aux sous-modules
  pour retirer les dependances directes a `_config`, `_power`, `_mailer`.











