---
name: ffp5cs-offline-network-agent
description: Dedicated subagent for reviewing and patching ESP32 networking code in FFP5CS to keep it offline-first, with NVS fallbacks and short timeouts. Use when modifying wifi_manager, web_client, web_server, any web_routes_*, bootstrap_network or ota_manager, especially when adding or changing HTTP/WebSocket flows or routes.
---

# FFP5CS – Sous-agent "Offline‑First / Réseau"

## Quand appliquer cette skill

Utiliser ce sous-agent dès que tu :

- modifies `wifi_manager`, `web_client`, `web_server`, un fichier `web_routes_*`, `bootstrap_network` ou `ota_manager` ;
- ajoutes/modifies des appels HTTP, WebSocket, TLS ou une nouvelle route côté firmware ;
- touches à la logique de connexion WiFi, de retry réseau, ou à l’initialisation réseau au boot.

Toujours combiner cette skill avec :

- `ffp5cs-esp32-offline-first` (règles offline-first réseau) ;
- `ffp5cs-web-api-local` pour les endpoints HTTP/WebSocket locaux ;
- les règles cœur de projet (`FFP5CS-R-gles-c-ur-de-projet.mdc`).

## Rôle du sous-agent

Ce sous-agent a deux modes d’utilisation :

- **Mode revue (explore)** : analyser le code réseau existant ou modifié pour vérifier qu’il reste bien offline-first.
- **Mode patch (generalPurpose)** : proposer et appliquer des corrections concrètes quand une évolution réseau casse les règles.

Quand tu en as la possibilité via l’outil `Task` :

- utilise `subagent_type: "explore"` pour une revue de code (lecture/analyse uniquement) ;
- utilise `subagent_type: "generalPurpose"` pour proposer/implémenter des patchs réseau conformes aux règles offline-first.

## Checklist de revue réseau (mode explore)

Quand tu lances ce sous-agent en mode **explore**, suis systématiquement cette checklist :

1. **Identifier le contexte**
   - Localiser toutes les fonctions réseau impactées (WiFi, HTTP, WebSocket, OTA) dans les fichiers ciblés.
   - Noter lesquelles sont appelées depuis des boucles principales, tâches critiques ou callbacks fréquents.

2. **Contrôler les blocages**
   - Rechercher les `while`, `for` ou waits qui dépendent d’une condition réseau (connexion WiFi, réponse HTTP/WS, ping serveur…).
   - Vérifier qu’aucune de ces boucles ne peut bloquer l’exécution principale ou déclencher le watchdog.
   - Signaler toute boucle bloquante sans borne claire (ni timeout ni nombre max de tentatives).

3. **Vérifier les timeouts**
   - Pour chaque appel HTTP/WebSocket/OTA/DNS, confirmer la présence d’un **timeout explicite ≤ 5s**.
   - Si un appel repose sur un timeout implicite de la lib, mentionner clairement la valeur supposée et proposer d’ajouter un timeout explicite si nécessaire.

4. **Fallback local (NVS / valeurs par défaut)**
   - Pour chaque accès au serveur distant (lecture de config, envoi de données, OTA, sync…), vérifier :
     - qu’un **fallback NVS** ou des **valeurs par défaut sûres** sont définis en cas d’échec ;
     - que le firmware peut continuer à fonctionner sans serveur (mode offline).
   - Signaler tout code qui :
     - abandonne silencieusement sans définir d’état local clair ;
     - ou qui suppose que la réponse serveur est toujours disponible.

5. **Surveillance des nouvelles routes et flux**
   - Pour toute nouvelle route HTTP/WebSocket ou nouveau flux réseau, vérifier :
     - qu’elle ne rend pas le fonctionnement local dépendant du serveur distant ;
     - qu’elle respecte les contrats de l’API locale (`ffp5cs-web-api-local`) ;
     - qu’elle ne spamme pas le réseau ou l’UI (fréquence raisonnable, payloads compacts).
   - Si une route rompt les règles (blocage, dépendance forte au serveur, pas de fallback), proposer un plan de correction.

6. **Synthèse de la revue**
   - Résumer clairement :
     - les points conformes aux règles offline-first ;
     - les risques identifiés (blocages, absence de timeout, manque de fallback) ;
     - les recommandations concrètes de corrections.

## Checklist de patch (mode generalPurpose)

Quand tu utilises ce sous-agent en **generalPurpose** pour proposer des patchs :

1. **Supprimer/réduire les blocages**
   - Remplacer les boucles bloquantes réseau par :
     - des essais bornés dans le temps/nombre de tentatives ;
     - ou des tâches FreeRTOS/callbacks non bloquants quand approprié.
   - S’assurer que le thread principal et les tâches critiques ne dépendent jamais d’une attente infinie réseau.

2. **Ajouter/renforcer les timeouts**
   - Imposer un timeout explicite (≤ 5s) pour chaque appel réseau.
   - Après timeout/échec :
     - logguer l’erreur de manière simple ;
     - revenir immédiatement à un comportement local sûr.

3. **Mettre en place les fallbacks NVS**
   - Pour les configurations :
     - lire en priorité la NVS ;
     - n’utiliser le serveur distant que pour mettre à jour la NVS, jamais comme unique source.
   - Pour les valeurs opérationnelles :
     - définir des valeurs par défaut sûres en dernier recours (en respectant les règles du projet).

4. **Respecter l’API locale**
   - Pour les modifications d’API locale, vérifier avec `ffp5cs-web-api-local` que :
     - les endpoints canoniques (`/`, `/api/status`, `/api/config`, `/api/feed`, `/ws`) ne sont pas cassés ;
     - le JSON reste simple, stable et compatible autant que possible.

5. **Valider le mode offline**
   - Vérifier conceptuellement (et si possible par tests) que :
     - le firmware reste pleinement fonctionnel sans WiFi ni serveur distant ;
     - les actions de base et l’automatisme ne dépendent pas de l’état réseau.

6. **Documenter rapidement les changements**
   - Laisser des commentaires courts lorsque tu introduis un timeout ou un fallback important, pour expliquer la logique offline-first.

## Résumé mental rapide

Avant de conclure, ce sous-agent doit toujours vérifier mentalement :

- **Offline-first** : rien d’essentiel ne dépend du réseau.
- **Fallback local** : chaque interaction serveur a une alternative NVS / valeurs par défaut.
- **Timeouts courts** : aucune attente réseau sans borne claire, timeout ≤ 5s partout.

