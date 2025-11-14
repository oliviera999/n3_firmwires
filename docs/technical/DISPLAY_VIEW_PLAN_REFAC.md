# Plan de refactorisation `DisplayView`

## Objectifs

- Réduire la taille du module (`~1 070` lignes) en séparant clairement les responsabilités.
- Limiter la fragmentation mémoire causée par `String` dans les chemins critiques.
- Rendre testable chaque écran/overlay sans nécessiter un ESP32 réel.

## Découpage proposé

1. **`DisplayTextUtils` (nouveau fichier)**
   - Contiendra `utf8ToCp437`, `printClipped`, helpers de centrage et buffers partagés (`char[64]`).
   - API pure, sans dépendance à la classe principale.

2. **`DisplayCache` (struct dédiée)**
   - Deux sous-structs : `StatusCache` (send/recv/RSSI/etc.) et `MainCache` (temp/levels/time).
   - Fournira méthodes `needsStatusUpdate(...)`, `needsMainUpdate(...)` basées sur seuils.

3. **Rendu status bar**
   - Extraire `drawStatus`/`drawStatusEx` vers `StatusBarRenderer` (classe/fonctions libres) qui reçoit `Adafruit_SSD1306&` + cache.
   - Gestion OTA overlay mutualisée (pas de duplication `showOtaProgress`/`showOtaProgressEx`).

4. **Écrans principaux / secondaires**
   - `MainScreenRenderer` pour `showMain` + widgets internes (température, niveaux, horloge).
   - `CountdownRenderer` pour `showCountdown` et `showFeedingCountdown` (param phase/isManual).
   - `InfoScreenRenderer` pour `showVariables`, `showDiagnostic`, `showServerVars`, `showSleep*`.

5. **Gestion mode d’affichage**
   - Introduire une classe légère `DisplaySession` (RAII) qui gère `_updateMode`, `_needsFlush`, `_isDisplaying` et `lockScreen`.
   - Simplifier `beginUpdate`/`endUpdate` en wrappers vers cette classe.

6. **Migration progressive**
   - Étape A : introduire helpers (TextUtils + buffers statiques) sans changer la logique.
   - Étape B : insérer `DisplayCache` et remplacer les tests sur `_last*` par appels au cache.
   - Étape C : déplacer le code de rendu dans sous-modules (status bar, main, countdown).
   - Étape D : nettoyer doublons (`showOtaProgress`/`Ex`, mutualisation overlay) et documenter API.

## Stratégie de tests

- **Tests unitaires (PC)** : compiler helpers (`DisplayTextUtils`, `DisplayCache`) avec `Unity` ou `doctest` via target host pour valider conversion UTF-8/CP437 et logique de seuils.
- **Simulation OLED** : utiliser stub `Adafruit_SSD1306` (déjà présent quand `FEATURE_OLED=0`) pour vérifier qu’aucun path n’accède à l’écran lorsque `_present=false`.
- **Tests intégrés ESP32** :
  - Recompiler en mode `FEATURE_OLED=1`, flasher et exécuter `monitor_90s_*` pour vérifier watchdog + heap.
  - Capturer `Serial` pour confirmer : splash <3s, status bar mise à jour, countdown fluide.
  - Vérifier logs `DisplayView::showOtaProgress` / overlay durant OTA simulée (utiliser script OTA test).
- **Validation mémoire** : comparer `ESP.getFreeHeap()` au boot avant/après, vérifier absence de nouvelles allocations dans boucle (`String::reserve` dans caches au besoin).
- **Régression UI** : photographier écrans (main/status/countdown) avant refactor et comparer visuellement.

## Risques & mitigations

- **Verrouillage écran** : préserver la logique `lockScreen` / `isLocked` en l’encapsulant dans `DisplaySession` pour éviter régressions.
- **Overlay OTA** : s’assurer qu’il reste prioritaire (dessiné après status bar). Ajouter tests manuels via `showOtaProgressOverlay`.
- **Internationalisation** : maintenir support CP437 (tests conversion accentués).
- **Temps d’exécution** : monitorer durée `showMain` via instrumentation (ex. `micros()`) pendant phase de validation.











