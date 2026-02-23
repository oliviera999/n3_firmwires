# ESP32-S3 PSRAM OPI et IWDT — Guide technique et réutilisation

Documentation détaillée de tout ce qui a été mis en place pour faire tourner un firmware sur **ESP32-S3 avec PSRAM 8 Mo OPI** (variant `qio_opi`) sans reset **TG1WDT_SYS_RST**. Ce guide permet de réutiliser la même approche dans d’autres projets (PlatformIO, Arduino, ESP32-S3 N16R8 ou équivalent).

---

## 1. Contexte et objectif

### 1.1 Matériel cible

- **Module** : ESP32-S3-WROOM-1-N16R8 (ou carte basée dessus).
- **Flash** : 16 Mo, mode QIO, 80 MHz.
- **PSRAM** : 8 Mo, interface **Octal (OPI)**.
- **Variant Arduino** : `qio_opi` (quad I/O flash + octal PSRAM).

### 1.2 Objectif

- Faire booter l’ESP avec la PSRAM activée.
- Tester la PSRAM (alloc, écriture, lecture, libération) sur des blocs de 256 Ko à 1 Mo.
- Éviter les resets **rst:0x8 (TG1WDT_SYS_RST)** dus à l’**Interrupt Watchdog (IWDT)** pendant le boot ou les tests.

### 1.3 Problème rencontré

Dès le début des tests PSRAM (alloc + boucles d’écriture/lecture), la carte redémarrait en **TG1WDT_SYS_RST** :

- L’**IWDT** utilise le **Timer Group 1 (TG1)** et un timeout par défaut de **300 ms**.
- Il est nourri par le **tick FreeRTOS** ; si le CPU reste bloqué plus de 300 ms sans laisser le tick s’exécuter (par ex. longue boucle d’écriture PSRAM sans `vTaskDelay`), l’IWDT déclenche un reset.
- Le **bootloader** fourni par la plateforme (binaire précompilé) peut **activer l’IWDT** au démarrage. Notre firmware est compilé avec un sdkconfig où l’IWDT est désactivé, mais le bootloader a déjà lancé l’IWDT avant le passage à notre code. Donc au démarrage de l’application, l’IWDT est parfois **déjà actif** avec 300 ms.

---

## 2. Solutions mises en place (synthèse)

Deux volets complémentaires :

1. **Patch du sdkconfig du variant `qio_opi`** : mettre `CONFIG_ESP_INT_WDT=0` dans le `sdkconfig.h` du variant (framework Arduino ESP32), pour que le **code applicatif et le framework** ne démarrent pas l’IWDT. Cela ne change pas le bootloader (binaire fourni par la plateforme).
2. **Désactivation de l’IWDT au runtime** : au tout début de `setup()`, avant toute opération longue, désactiver le **MWDT1** (Timer Group 1) via le **HAL WDT**. Ainsi, si le bootloader a laissé l’IWDT actif, on le coupe dès l’entrée dans notre application.

En plus, les tests PSRAM sont découpés en **chunks de 4 Ko** avec **`vTaskDelay(20)`** entre chaque chunk pour ne pas bloquer le CPU trop longtemps (utile si on ne désactivait pas l’IWDT).

---

## 3. Structure du projet

```
test psram s3/
├── platformio.ini              # Env, board, qio_opi, custom_sdkconfig, script pre-patch
├── sdkconfig_s3_psram.txt      # Options sdkconfig (PSRAM, IWDT, TWDT, partitions)
├── config/
│   └── partitions/
│       └── partitions_s3_minimal.csv   # Table nvs + app0
├── tools/
│   └── pio_s3_psram_patch_iwdt.py      # Pre-script : patch CONFIG_ESP_INT_WDT=0 (qio_opi)
├── src/
│   └── main.cpp                # setup (désactivation IWDT, Serial, TWDT, tests PSRAM), loop
├── DOC_ESP32S3_PSRAM_ET_IWDT.md   # Ce document
└── TESTS_ET_RESULTATS.md        # Journal des tests (optionnel)
```

---

## 4. Détail des fichiers

### 4.1 `platformio.ini`

- **Board** : `esp32-s3-devkitc-1` (ou équivalent S3).
- **Mémoire** : `board_build.arduino.memory_type = qio_opi` pour PSRAM OPI 8 Mo.
- **Flash** : `board_upload.flash_size = 16MB`, `board_upload.flash_mode = qio`.
- **Partitions** : table custom pointant vers `config/partitions/partitions_s3_minimal.csv`.
- **sdkconfig** : `custom_sdkconfig = file://sdkconfig_s3_psram.txt`.
- **Pre-script** : `extra_scripts = pre:tools/pio_s3_psram_patch_iwdt.py` pour patcher le variant `qio_opi` avant le build.
- **Port** : `upload_port = COM7` (à adapter selon la machine).

Exemple minimal à réutiliser :

