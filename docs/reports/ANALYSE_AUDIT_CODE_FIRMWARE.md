# Audit firmware - bugs/risques de régression

## Contexte
- Périmètre: firmware uniquement (`src/`, `include/`).
- Sources: code only (pas de logs/rapports).
- Objectif: identifier bugs/risques de régression côté stabilité/fiabilité.

## Synthèse
- Constats majeurs: 3
- Constats mineurs: 2
- Zones sensibles: endpoints web async, WiFi connect, sérialisation JSON/HTTP.

## Constatations (par sévérité)

### Majeur
**A1 - Création de tâches async sans gestion d’échec**
- Où: `src/web_server.cpp` (routes `/action` et `handleRelayAction()`).
- Symptôme: `xTaskCreate(...)` est appelée sans vérifier le retour. Si la création échoue, les paramètres `new` ne sont jamais libérés et certaines actions restent en attente (sync, email).
- Impact: fuite mémoire + désynchronisation d’état possible; en charge mémoire faible, les actions peuvent sembler “validées” côté HTTP mais ne pas être traitées.
- Recommandation: vérifier la valeur de retour, libérer `params` si échec, et remettre le compteur d’async à l’état cohérent.

**A2 - Compteur `asyncTaskCount` non robuste**
- Où: `src/web_server.cpp` (route `/action`, tâches email/sync).
- Symptôme: `asyncTaskCount` est incrémenté avant `xTaskCreate()` et décrémenté dans la tâche. Si la création échoue, le compteur ne revient jamais à la normale. De plus, le compteur est modifié depuis plusieurs tâches sans protection (dual-core).
- Impact: blocage définitif des tâches async (mail/sync) et comportement non déterministe sous charge.
- Recommandation: incrémenter uniquement après création réussie, protéger le compteur (section critique) ou remplacer par un sémaphore/queue.

**A3 - Connexion WiFi asynchrone avec SSID mutable**
- Où: `src/web_server.cpp` (route `/wifi/connect`, `static String targetSSID`).
- Symptôme: le SSID cible est stocké dans une variable `static` partagée. Si l’utilisateur envoie deux requêtes de connexion successives, la tâche en arrière-plan peut utiliser un SSID différent de celui de sa requête initiale.
- Impact: connexion au mauvais réseau ou échec aléatoire, surtout si le front déclenche plusieurs essais.
- Recommandation: passer une copie dédiée du SSID à la tâche (allocation ou struct), ne pas utiliser de `static` partagé.

### Mineur
**B1 - JSON `/json` potentiellement tronqué**
- Où: `src/web_server.cpp` (route `/json`).
- Symptôme: sérialisation dans un buffer fixe de 512 bytes, sans vérifier `serializeJson()` (taille de sortie). Si le JSON dépasse 512 bytes, la réponse est tronquée et peut devenir invalide.
- Impact: erreurs de parsing côté UI, affichage incohérent, régressions silencieuses selon l’ajout de champs.
- Recommandation: augmenter le buffer, ou streamer directement via `AsyncResponseStream`/`serializeJson()` sans buffer fixe, et vérifier la taille.

**B2 - Payload HTTP potentiellement tronqué sans détection**
- Où: `src/web_client.cpp` (`sendMeasurements()`).
- Symptôme: `payload[512]` + `appendKV()` utilise `snprintf` et augmente `offset` même si la chaîne est tronquée. Il n’y a pas de flag d’erreur ni de validation finale.
- Impact: payload partiel/malformé envoyé au serveur, entraînant des valeurs manquantes ou des erreurs côté backend sans alerte locale.
- Recommandation: détecter la troncature (retour de `snprintf`) et refuser l’envoi, ou augmenter le buffer et loguer un avertissement.

## Points positifs notables
- WDT configuré, et reset explicite dans les tâches principales.
- Gestion capteurs avec validations et fallbacks (NaN, valeurs aberrantes).
- NVS centralisé avec cache et nettoyage périodique.

## Recommandations globales
- Prioriser la robustesse des tâches asynchrones (création, compteur, nettoyage).
- Normaliser les chemins d’envoi HTTP/JSON pour éviter les réponses tronquées.
- Ajouter un “mode erreur” clair lorsque les ressources (heap, tâches) sont insuffisantes.
