---
name: ffp5cs-esp32-offline-first
description: Enforces offline-first behaviour and local fallbacks when editing ESP32 firmware networking code in the FFP5CS project. Use when modifying wifi_manager, web_client, web_server, web_routes_*, bootstrap_network, ota_manager, or any WiFi/HTTP/WebSocket/OTA-related logic.
---

# FFP5CS ESP32 Offline-first

## When to use this skill

Use this skill whenever you:

- Edit or create code in `wifi_manager`, `web_client`, `web_server`, any `web_routes_*` file, `bootstrap_network`, `ota_manager`, or related networking modules.
- Touch code that depends on WiFi, HTTP, WebSocket, TLS, or the distant server.
- Add or modify logic that could block while waiting on the network or server.

Always combine this skill with the core project rules from `FFP5CS-R-gles-c-ur-de-projet.mdc`.

## Core offline-first principles

When working on the files mentioned above, **always remind yourself and apply** these rules:

1. **Aucune fonction essentielle ne dépend du réseau**
   - Une fonction essentielle (boucles principales, automatisme, sécurité des poissons/plantes) doit continuer à fonctionner **sans WiFi ni serveur**.
   - Toute dépendance réseau doit être optionnelle, avec un chemin d'exécution sûr en cas d'absence de réseau.

2. **Toujours prévoir un fallback local**
   - Pour **chaque accès serveur** (lecture config, envoi de données, OTA, etc.), définir un comportement local en cas d'échec :
     - Lire la config en NVS.
     - Utiliser des **valeurs par défaut sûres** si la NVS est absente ou corrompue.
   - Ne jamais laisser un appel réseau silencieusement échouer sans décider explicitement quoi faire côté firmware.

3. **Timeouts stricts, jamais de blocage infini**
   - **Timeout réseau maximum : 5 secondes** pour toute opération HTTP, WebSocket, DNS, etc.
   - **Interdit** :
     - Les boucles `while` qui attendent indéfiniment une connexion, une réponse serveur ou un flag réseau.
     - Les mécanismes de retry infini sans backoff ni limite.
   - Après un timeout ou plusieurs échecs :
     - Logguer l'erreur de manière simple.
     - Revenir immédiatement à un fonctionnement local (NVS / valeurs par défaut).

4. **Configuration locale comme source de vérité**
   - Considérer la NVS comme **source de vérité** pour les horaires, seuils, durées et états.
   - Le serveur distant sert uniquement à **mettre à jour** cette configuration, jamais à piloter l'automatisme en direct.

## Workflow recommandé lors d'une modification

Lorsque vous modifiez un code réseau (WiFi, HTTP, WebSocket, OTA) :

1. **Identifier le rôle de la fonction**
   - Vérifier si la fonction impacte directement la sécurité ou le comportement de base du système.
   - Si oui, confirmer qu'elle **ne bloque pas** et ne dépend pas d'une réponse réseau pour fonctionner.

2. **Vérifier les dépendances réseau**
   - Rechercher les appels à `WiFi.*`, client HTTP, WebSocket, ou APIs serveur.
   - S'assurer que chaque appel est entouré de :
     - Un **timeout ≤ 5s**.
     - Une gestion d'erreur explicite.
     - Un chemin de fallback local bien défini.

3. **Mettre en place ou renforcer le fallback local**
   - Lire la configuration depuis la NVS quand c'est possible.
   - Si la NVS est indisponible, appliquer des valeurs par défaut sûres pour ne jamais mettre le système dans un état dangereux.

4. **Éviter tout blocage**
   - Remplacer les `while` bloquants par :
     - Des essais bornés dans le temps ou en nombre de tentatives.
     - Des tâches FreeRTOS ou callbacks non bloquants si nécessaire.
   - S'assurer que le watchdog ne peut pas être déclenché par du code réseau.

5. **Résumer mentalement (ou dans un commentaire) ce check offline-first**
   - Avant de finaliser les changements, vérifier rapidement :
     - Le firmware continue-t-il à fonctionner **sans serveur ni WiFi** ?
     - Chaque appel réseau a-t-il un **timeout ≤ 5s** ?
     - Y a-t-il un **fallback local clair** (NVS / valeurs par défaut) pour chaque dépendance serveur ?

## Exemple de rappel à appliquer en début de tâche

Quand vous commencez à modifier un de ces fichiers (wifi/web/OTA), gardez en tête ce mini-résumé :

- **OFFLINE-FIRST** : le firmware doit rester pleinement fonctionnel sans réseau.
- **Fallback local systématique** : NVS ou valeurs par défaut pour chaque interaction serveur.
- **Pas de blocage** : timeout max 5s, pas de retry infini, pas de `while` bloquant en attente du réseau.