```ini
[env:s3-n16r8-psram]
platform = platformio/espressif32
framework = arduino
board = esp32-s3-devkitc-1
board_upload.flash_size = 16MB
board_upload.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_build.partitions = config/partitions/partitions_s3_minimal.csv
custom_sdkconfig = file://sdkconfig_s3_psram.txt
extra_scripts = pre:tools/pio_s3_psram_patch_iwdt.py
upload_port = COM7
monitor_speed = 115200
```

### 4.2 `sdkconfig_s3_psram.txt`

Contenu type (à adapter selon les besoins) :

- **PSRAM** : `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_BOOT_INIT=y`.
- **IWDT** : `# CONFIG_ESP_INT_WDT is not set` (désactivé côté config app).
- **TWDT** : ne pas l’initialiser au boot (`# CONFIG_ESP_TASK_WDT_INIT is not set`), timeout 300 s si on l’init en setup.
- **Partitions** : `CONFIG_PARTITION_TABLE_CUSTOM=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions/partitions_s3_minimal.csv"`.
- **Rollback** : désactiver `CONFIG_APP_ROLLBACK_ENABLE` pour S3 si besoin (évite blocage sur flash vierge).
- **Stack loop** : `CONFIG_ARDUINO_LOOP_STACK_SIZE=32768` si besoin.
- **Coredump** : désactivé pour table minimal (pas de partition coredump).

Exemple (extrait) :

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_BOOT_INIT=y
# CONFIG_ESP_INT_WDT is not set
# CONFIG_ESP_TASK_WDT_INIT is not set
CONFIG_ESP_TASK_WDT_TIMEOUT_S=300
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions/partitions_s3_minimal.csv"
```

### 4.3 `config/partitions/partitions_s3_minimal.csv`

Table minimale : NVS + une partition app.

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000
app0,     app,  factory, 0x10000, 0x1F0000
```

Les offsets et tailles sont à aligner avec la doc ESP32 (bootloader, partition table). Pour un projet plus gros, augmenter la taille d’`app0` ou ajouter d’autres partitions.

### 4.4 `tools/pio_s3_psram_patch_iwdt.py`

**Rôle** : avant chaque build, modifier le `sdkconfig.h` du **variant `qio_opi`** dans le paquet du framework Arduino ESP32 pour mettre `CONFIG_ESP_INT_WDT` à **0**.

- **Fichier cible** :  
  `{PACKAGE_DIR}/tools/sdk/esp32s3/qio_opi/include/sdkconfig.h`
- **Modification** : remplacer `#define CONFIG_ESP_INT_WDT 1` par `#define CONFIG_ESP_INT_WDT 0`.
- **Quand** : à chaque `pio run` (pre-script). Si le fichier est déjà patché, le script ne change rien (idempotent).

Pour réutiliser dans un autre projet : copier ce script dans `tools/` et ajouter `extra_scripts = pre:tools/pio_s3_psram_patch_iwdt.py` dans `platformio.ini`. Le chemin du framework est résolu via `env.PioPlatform().get_package_dir("framework-arduinoespressif32")`.

**Important** : le patch s’applique au **variant utilisé** (ici `qio_opi`). Si tu utilises un autre variant (ex. `dio_opi`), adapter le chemin dans le script (`qio_opi` → `dio_opi`).

### 4.5 Désactivation de l’IWDT au runtime dans `src/main.cpp`

Au **tout début** de `setup()`, avant `Serial.begin()` et toute opération longue :

1. Inclure le HAL WDT :
   - `#include "hal/wdt_hal.h"`
   - `#include "hal/wdt_types.h"`
2. Créer un contexte `wdt_hal_context_t` et initialiser avec **WDT_MWDT1** (Timer Group 1 = IWDT).
3. Désactiver la protection en écriture, appeler **`wdt_hal_disable()`**, puis réactiver la protection.

Extrait à réutiliser :

```cpp
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"

void setup() {
  // Désactiver l'IWDT (Timer Group 1) au plus tôt — le bootloader peut l'avoir activé à 300 ms
  {
    wdt_hal_context_t iwdt_hal = {};
    wdt_hal_init(&iwdt_hal, WDT_MWDT1, 40000, false);  // MWDT1 = IWDT, prescaler 40000
    wdt_hal_write_protect_disable(&iwdt_hal);
    wdt_hal_disable(&iwdt_hal);
    wdt_hal_write_protect_enable(&iwdt_hal);
  }

  // ... reste du setup (Serial, TWDT, etc.)
}
```

Le HAL WDT est une API interne ESP-IDF ; elle est disponible dans le framework Arduino car les chemins `hal/include` sont dans les includes du build S3. Utiliser **WDT_MWDT1** (Timer Group 1), pas WDT_MWDT0.

### 4.6 Tests PSRAM avec chunks et yield

Pour rester robuste même si l’IWDT n’était pas désactivé, le test PSRAM :

