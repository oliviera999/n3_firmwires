# Analyse du rafraîchissement OLED – FFP5CS

## 1. Constantes et emplacements

| Constante | Fichier | Valeur | Rôle |
|-----------|---------|--------|------|
| `DisplayConfig::OLED_INTERVAL_MS` | `include/config.h` L.583 | **80 ms** | Intervalle entre deux mises à jour OLED en mode normal |
| `DisplayConfig::OLED_COUNTDOWN_INTERVAL_MS` | `include/config.h` L.584 | **250 ms** | Intervalle en mode countdown (nourrissage / timer) |
| `DisplayConfig::SCREEN_SWITCH_INTERVAL_MS` | `include/config.h` L.600 | **6000 ms** | Bascule automatique écran principal ↔ écran Variables |

Fréquences théoriques maximales (si la boucle tournait à cette cadence) :
- Mode normal : 1000/80 ≈ **12,5 Hz**
- Mode countdown : 1000/250 = **4 Hz**

---

## 2. Flux d’exécution réel

### 2.1 Qui déclenche l’affichage ?

L’affichage OLED est **uniquement** mis à jour depuis **automationTask** (`src/app_tasks.cpp`), qui :

1. Attend des données sur la queue capteurs :  
   `xQueueReceive(g_sensorQueue, &readings, pdMS_TO_TICKS(1000))` (timeout 1 s).
2. **Quand une lecture arrive** : exécute dans l’ordre  
   `g_ctx->automatism.update(readings)` puis `g_ctx->automatism.updateDisplayWithReadings(readings)`.  
   La dernière lecture est mémorisée (`s_lastReadings`, `s_haveReadings`) pour la branche timeout.
3. **Quand la queue expire (timeout 1 s)** : si au moins une lecture a déjà été reçue (`s_haveReadings`), appelle **uniquement**  
   `g_ctx->automatism.updateDisplayWithReadings(s_lastReadings)`  
   pour rafraîchir l’écran (heure RTC, barre d’état, etc.) sans relancer la logique métier ni `update(readings)`.

Aucun timer dédié, aucune boucle `loop()` et aucune autre tâche n’appellent `updateDisplay()` ou `updateDisplayWithReadings()`.  
`wifi.loop(&oled)` dans `app.cpp` ne met pas à jour le contenu principal (capteurs / variables) ; il utilise l’OLED pour le WiFi si besoin.

### 2.2 Fréquence réelle de rafraîchissement

- **sensorTask** envoie des lectures sur `g_sensorQueue` à l’intervalle **`TimingConfig::SENSOR_TASK_INTERVAL_MS` = 10 000 ms** (10 s).
- **automationTask** se réveille soit à réception d’une lecture (environ toutes les 10 s), soit au **timeout 1 s** (pas de donnée).
- En **timeout** : si `s_haveReadings` est true, l’affichage est rafraîchi avec `s_lastReadings` ; aucun `update(readings)`.

Conséquence : **l’écran est rafraîchi environ une fois par seconde** (~1 Hz) grâce à la branche timeout, en plus d’un cycle complet (update + affichage) à chaque réception capteur (~0,1 Hz). L’heure RTC et la barre d’état suivent donc la seconde près.

Les constantes **80 ms** et **250 ms** restent des **throttles** dans `updateDisplayInternal` pour limiter le nombre de dessins lors d’un même cycle (voir § 3).

---

## 3. Double appel et garde 80 ms

Dans un même cycle d’automationTask, l’affichage est sollicité **deux fois** :

1. **Premier appel** :  
   `update(readings)` → `updateFeedingAndDisplay(r, now)` → **`updateDisplayInternal(ctx)`**
2. **Deuxième appel** :  
   **`updateDisplayWithReadings(readings)`** → **`updateDisplayInternal(ctx)`**

Les deux appels ont lieu à quelques millisecondes d’intervalle (même `millis()`). Dans `updateDisplayInternal` :

```cpp
if (currentMillis - _lastOled < displayInterval) {
    return;  // pas de draw
}
_lastOled = currentMillis;
// ... dessin ...
```

- Le **premier** appel passe la garde, met à jour `_lastOled` et dessine.
- Le **second** appelle voit `(currentMillis - _lastOled) < 80` et **sort sans dessiner**.

Donc **un seul dessin par cycle** malgré les deux entrées dans `updateDisplayInternal`. La valeur 80 ms sert bien de throttle entre appels rapprochés, mais dans la situation actuelle le goulot d’étranglement est la **période des capteurs (10 s)**.

---

## 4. Logique dans `updateDisplayInternal` (résumé)

