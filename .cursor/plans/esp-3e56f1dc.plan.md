---
name: Plan Refactorisation Progressive
overview: ""
todos:
  - id: 7eabf932-4f9f-421b-b668-dba5afa0f7b5
    content: Collecter compilation actuelle + mesures heap/flash pour servir de référence
    status: completed
  - id: 2996380d-1a20-4e2f-a58e-be18e0d75d6b
    content: Analyser et cartographier `src/display_view.cpp` (responsabilités, dépendances, points critiques mémoire)
    status: completed
  - id: 094eac66-45fd-4da1-8f5c-6d863394aeaf
    content: Définir découpage et stratégie de tests pour la refactorisation du module affichage
    status: completed
  - id: 9b25c1ef-c8cf-4814-829d-cf0cf61c79ac
    content: Introduire `DisplayCache` et migrer les comparaisons `_last*`
    status: completed
  - id: 02e23e95-fac8-4baa-b28f-caede9367f8e
    content: Extraire la barre de statut (`StatusBarRenderer`)
    status: completed
  - id: f89c84f4-5076-439d-b5e0-1063c7e15f5e
    content: Refactoriser les écrans principaux/secondaires via renderers dédiés
    status: completed
  - id: c04eb70d-fe4c-4ea8-8aa0-d152c36ce331
    content: Ajouter `DisplaySession` (RAII) et finaliser nettoyage `display_view.cpp`
    status: completed
  - id: a14d4f3c-5634-4629-a4b1-3d4fa7baab5d
    content: Exécuter tests (build + monitoring 90s) et documenter version (reporté)
    status: pending
  - id: 69fd5816-713b-4988-b6f4-aabe0a906b52
    content: Étape 2 — analyser `src/web_server.cpp` (structure, responsabilités, dépendances)
    status: completed
  - id: f1837bf1-b1f1-4bce-8fb7-5b6036fbd4a6
    content: Étape 2 — définir découpage cible + stratégie de tests (REST, WebSocket, emails)
    status: completed
  - id: b55fec99-785b-4a60-a03c-2d5d83d26473
    content: Étape 2 — introduire `WebServerContext` et migrer les helpers JSON/email
    status: completed
  - id: 016ad9f1-9923-4165-959d-40b1bad603c5
    content: Étape 2 — extraire les routes UI/Status dans des modules dédiés
    status: pending
  - id: 0694f44c-5909-49c8-bd41-985386ec8ed0
    content: Étape 2 — isoler `/action` & services async (NotificationService, AutomationServiceAdapter)
    status: pending
---

# Plan Refactorisation Progressive

## Étape 0 – Préparation ✅

- Compilation & taille binaire référencées (`pio run -t size`).
- Logs runtime (heap boot/min) archivés dans `docs/technical/REFAC_BASELINE_2025-10-29.md`.
- Monitoring post-flash listé pour répétition après chaque refactor.

## Étape 1 – Module d’affichage (`src/display_view.cpp`)

### Terminées

- Cartographie complète des responsabilités et dépendances (`docs/technical/DISPLAY_VIEW_CARTOGRAPHIE.md`).
- Stratégie de découpage + tests définie (`docs/technical/DISPLAY_VIEW_PLAN_REFAC.md`).
- Extraction initiale des helpers texte (`DisplayTextUtils`) + version 11.89.
- `DisplayCache`, `StatusBarRenderer`, `MainScreenRenderer`, `CountdownRenderer`, `InfoScreenRenderer` et `DisplaySession` intégrés (v11.90 flashé).

### Restant

- Monitoring série 90s différé (à planifier si requis).

## Étape 2 – Serveur web (`src/web_server.cpp`)

- Cartographie détaillée réalisée (`docs/technical/WEBSERVER_CARTOGRAPHIE.md`).
- Plan de refactorisation et stratégie de tests définis (`docs/technical/WEBSERVER_PLAN_REFAC.md`).
- `WebServerContext` introduit : helpers `sendJson`, `ensureHeap`, `sendManualActionEmail`, seuils mémoire unifiés (v11.91).
- Prochaines sous-étapes : extraire les groupes de routes (`UiRoutes`, `StatusRoutes`, `ControlRoutes`, `ConfigRoutes`) et déplacer les services asynchrones vers des modules dédiés.

## Étape 3 – Automatismes (`src/automatism.cpp` + sous-dossiers)

- Formaliser machine d’états, isoler NVS/sleep/email.

## Étape 4 – OTA (`src/ota_manager.cpp`)

- Séparer métadonnées, téléchargement, callbacks FreeRTOS.

## Étape 5 – NVS (`src/nvs_manager.cpp`)

- Extraire mutex/cache différé, tests d’endurance.

## Étape 6 – Automatism Network & Sensors

- Découper `automatism_network.cpp`, segmenter `sensors.cpp`.

## Étape 7 – Boucle d’amélioration continue

- Après chaque refactor : build, monitoring 90s, mise à jour `VERSION.md`, ajustement roadmap.