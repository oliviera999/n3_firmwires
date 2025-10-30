<!-- 3e56f1dc-fee8-46b9-959c-1e2ba9a0140d 50526371-3cdd-44eb-ae89-4553e7d4118a -->
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

### To-dos

- [x] Collecter compilation actuelle + mesures heap/flash pour servir de référence
- [x] Analyser et cartographier `src/display_view.cpp` (responsabilités, dépendances, points critiques mémoire)
- [x] Définir découpage et stratégie de tests pour la refactorisation du module affichage
- [x] Introduire `DisplayCache` et migrer les comparaisons `_last*`
- [x] Extraire la barre de statut (`StatusBarRenderer`)
- [x] Refactoriser les écrans principaux/secondaires via renderers dédiés
- [x] Ajouter `DisplaySession` (RAII) et finaliser nettoyage `display_view.cpp`
- [ ] Exécuter tests (build + monitoring 90s) et documenter version (reporté)
- [x] Étape 2 — analyser `src/web_server.cpp` (structure, responsabilités, dépendances)
- [x] Étape 2 — définir découpage cible + stratégie de tests (REST, WebSocket, emails)
- [x] Étape 2 — introduire `WebServerContext` et migrer les helpers JSON/email
- [ ] Étape 2 — extraire les routes UI/Status dans des modules dédiés
- [ ] Étape 2 — isoler `/action` & services async (NotificationService, AutomationServiceAdapter)