- **Splash** : timeout `SPLASH_DURATION_MS + 2000` pour forcer la fin du splash.
- **Changement d’écran** : toutes les **6 s** (`SCREEN_SWITCH_INTERVAL_MS`), bascule `_oledToggle` (principal ↔ Variables) et reset des caches (`resetMainCache`, `resetStatusCache`, `resetVariablesCache`).
- **Choix de l’intervalle de throttle** :
  - **250 ms** si countdown actif (timer ou phase nourrissage),
  - **80 ms** sinon.
- **Contenu affiché** selon l’état :
  - Countdown timer → `showCountdown` + barre d’état
  - Phase nourrissage → `showFeedingCountdown` + barre d’état
  - Sinon : écran principal (`showMain`) ou écran Variables (`showVariables`) selon `_oledToggle`, puis `drawStatus` en overlay.

`DisplayView` utilise des caches (main, status, variables) pour éviter de redessiner si les données n’ont pas changé ; le reset des caches à chaque changement d’écran force un redessin complet.

---

## 5. Incohérences et pistes

### 5.1 Constantes dupliquées (résolu)

La constante **`oledInterval`** et la fonction **`getRecommendedDisplayIntervalMs()`** (anciennement dans `automatism.h` / `automatism.cpp`) ont été supprimées. La source de vérité pour l'intervalle de rafraîchissement OLED est **`DisplayConfig::OLED_INTERVAL_MS`** et **`DisplayConfig::OLED_COUNTDOWN_INTERVAL_MS`** dans `include/config.h`. L'affichage est entièrement géré dans **automationTask** (plus de displayTask dédiée).


### 5.2 Rafraîchissement piloté par le timeout 1 s (implémenté)

En cas de **timeout** de la queue capteurs (1 s sans donnée), automationTask appelle `updateDisplayWithReadings(s_lastReadings)` avec la dernière lecture mémorisée, **sans** rappeler `update(readings)`. Cela permet un rafraîchissement **~1 Hz** (heure RTC, barre d’état) sans modifier la cadence des capteurs (10 s). Détails : `src/app_tasks.cpp` (statiques `s_lastReadings`, `s_haveReadings` ; branche `else` du `xQueueReceive`).

---

## 6. Synthèse

| Élément | Valeur / comportement |
|--------|------------------------|
| Intervalle config (normal) | **80 ms** (`config.h` `OLED_INTERVAL_MS`) |
| Intervalle config (countdown) | **250 ms** (`OLED_COUNTDOWN_INTERVAL_MS`) |
| Bascule d’écran | **6 s** (`SCREEN_SWITCH_INTERVAL_MS`) |
| **Fréquence réelle de rafraîchissement** | **~1 Hz** (branche timeout 1 s : `updateDisplayWithReadings(s_lastReadings)`) + **~0,1 Hz** à réception capteur (update + affichage) |
| Appels par cycle (donnée reçue) | 2 (`update` + `updateDisplayWithReadings`), 1 seul dessin grâce à la garde 80 ms |
| Branche timeout | Rafraîchit l'écran avec `s_lastReadings` sans `update(readings)` ; heure RTC et barre d'état à la seconde près |
| Source de vérité des constantes | `include/config.h` namespace `DisplayConfig` |

Les constantes 80 ms et 250 ms restent des **throttles** dans `updateDisplayInternal`. Le rafraîchissement effectif est piloté par le **timeout 1 s** de la queue (affichage avec dernière lecture) et par la **réception capteur** (cycle complet).

---

## 7. Impact RAM / heap / CPU (rafraîchissement timeout 1 s)

### RAM (BSS / statique)

- **`static SensorReadings s_lastReadings`** : une copie de la structure `SensorReadings` (3 × `float` + 4 × `uint16_t` → **20 octets** sur ESP32, sans padding).
- **`static bool s_haveReadings`** : **1 octet**.

**Total : ~21 octets** en BSS dans `automationTask`. Aucun impact sur la stack de la tâche (les statiques sont en BSS, pas sur la pile).

### Heap

- **Aucune allocation dynamique** (`malloc` / `new`) ajoutée. Les variables sont statiques.
- **Impact heap : 0.**

### CPU

- **Avant** : en timeout (1 s sans donnée), la branche `else` ne faisait pas d’appel à l’affichage (uniquement log éventuel et fallback réseau si WiFi).
- **Après** : à chaque timeout, si `s_haveReadings`, appel à `updateDisplayWithReadings(s_lastReadings)` → `updateDisplayInternal` → un cycle de dessin OLED (I2C, `snprintf`, caches DisplayView). Une mise à jour OLED typique (texte + barre d’état) est de l’ordre de **quelques ms à ~20 ms** (I2C 100–400 kHz, buffer partiel).
- Sur **10 s** : ~9 timeouts → ~9 rafraîchissements supplémentaires, soit **~50–150 ms de CPU** en plus sur automationTask sur cette période, soit **~0,5–1,5 %** de charge CPU supplémentaire sur cette tâche. Négligeable par rapport au reste (lecture capteurs, réseau, logique métier).
