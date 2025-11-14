# Cartographie `automatism/automatism_network.cpp` - etat au 2025-10-29

## Vue d'ensemble

- Module dedie aux echanges avec le serveur distant: polling JSON, application
  configuration, envoi complet d'etat via file de retry (`DataQueue`), gestion des
  acquittements reset et synchronisation e-mails.
- Maintient des caches locaux (email, seuils, flags bouffe, dernier envoi, etats
  pompes) et expose `sendFullUpdate`, `pollRemoteState`, `handleResetCommand`,
  `handleRemoteOverrides`, `flushDataQueue`.
- S'interface fortement avec `Automatism` pour acceder aux capteurs, actionneurs,
  affichage, mail notifier et configuration persistante (`ConfigManager`).

## Pipeline envoi

- `sendFullUpdate()` construit un payload cle=valeur (ET, JSON) a partir des
  mesures (`SensorReadings`), etats actionneurs, flags de nourrissage, variables de
  veille. Utilise `_dataQueue` pour mise en tampon et gere un backoff exponentiel
  sur les echecs (`_currentBackoffMs`, `_consecutiveSendFailures`).
- En cas d'envoi reussi, met a jour `_serverOk`, `_sendState`, les caches
  `_prevPump*` et arme des notifications e-mail via `_mailer` (appel via `Automatism`).
- Les evenements critiques (pompe, nourrissage) reutilisent `sendChange()` et des
  helpers pour generer corps mails, consomment `SensorReadings` supplementaires et
  manipulent la DataQueue.

## Pipeline reception

- `pollRemoteState()` applique un intervalle fixe (`REMOTE_FETCH_INTERVAL_MS`),
  verifie WiFi, recupere JSON via `WebClient`, aplatit potentiels wrappers,
  journalise (`serializeJsonPretty` tronque), sauvegarde un cache NVS via
  `ConfigManager::saveRemoteVars`.
- Fallback offline: si le fetch echoue, recharge la derniere version NVS.
- `_recvState` pilote l'affichage (flèche vide/OK/erreur) depuis `Automatism::handleRemoteState`.
- `applyConfigFromJson()` met a jour email, mails, seuils, freqWake en filtrant les
  valeurs nulles/zero.

## Commandes distantes

- `handleResetCommand()` detecte `reset`/`resetMode`, applique protections anti
  boucle (timestamp NVS), envoie acquittement via `Automatism::sendFullUpdate` avec
  retry 5x et confirme via fetch supplementaire avant `ESP.restart()`.
- `handleRemoteOverrides()` traite les commandes relatives aux actionneurs
  (pompes, relais, nourrissage) en verifiant la heap libre, les verrous, les
  conditions de securite (reservoir bas, seuils) et en tenant a jour
  `_prev*`/`_emailEnabled`.
- `handleManualEmail()` et helpers generent les messages (buffers 256/1024 char).

## Dependances cles

- `WebClient`, `ConfigManager`, `Automatism`, `SystemActuators`, `SensorReadings`,
  `Mailer`, `AutomatismPersistence`, `Preferences` (anti-reset loops), `GPIOParser`,
  `DataQueue`, `AutomatismFeeding` (pour ack nourrissage), `ArduinoJson`, `WiFi`.
- Utilitaires: `esp_task_wdt`, `esp_heap_caps`, `vTaskDelay`, `millis()`.

## Points de fragilite

- Parametrisation lourde de `sendFullUpdate`: plus de 12 arguments en entree,
  dependances fortes a l'etat interne `Automatism`, duplication entre caches
  `_prev*` et `Automatism`.
- Code volumineux (~900 lignes) contenant logique metier (alertes e-mail,
  diagnostics) melangee avec la communication reseau.
- `String` et buffers temporaires multiples pour generer les payloads/JSON;
  `serializeJsonPretty` sur la console peut etre couteux (mémoire + temps).
- `DataQueue` pilotee ici mais consommee ailleurs (envoi via `Automatism`), ce qui
  rend difficile le suivi des retries et de la memoire tampon.
- Gestion du backoff repartie entre plusieurs variables globales internes; pas de
  config centralisee ni de stats exposees.
- Couplage a `ESP.restart()` dans `handleResetCommand()` rendant le test unitaire
  difficile et melangeant la responsabilite reseau/systeme.

## Pistes de refactorisation

- Introduire un objet `RemoteUpdate` (struct) contenant toutes les variables a
  envoyer; `Automatism` peuplerait la struct puis `AutomatismNetwork` la serialiserait.
- Isoler un `RemoteCommandProcessor` qui traite reset/overrides, accessible via une
  interface et testable hors hardware; cela permettrait aussi de deleguer la
  generation d'e-mails.
- Centraliser la gestion `DataQueue` (push, flush, stats) dans une classe dediee
  partagee par WebServer et Automatism, avec API de monitoring (taille, failures).
- Remplacer l'utilisation directe de `String`/`serializeJsonPretty` par des buffers
  char multi-usage et un logger structure (niveaux, categories) afin de reduire la
  charge memoire.
- Extraire la logique de confirmation reset + restart dans un service systeme
  mutualise capable de valider la stabilite avant `ESP.restart()`.











