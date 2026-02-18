# WROOM (wroom-test / wroom-prod) : Task WDT 30 s

## Objectif
Éviter les reboots **Task watchdog timeout** pendant les POST HTTP lents (timeout 8 s) : le TWDT par défaut (~5 s) déclenchait un reset avant la fin du POST.

## Moyens mis en œuvre

### 1. Reconfig au runtime (source de vérité, IDF 5+)
Dans **`src/app.cpp`**, au tout début de `setup()` (juste après l’affichage de la cause du reset) :

- Pour les cibles **non-S3** (WROOM) : `esp_task_wdt_deinit()` puis `esp_task_wdt_init()` avec **timeout 30 s** (30 000 ms).
- Cela s’applique **avant** la création des tâches (netTask, webTask, etc.), donc toutes les tâches inscrites au TWDT héritent du timeout 30 s.

**Vérification en log** après flash :

- **`[BOOT] Watchdog configuré: timeout=30000 ms (WROOM)`** → reconfig à 30 s active (IDF 5+).
- **`[BOOT] Watchdog: pas de reconfig runtime (IDF<5), sdkconfig doit avoir CONFIG_ESP_TASK_WDT_TIMEOUT_S=30`** → build en IDF 4.x ; le timeout dépend alors du sdkconfig (voir ci‑dessous).

### 2. custom_sdkconfig (complément pour build / IDF 4)
Les env WROOM (wroom-prod, wroom-test, wroom-beta) n’exécutent pas les scripts pre S3 (ceux-ci sont déclarés uniquement dans `[env:wroom-s3-base]`) et utilisent **sdkconfig_wroom_wdt.txt** pour le TWDT 30 s (wroom-prod et wroom-beta via héritage de wroom-prod, wroom-test en direct).

Dans **`platformio.ini`**, l’env **wroom-test** et **wroom-prod** définissent :

```ini
custom_sdkconfig = file://sdkconfig_wroom_wdt.txt
```

Le fichier **`sdkconfig_wroom_wdt.txt`** (racine du projet) contient :

```
# wroom-test / wroom-prod : timeout Task WDT 30 s (évite reset pendant HTTP/async_tcp)
CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
```

**Comment c’est appliqué**

- Avec **platformio/espressif32** : PlatformIO fusionne le contenu de `custom_sdkconfig` dans la configuration SDK du build (sdkconfig.defaults ou équivalent, selon la version de la plateforme). Les options du fichier remplacent ou complètent les valeurs par défaut.
- Pour les builds **Arduino** : les libs du core peuvent être **précompilées** avec un TWDT à 5 s. Dans ce cas, le sdkconfig du projet ne change pas ces libs ; **la reconfig au runtime dans `app.cpp` est donc nécessaire** pour garantir 30 s. Le custom_sdkconfig reste utile pour cohérence et pour les builds en IDF 4.x (où la reconfig runtime n’est pas compilée).

### 3. Vérification après build (optionnel)
Le script **`tools/verify_wroom_wdt_sdkconfig.ps1`** peut être exécuté après `pio run -e wroom-test` : il cherche un fichier `sdkconfig` généré dans le répertoire de build et affiche si `CONFIG_ESP_TASK_WDT_TIMEOUT_S=30` est présent. Si aucun sdkconfig n’est trouvé (build Arduino sans génération d’un sdkconfig projet), le script le signale : dans ce cas, **la reconfig runtime dans `app.cpp` est la seule garantie** pour un timeout à 30 s.

## Résumé
| Élément | Rôle |
|--------|------|
| **app.cpp** (reconfig 30 s) | Garantit 30 s au runtime pour WROOM (IDF 5+), indépendamment des libs précompilées. |
| **sdkconfig_wroom_wdt.txt** + **custom_sdkconfig** | Aligne la config de build avec 30 s ; nécessaire si IDF < 5 (pas de reconfig runtime). |
| **Log au boot** | Confirmer « Watchdog configuré: timeout=30000 ms (WROOM) » ou, en IDF<5, que le build utilise bien un sdkconfig avec 30 s. |

## OTA au boot et TWDT (WROOM)

Sur WROOM, la vérification OTA au boot (`checkForUpdate()` → `downloadMetadata()` dans netTask) effectue un `http.GET()` HTTPS vers les métadonnées. Si le serveur est injoignable, ce GET peut bloquer au-delà de 30 s (phase TLS / connexion), ce qui déclenche un reboot Task WDT car netTask ne rappelle pas `esp_task_wdt_reset()` à temps.

**Correctif appliqué** : la vérification OTA au boot n’est exécutée **que si le serveur a répondu** au premier GET (config) au boot (`tryFetchConfigFromServer()` retourne au moins 1). Si le serveur est injoignable, netTask logue `[netTask] Boot: OTA skip (serveur injoignable, évite TWDT)` et n’appelle pas `checkForUpdate()` au boot. Le timeout HTTP des métadonnées OTA est par ailleurs limité à 20 s (`OTAConfig::HTTP_TIMEOUT` dans `include/ota_config.h`) pour rester sous la fenêtre TWDT de 30 s.

Voir `src/app_tasks.cpp` (bootServerReachable, condition avant l’appel à `checkForUpdate()` au boot).

## Lien avec le S3 (même code, timeout TWDT différent)

Le code réseau/OTA (netTask, `checkForUpdate()`, `downloadMetadata()`) est **partagé** entre WROOM et S3. Sur **S3**, le Task WDT est configuré à **300 s** dans `src/app.cpp` (et `sdkconfig_s3_wdt.txt`), donc un blocage netTask de 30–60 s dans `downloadMetadata()` ne déclenche pas de reboot Task WDT sur S3. La même cause (blocage HTTPS/OTA) se manifeste donc surtout sur WROOM à cause du TWDT 30 s.

Les watchdogs documentés spécifiquement pour le S3 concernent le **boot** (TG1WDT, TG0WDT), pas ce scénario runtime : voir `docs/technical/ESP32S3_TG1WDT_BOOT_FIX.md`.

**Diagnostic S3** : en cas de watchdogs observés sur S3 en runtime, vérifier dans les logs si le reset est un Task WDT (après 300 s sans feed) ou un TG1WDT/TG0WDT au boot, pour distinguer un blocage netTask/OTA d’un problème de boot S3.

## Références
- Timeout HTTP POST : `HTTP_POST_TIMEOUT_MS` (8 s) dans `include/config.h`.
- S3 / autre WDT : `docs/technical/ESP32S3_TG1WDT_BOOT_FIX.md`, `docs/technical/ISSUE_TG0WDT_ESP32S3_BOOT_LOOP.md`.
