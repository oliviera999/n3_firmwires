---
name: ffp5cs-memory-watchdog
description: Encadre l’utilisation mémoire et le watchdog dans le firmware ESP32 FFP5CS. À utiliser lors de modifications dans src/*.cpp ou include/*.h qui touchent aux tâches FreeRTOS, au réseau ou aux buffers.
---

# FFP5CS – Mémoire et Watchdog

## Quand appliquer cette skill

Utiliser ces règles dès que le code modifie :
- des fichiers `src/*.cpp` ou `include/*.h`, **en particulier** :
  - tâches FreeRTOS (`xTaskCreate`, `vTaskDelay`, boucles de tâches),
  - logique réseau (WiFi, HTTP, WebSocket, OTA),
  - buffers (char[], tableaux, structures utilisées dans des boucles ou callbacks).

Si un fichier combine logique métier et réseau/tâches/buffers, appliquer les règles à l’ensemble de la fonction ou tâche concernée.

## Objectifs

- Préserver une **marge de mémoire suffisante** pour le fonctionnement stable du firmware.
- Éviter les **allocations dynamiques répétées** qui fragmentent le heap.
- S’assurer que le **watchdog ne déclenche pas** à cause de boucles longues ou bloquantes.

## Règles mémoire

1. **Surveiller le heap libre**
   - Viser **heap libre > 50 KB** en fonctionnement normal.
   - Considérer qu’il y a **alerte** si le heap libre descend **< 30 KB** (risque de fragmentation / instabilité).
   - Quand c’est pertinent (nouvelle grosse allocation, nouveaux buffers), ajouter ou utiliser des points de log existants pour afficher le heap libre après l’initialisation de la fonctionnalité.

2. **Éviter les allocations dynamiques dans les boucles**
   - **Interdit** dans ce projet : nouvelles utilisations de `new`, `delete`, `malloc`, `free`, `std::string`, `std::vector` ou objets alloués dynamiquement **à l’intérieur de boucles fréquentes** :
     - `loop()`,
     - boucles de tâches FreeRTOS,
     - callbacks réseau (HTTP, WebSocket, timers logiciels).
   - **Préférer** :
     - des buffers `static` ou globaux,
     - des tableaux `char[]` ou structures statiques réutilisées,
     - le ré-emploi d’objets déjà alloués à l’initialisation.

3. **Buffers**
   - Dimensionner les buffers avec une **marge de sécurité** (éviter les tailles "juste assez").
   - Pour les chaînes et payloads réseau, préférer des buffers `char[]` avec taille fixe et vérification stricte de limites avant écriture.

## Règles watchdog et temps d’exécution

1. **Pas de `delay()` longs dans les chemins critiques**
   - Dans `loop()` et dans les tâches critiques (celles qui tournent souvent ou pilotent des capteurs/actionneurs) :
     - **Ne pas utiliser `delay()` > 100 ms**.
     - Si une attente plus longue est nécessaire, la découper en **petits incréments** (ex. boucles avec `delay(10)` ou `vTaskDelay(pdMS_TO_TICKS(10))`) tout en permettant au système et au watchdog de respirer.

2. **Boucles longues et watchdog**
   - Pour toute boucle potentiellement longue (traitement d’une liste, envoi/lecture réseau avec retries, parsing lourd) :
     - S’assurer qu’il y a au moins **un point de sortie raisonnable** (timeout, limite de retries),
     - Appeler périodiquement `esp_task_wdt_reset()` **ou** laisser FreeRTOS reprendre la main via `vTaskDelay`/`delay` courts.
   - Ne jamais laisser une boucle tourner plus de quelques centaines de millisecondes **sans** :
     - pause (`vTaskDelay`, `delay` court),
     - et/ou `esp_task_wdt_reset()` dans les tâches surveillées par le WDT.

## Workflow recommandé lors des modifications

1. **Identifier le contexte**
   - Le changement concerne-t-il :
     - une tâche FreeRTOS ou `loop()` ?
     - du code réseau ou des callbacks asynchrones ?
     - la création/gestion de buffers (réception, envoi, stockage) ?
   - Si oui, appliquer **toutes** les règles de cette skill à la section de code modifiée.

2. **Revoir la gestion mémoire**
   - Vérifier qu’aucune nouvelle allocation dynamique n’est introduite dans une boucle ou un callback fréquent.
   - Là où c’est possible, convertir les allocations dynamiques en :
     - buffers `static`,
     - `char[]` de taille fixe,
     - structures globales ou membres persistants.

3. **Vérifier les délais et le watchdog**
   - Rechercher les appels à `delay` / `vTaskDelay` dans les fonctions/tâches modifiées.
   - S’assurer qu’aucun appel unique ne dépasse **100 ms** dans les chemins critiques.
   - Pour les boucles longues ou les traitements par paquets :
     - insérer `esp_task_wdt_reset()` si la tâche est surveillée par le watchdog,
     - ou découper le traitement avec de petites pauses (`vTaskDelay` courts).

4. **Contrôle final**
   - Confirmer que :
     - le firmware ne crée pas de nouvelles allocations répétées dans les boucles,
     - les temps d’attente sont compatibles avec un système réactif,
     - les marges de mémoire restent raisonnables (objectif > 50 KB, alerte < 30 KB).

