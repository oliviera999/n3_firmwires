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
Dans **`platformio.ini`**, l’env **wroom-test** (et wroom-prod si aligné) définit :

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

## Références
- Timeout HTTP POST : `HTTP_POST_TIMEOUT_MS` (8 s) dans `include/config.h`.
- S3 / autre WDT : `docs/technical/ESP32S3_TG1WDT_BOOT_FIX.md`, `docs/technical/ISSUE_TG0WDT_ESP32S3_BOOT_LOOP.md`.
