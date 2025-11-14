# Cartographie `DisplayView` – état au 2025-10-29

## Responsabilités principales

- **Initialisation matérielle** : gestion I2C, détection OLED, splash screen (3 s) et configuration CP437 (`begin`).
- **Helpers texte** : conversion UTF-8 → CP437 (`utf8ToCp437`), impression tronquée (`printClipped`).
- **Gestion d’état** : caches des dernières valeurs (`_lastTemp*`, niveaux eau/lumière, `String _lastTimeStr`), status bar (`_lastSendState`, `_lastRssi`, etc.), modes d’affichage (`_immediateMode`, `_updateMode`, `_needsFlush`).
- **Verrouillage & splash** : verrou d’écran temporaire (`lockScreen`), affichage splash différé (`_splashUntil`), forçage de flush.
- **Widgets graphiques** : status bar (`drawStatus`, `drawStatusEx`, icônes flèches/Wi-Fi/OTA), compte à rebours nourrissage (`showFeedingCountdown`), décomptes génériques (`showCountdown`).
- **Écran principal** : affichage des capteurs et horloge (`showMain`), avec caches pour limiter les redraws.
- **Écrans secondaires** : variables actionneurs (`showVariables`), diagnostics (`showDiagnostic`), variables serveur (`showServerVars`).
- **OTA & sommeil** : écrans dédiés (`showOtaProgress`, `showOtaProgressEx`, overlay, `showSleepReason`, `showSleepInfo`).

## Dépendances clés

- `project_config.h` / `ProjectConfig::VERSION`, `ProjectConfig::PROFILE_TYPE` pour afficher version/profil.
- `pins.h` (pins I2C, adresse OLED) et `ExtendedSensorConfig::I2C_STABILIZATION_DELAY_MS` pour init hardware.
- `Features::OLED_ENABLED`, `FEATURE_OLED` pour activer/désactiver l’affichage.
- `oled_logo.h` (logo splash), `Wire`, `Adafruit_SSD1306` pour le rendu.
- Interactions avec `TimerManager` implicites via `millis()` pour locks/splash.
- `String` Arduino utilisé pour toutes conversions dynamiques (risque fragmentation).

## Zones critiques mémoire/performance

- Utilisation intensive de `String` (conversion, concaténation) dans `showMain`, `showFeedingCountdown`, `showServerVars` → fragmentation possible.
- Buffers locaux multiples (`char buf[32]`, `char percentStr[12]`, etc.) mais absence de buffers statiques mutualisés.
- Status bar redessine zones par rectangles pleins; lambdas `drawArrow` allouent closures capturant `this` (OK mais complexité élevée).
- Overlay OTA maintenu via `String`/`snprintf` à chaque draw; duplication `showOtaProgress` vs `showOtaProgressEx`.
- Pas de séparation stricte entre logique métier (états actionneurs) et rendu; la classe manipule directement des `bool`/`String` venant de l’extérieur.

## Axes de découpage envisagés

- **Module helpers texte** : regrouper `utf8ToCp437`, `printClipped`, centrements, formattage (réutilisable par d’autres vues).
- **Gestion cache** : isoler un `DisplayCache` (status + main screen) pour clarifier les conditions de redraw et fournir API de comparaison.
- **Widgets** : créer des fonctions/classes pour status bar, compte à rebours, overlay OTA afin de réduire la taille des méthodes.
- **Écrans spécialisés** : séparer en mini-contrôleurs (`MainScreenRenderer`, `CountdownRenderer`, `DiagnosticRenderer`).
- **Strings → buffers** : planifier migration vers buffers `char[]` partagés (par ex. `constexpr size_t TEXT_BUFFER = 64`) pour limiter fragmentation.

## Points d’attention complémentaires

- `lockScreen` utilise `_lockUntil` + `millis()` avec modification via `const_cast` dans `isLocked()` → à documenter pour future refactor.
- `splashActive()` (non listée dans `ripgrep` car inline?) à vérifier lors du découpage.
- Interaction avec `TimerManager::process()` (flush différé) à identifier lorsque l’on préparera les tests.
- Vérifier la dépendance au raster 128×64 : coordonnées codées en dur dans toutes les méthodes; prévoir configuration centralisée.