- Alloue un bloc (ex. 256 Ko ou 1 Mo) avec `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- Traite par **chunks de 4 Ko** : pour chaque chunk, écriture d’un motif, puis **`vTaskDelay(pdMS_TO_TICKS(20))`**.
- Relit par chunks avec le même `vTaskDelay(20)`.
- Libère le bloc avec `heap_caps_free()`.

Cela évite de bloquer le CPU plus de ~20 ms d’affilée. En réutilisant dans un autre projet, on peut garder ce schéma pour toute opération PSRAM longue.

### 4.7 Détection PSRAM avec sdkconfig custom

Avec un sdkconfig custom (PSRAM OPI), **`psramFound()`** et **`ESP.getPsramSize()`** peuvent rester à 0, alors que la PSRAM est bien utilisable. Il faut considérer la PSRAM comme disponible si **`heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0`**.

---

## 5. Réutilisation dans un autre projet

### 5.1 Checklist

1. **platformio.ini**
   - Board S3, `board_build.arduino.memory_type = qio_opi` (ou le variant OPI utilisé).
   - `custom_sdkconfig` pointant vers un fichier du type `sdkconfig_s3_psram.txt`.
   - `extra_scripts = pre:tools/pio_s3_psram_patch_iwdt.py`.

2. **sdkconfig**
   - PSRAM activé (`CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_BOOT_INIT=y`).
   - IWDT désactivé dans la config : `# CONFIG_ESP_INT_WDT is not set`.
   - TWDT : selon besoin (non init au boot puis init en setup, ou laisser par défaut).

3. **Partitions**
   - Créer `config/partitions/partitions_s3_minimal.csv` (ou équivalent) et le référencer dans `platformio.ini` et dans le sdkconfig.

4. **Script de patch**
   - Copier `tools/pio_s3_psram_patch_iwdt.py` ; si le variant n’est pas `qio_opi`, adapter le chemin (ex. `dio_opi`, `opi_opi`).

5. **Code applicatif**
   - Au début de `setup()`, ajouter le bloc de désactivation IWDT (HAL WDT, WDT_MWDT1) comme en section 4.5.
   - Pour toute boucle longue sur PSRAM, découper en chunks et insérer `vTaskDelay` si tu réactives l’IWDT plus tard.

### 5.2 Variantes possibles

- **Autre variant (dio_opi, opi_opi)** : dans le script Python, remplacer `qio_opi` par le bon répertoire sous `tools/sdk/esp32s3/`.
- **IWDT laissé actif** : ne pas appeler `wdt_hal_disable()` ; garder les chunks + `vTaskDelay` dans les tests PSRAM pour ne pas dépasser 300 ms.
- **Production** : pour un firmware de production, réfléchir à réactiver l’IWDT (ou à augmenter fortement son timeout) et à éviter les longues sections critiques sans yield ; documenter le choix.

---

## 6. Risques de désactiver l’IWDT

- **Rôle de l’IWDT** : détecter un blocage prolongé des interruptions (ou du tick FreeRTOS) et déclencher un reset (souvent après 300 ms). C’est un filet de sécurité.
- **Risque en le désactivant** : un bug qui bloque les IRQ ou le tick plus de 300 ms ne provoquera plus de reset ; le système peut rester figé ou incohérent au lieu de redémarrer.
- **Contexte actuel** : projet de **test/validation** PSRAM ; la désactivation est un compromis pour permettre les tests. En **production**, il est préférable de réactiver l’IWDT (ou d’augmenter son timeout) et de s’assurer qu’aucune section de code ne bloque plus de 300 ms sans yield.

---

## 7. Commandes utiles

```bash
# Build
pio run -e s3-n16r8-psram

# Clean + build (pour être sûr que le patch et le framework sont pris en compte)
pio run -t clean -e s3-n16r8-psram && pio run -e s3-n16r8-psram

# Upload (port par défaut ou explicite)
pio run -e s3-n16r8-psram -t upload
pio run -e s3-n16r8-psram -t upload --upload-port COM7

# Moniteur série
pio device monitor -b 115200 -p COM7
```

**Si le port est occupé (COM7 busy)** sous Windows :

```cmd
taskkill /F /IM python.exe
```

Puis relancer l’upload et le moniteur.

---

## 8. Résumé des choix techniques

| Élément | Choix | Raison |
|--------|--------|--------|
| Variant | `qio_opi` | Flash QIO + PSRAM OPI 8 Mo |
| IWDT (config) | Désactivé dans sdkconfig + patch script | Éviter que l’app/framework ne démarre l’IWDT |
| IWDT (runtime) | Désactivation MWDT1 en début de setup() | Le bootloader peut avoir activé l’IWDT ; on le coupe dès l’entrée en app |
| TWDT | Non init au boot, init 300 s en setup() | Contrôle explicite, évite reset pendant init |
| Tests PSRAM | Chunks 4 Ko + vTaskDelay(20) | Limiter le blocage CPU si IWDT était actif |
| Détection PSRAM | heap_caps_get_free_size(MALLOC_CAP_SPIRAM) | fiable même si psramFound()/getPsramSize() = 0 avec sdkconfig custom |

Ce document et les fichiers décrits peuvent être copiés ou adaptés pour tout projet ESP32-S3 avec PSRAM OPI sous PlatformIO/Arduino.
