---
name: ffp5cs-logging-and-diagnostics
description: Normalise les logs série et les messages de diagnostic dans le firmware ESP32 FFP5CS. À utiliser lors de modifications dans src/*.cpp ou include/*.h qui ajoutent, modifient ou suppriment des Serial.printf liés au monitoring ou au debug sur hardware réel.
---

# FFP5CS – Logging et diagnostics

## Quand utiliser cette skill

Utiliser ces règles dès que le code :
- ajoute, modifie ou supprime des appels à `Serial.printf` ou fonctions de log équivalentes,
- touche aux modules de monitoring / diagnostics (`diagnostics`, `task_monitor`, `debug_log`, `ota_manager`, `wifi_manager`, `web_client`, `web_server`, `automatism`, etc.),
- change la structure ou le contenu des messages de log (format, module, valeurs affichées).

## Objectifs

- Avoir des logs **simples, lisibles et cohérents** sur le port série, utiles en debug sur hardware réel.
- Éviter toute introduction d’un **framework de logging lourd** : pas de multi-niveaux complexes, pas de dépendances tierces inutiles.
- Mettre en avant des **logs ciblés** sur les états critiques du système :
  - alimentation / nourrissage,
  - niveaux d’eau et capteurs associés,
  - températures,
  - réseau (WiFi, serveur distant, erreurs HTTP/WS),
  - persistance (NVS), OTA, et transitions d’états de l’automatisme.

## Format standard des logs

1. **Format de base**
   - Utiliser un format simple, inspiré de :
     - `Serial.printf("[Module] Message: %d\n", value);`
   - Recommandation générale :
     - `Serial.printf("[Module] LEVEL: message (clé=%d, autre=%s)\n", ...);`

2. **Règles de format**
   - `Module` : nom court du module ou de la tâche :
     - exemples : `AUTOM`, `FEED`, `LEVEL`, `TEMP`, `WIFI`, `NET`, `NVS`, `OTA`, `WEB`, `MON`.
   - `LEVEL` (optionnel mais recommandé) :
     - `INFO` pour un événement normal,
     - `WARN` pour un comportement anormal mais continuable,
     - `ERROR` pour une erreur qui impacte la fonctionnalité.
   - `message` :
     - court, explicite, sans phrases longues,
     - inclure uniquement les valeurs utiles au diagnostic.

3. **Interdits**
   - Ne pas introduire :
     - de framework de log tiers ou générique,
     - de systèmes de configuration de niveau de log complexes,
     - de logs structurés lourds (JSON, gros blobs) dans les chemins fréquents.

## Où placer des logs utiles

Concentrer les logs sur les **points critiques** plutôt que de spammer chaque boucle :

1. **Alimentation / nourrissage**
   - Début et fin d’un cycle de nourrissage.
   - Paramètres utilisés (durée, quantité si applicable).
   - Éventuelles erreurs d’activation d’actionneurs.

2. **Niveaux d’eau**
   - Changements d’état significatifs :
     - passage en niveau bas / très bas,
     - retour à un niveau normal.
   - Anomalies capteur (valeurs hors plage, timeout, `isnan`).

3. **Températures**
   - Valeurs aberrantes ou hors plage sûre définie par le projet.
   - Erreurs de lecture capteur (timeout, CRC, etc.).

4. **Réseau (WiFi / serveur distant)**
   - Connexion / déconnexion WiFi.
   - Échecs répétés de connexion au serveur.
   - Erreurs HTTP/WS significatives (codes 4xx/5xx, timeouts).
   - Basculage explicite vers un mode offline / fallback local.

5. **NVS / persistance / OTA**
   - Erreurs de lecture/écriture NVS (clé absente, corruption, échec d’écriture).
   - Changements de configuration importants (horaires, seuils, durées).
   - Démarrage / succès / échec d’une mise à jour OTA.

6. **Automatisme / états internes**
   - Transitions d’états majeurs de l’automatisme (ex : `IDLE -> FEEDING`, `FEEDING -> REFILL`).
   - Activation / désactivation d’actionneurs critiques (pompes, relais, etc.).
   - Conditions de sécurité déclenchées (par exemple arrêt d’urgence).

## Bonnes pratiques pour éviter le spam

1. **Ne pas logger dans chaque itération de boucle**
   - Éviter les logs dans les parties exécutées à haute fréquence (`loop()`, tâches FreeRTOS rapides).
   - Préférer :
     - des logs sur les **changements d’état**,
     - des logs périodiques **à intervalle raisonnable** (ex. toutes les 10–60 s) si nécessaire.

2. **Limiter la verbosité**
   - Ne pas répéter la même information identique à chaque cycle.
   - Pour les erreurs répétées, regrouper :
     - soit en comptant le nombre d’occurrences et en loggant un résumé,
     - soit en loggant uniquement la première occurrence et un rappel périodique.

3. **Messages orientés diagnostic**
   - Chaque log doit aider à répondre à au moins une question de debug :
     - "Que fait le système ?",
     - "Quel est l’état actuel ?",
     - "Pourquoi l’automatisme a-t-il pris cette décision ?",
     - "Pourquoi la communication réseau échoue-t-elle ?".
   - Supprimer ou simplifier les logs purement décoratifs ou redondants.

## Workflow recommandé lors d’une modification

1. **Identifier le contexte**
   - Le changement touche-t-il :
     - un module de monitoring / diagnostics,
     - l’automatisme,
     - la gestion réseau,
     - les capteurs critiques (niveau, température),
     - la persistance (NVS) ou OTA ?

2. **Standardiser le format**
   - Aligner les nouveaux logs avec le format :
     - `[Module] LEVEL: message (clé=%d, ...)\n`
   - Vérifier la cohérence des `Module` et `LEVEL` utilisés dans le fichier concerné.

3. **Sélectionner les points de log**
   - Placer les logs :
     - sur les **transitions d’état** importantes,
     - à l’entrée / sortie de séquences critiques (nourrissage, OTA, reconfig réseau),
     - sur les erreurs et timeouts significatifs.
   - Éviter les logs dans les boucles rapides, sauf pour des résumés périodiques.

4. **Contrôle final**
   - Vérifier que :
     - les messages sont **clairs, courts et utiles**,
     - aucun framework de logging lourd n’a été introduit,
     - la quantité de logs reste raisonnable pour un debug sur hardware réel,
     - les informations essentielles pour comprendre les états critiques sont bien couvertes.

