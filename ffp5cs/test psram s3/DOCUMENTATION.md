# Documentation complète – ESP32-S3 N16R8 PSRAM OPI (PlatformIO Arduino)

Ce document décrit **tout ce qui a été fait** pour faire booter un ESP32-S3-WROOM-1-N16R8 avec PSRAM OPI activée, la tester et la rendre stable. Il est conçu pour être réutilisé tel quel dans un autre projet (autre répertoire, autre port COM, autre variante de board).

---

## 1. Contexte matériel

| Élément | Valeur |
|--------|--------|
| **Module** | ESP32-S3-WROOM-1-N16R8 |
| **SoC** | ESP32-S3 QFN56, révision v0.2 |
| **Flash** | 16 Mo (N16). Souvent en Octal sur le module ; on utilise **QIO** pour compatibilité boot/outils. |
| **PSRAM** | 8 Mo en mode **Octal (OPI)** – doit être activée explicitement dans la config. |
| **Port série** | COM7 (modifiable dans `platformio.ini`). |

Aucun fichier board JSON externe n’est nécessaire : on utilise la board générique `esp32-s3-devkitc-1` et on surcharge les options pour N16R8.

---

## 2. Structure du projet

```
<racine du projet>/
├── platformio.ini      # Configuration Plateforme / Board / Flash / PSRAM / Port
├── src/
│   └── main.cpp       # Boot, détection PSRAM, test R/W, boucle de stabilité
├── README.md           # Résumé et commandes
└── DOCUMENTATION.md    # Ce fichier
```

À la création du projet : pas de `lib/`, pas de board personnalisée dans `.platformio/`.

---

## 3. Fichier `platformio.ini` – ligne par ligne

Copier le bloc `[env:...]` ci‑dessous et adapter les valeurs indiquées.

```ini
; ESP32-S3-WROOM-1-N16R8 (16MB Flash QIO, 8MB PSRAM OPI)
; Port: COM7

[env:esp32-s3-n16r8]
platform = espressif32@6.4.0
board = esp32-s3-devkitc-1
framework = arduino
platform_packages = framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32.git#2.0.14

upload_port = COM7
monitor_port = COM7
monitor_speed = 115200

; Flash 16 Mo
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv

; PSRAM OPI (Quad Flash + Octal PSRAM)
board_build.arduino.memory_type = qio_opi
board_build.psram_type = opi
board_build.flash_mode = qio
board_build.extra_flags = -DBOARD_HAS_PSRAM

; Stabilité: forcer QIO flash et SPIRAM au niveau SDK
board_build.sdkconfig_options =
  CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
  CONFIG_SPIRAM=y
```

### Explication de chaque option

| Option | Valeur | Rôle |
|--------|--------|------|
| `platform` | `espressif32@6.4.0` | Plateforme PlatformIO. Version fixée pour éviter les régressions. |
| `board` | `esp32-s3-devkitc-1` | Board de base ; les options ci‑dessous la transforment en N16R8. |
| `framework` | `arduino` | Framework Arduino (API `Serial`, `psramFound()`, etc.). |
| `platform_packages` | `framework-arduinoespressif32 @ ...#2.0.14` | **Obligatoire** : avec la plateforme 6.4.0, le framework 3.x provoque une erreur « missing SConscript » (pioarduino-build.py). On impose Arduino-ESP32 **2.0.14** pour que le build passe. |
| `upload_port` | `COM7` | Port série pour le flash. **À adapter** (ex. COM3, /dev/ttyUSB0). |
| `monitor_port` | `COM7` | Port pour le moniteur série. Même valeur que `upload_port` en général. |
| `monitor_speed` | `115200` | Débit du moniteur (et de `Serial.begin(115200)` dans le code). |
| `board_upload.flash_size` | `16MB` | Taille flash pour l’outil de flash (16 Mo). |
| `board_build.partitions` | `default_16MB.csv` | Table de partitions 16 Mo (app0 ~6,5 Mo, OTA, SPIFFS). |
| `board_build.arduino.memory_type` | `qio_opi` | **Critique** : Quad I/O pour la flash, Octal (OPI) pour la PSRAM. Sans cela la PSRAM n’est pas initialisée en OPI. |
| `board_build.psram_type` | `opi` | PSRAM en mode Octal. |
| `board_build.flash_mode` | `qio` | Mode flash Quad I/O. |
| `board_build.extra_flags` | `-DBOARD_HAS_PSRAM` | **Obligatoire** pour que le runtime Arduino/IDF active la PSRAM (`psramFound()`, `heap_caps_*` SPIRAM). |
| `board_build.sdkconfig_options` | `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`, `CONFIG_SPIRAM=y` | Aligne le SDK avec QIO et active SPIRAM (évite incohérences / boot loop). |

### Adapter pour un autre projet

- **Autre port** : remplacer `COM7` par le bon port (Windows : COMx ; Linux/macOS : `/dev/ttyUSB0`, `/dev/ttyACM0`, etc.).
- **Autre nom d’env** : remplacer `esp32-s3-n16r8` par le nom de votre env (ex. `mon_projet`).
- **Garder N16R8** : ne pas modifier les lignes Flash 16 Mo et PSRAM OPI (`qio_opi`, `opi`, `BOARD_HAS_PSRAM`, etc.).

---

## 4. Code `src/main.cpp` – logique et règles importantes

### 4.1 Rôle des parties du code

1. **setup()**
   - `Serial.begin(115200)` + `delay(500)` pour laisser le USB CDC s’établir si besoin.
   - Vérification PSRAM avec `psramFound()`.
   - Affichage de la taille libre et du plus grand bloc avec `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` et `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)`.
   - Un **premier test** : allocation en PSRAM, écriture d’un motif, relecture, vérification, libération (voir `test_psram_block()`).
   - Si échec : message d’erreur et `g_psram_ok = false`.

2. **loop()**
   - Toutes les 5 secondes, rappel du même test PSRAM et affichage `Cycle N: OK` ou `FAIL` (boucle de stabilité).

3. **test_psram_block(size)**
   - `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` pour allouer en PSRAM.
   - Écriture d’un motif alterné (0x55, 0xAA).
   - Lecture et comparaison octet par octet.
   - `heap_caps_free(buf)`.
   - Retourne true si tout est correct, false sinon.

### 4.2 Règle critique : Task Watchdog (TG1WDT)

Sans précaution, une boucle serrée qui remplit/lit beaucoup de PSRAM peut tenir le CPU trop longtemps : le **Task Watchdog** déclenche un reset (`rst:0x8 (TG1WDT_SYS_RST)`), donc boot loop.

**Mesures appliquées dans ce projet :**

1. **Taille du buffer de test** : **4 Ko** (pas 64 Ko) pour le test appelé depuis `setup()` et depuis `loop()`, pour limiter la durée des boucles.
2. **Avant toute utilisation PSRAM dans le test** : un `delay(1)` au début de `test_psram_block()` pour laisser le watchdog être nourri.
3. **Dans les boucles d’écriture et de lecture** : un `delay(1)` **tous les 128 octets** (`(i & 127) == 0 && i`), pour ne jamais bloquer trop longtemps.

Extrait correspondant :

```cpp
static bool test_psram_block(size_t size) {
    delay(1);  /* laisser le WDT se nourrir avant accès PSRAM */
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    // ...
    for (size_t i = 0; i < size; i++) {
        buf[i] = (i % 2 == 0) ? PATTERN_A : PATTERN_B;
        if ((i & 127) == 0 && i) delay(1);
    }
    // ...
    for (size_t i = 0; i < size; i++) {
        if ((i & 127) == 0 && i) delay(1);
        // vérification
    }
    // ...
}
```

**À réutiliser dans un autre projet** : si vous faites des boucles longues (remplissage/lecture de gros buffers en PSRAM) dans une même tâche sans autre `delay`/`yield`, appliquer la même idée : `delay(1)` (ou `yield()` / `esp_task_wdt_reset()` selon le cas) à intervalles réguliers (ex. tous les 128 ou 256 octets).

### 4.3 Dépendances et APIs

- `#include <Arduino.h>` : `Serial`, `delay`, `psramFound()`.
- `#include <esp_heap_caps.h>` : `heap_caps_malloc`, `heap_caps_get_free_size`, `heap_caps_get_largest_free_block`, `heap_caps_free`, `MALLOC_CAP_SPIRAM`.

Aucune lib externe au‑delà du framework Arduino et de la plateforme espressif32.

---

## 5. Commandes PlatformIO

À exécuter à la racine du projet (là où se trouve `platformio.ini`) :

```bash
# Compilation
pio run

# Flash du firmware (utilise upload_port)
pio run --target upload

# Moniteur série (utilise monitor_port et monitor_speed)
pio run --target monitor
```

**Important** : fermer le moniteur (Ctrl+C) avant un nouvel upload, sinon le port peut rester occupé et l’upload échouer (« Could not open COMx »).

---

## 6. Dépannage

### 6.1 « Could not open COM7 » (ou autre port)

- Un autre programme tient le port (moniteur PlatformIO, Arduino IDE, autre terminal série).
- **À faire** : fermer tout ce qui utilise ce port, puis relancer l’upload.
- Sous Windows, pour libérer le port utilisé par le moniteur PlatformIO : identifier le processus (ex. `pio device monitor`) et l’arrêter (Gestionnaire des tâches ou `taskkill`), puis relancer l’upload.

### 6.2 Boot loop avec `rst:0x8 (TG1WDT_SYS_RST)`

- Le Task Watchdog reset la carte parce qu’une tâche garde le CPU trop longtemps sans nourrir le watchdog.
- **À faire** : dans les boucles longues (surtout accès PSRAM serrés), ajouter `delay(1)` (ou `yield()` / `esp_task_wdt_reset()`) à intervalles réguliers (voir section 4.2). Réduire la taille des buffers testés en une seule fois peut aussi suffire (ex. 4 Ko au lieu de 64 Ko).

### 6.3 « PSRAM non detectee » ou `psramFound()` faux

- Vérifier dans `platformio.ini` :
  - `board_build.arduino.memory_type = qio_opi`
  - `board_build.psram_type = opi`
  - `board_build.extra_flags = -DBOARD_HAS_PSRAM`
- Vérifier que la board est bien un module avec PSRAM OPI (ex. N16R8, N8R8). Sans PSRAM matérielle, ces options ne la feront pas apparaître.

### 6.4 Boot loop sans message série (ou flash qui ne démarre pas)

- Vérifier que `memory_type` est bien `qio_opi` (Quad flash + OPI PSRAM), pas `opi_opi` si la flash est Quad.
- Vérifier que `board_build.flash_mode = qio` et les `sdkconfig_options` (QIO + SPIRAM) sont présents.

### 6.5 Erreur de build « missing SConscript » / pioarduino-build.py

- Utiliser **obligatoirement** une version 2.0.x du framework Arduino-ESP32 avec la plateforme espressif32 6.4.0 (comme dans ce projet : `platform_packages = framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32.git#2.0.14`). Ne pas utiliser le framework 3.x avec la plateforme espressif32 standard sans adaptation (pioarduino, etc.).

---

## 7. Table de partitions et gros firmware

- Table utilisée : **default_16MB.csv** (celle fournie par Arduino-ESP32 pour 16 Mo).
- Partition app (app0 / app1) : **~6,5 Mo** chacune (0x640000).
- Tant que la taille du firmware reste **inférieure à ~6 Mo**, la configuration est adaptée à un gros firmware. En cas de dépassement (erreur de lien / section overflow), il faudra une table de partitions personnalisée (partition app plus grande, par exemple en réduisant SPIFFS).

La configuration décrite ici (flash 16 Mo, PSRAM 8 Mo OPI, partitions 16 Mo) est **sans risque** pour un gros firmware tant que la taille du binaire reste dans la partition app.

---

## 8. Réutilisation dans un autre projet – checklist

1. Créer un nouveau répertoire de projet et s’y placer.
2. Créer **platformio.ini** en copiant le bloc `[env:...]` de la section 3 ; modifier au minimum :
   - `upload_port` et `monitor_port` (ex. COM3, /dev/ttyUSB0).
   - Le nom de l’env si besoin (`[env:mon_projet]`).
3. Créer **src/main.cpp** en copiant le code de ce projet (ou en gardant la même logique : `psramFound()`, `heap_caps_*`, test avec `delay(1)` tous les 128 octets et buffer 4 Ko).
4. Lancer `pio run`, puis `pio run --target upload`, puis `pio run --target monitor` pour vérifier boot, détection PSRAM, test OK et cycles stables.
5. Pour un firmware applicatif (pas seulement test PSRAM) : reprendre la **même** configuration `platformio.ini` (N16R8, qio_opi, 16MB, BOARD_HAS_PSRAM, etc.) et remplacer le contenu de `main.cpp` par votre logique ; dans les boucles longues en PSRAM, conserver l’idée des `delay(1)` (ou équivalent) pour éviter TG1WDT.

---

## 9. Résumé des fichiers à copier

| Fichier | Contenu à reprendre |
|--------|----------------------|
| **platformio.ini** | Tout le bloc `[env:...]` de la section 3 ; adapter port et nom d’env. |
| **src/main.cpp** | Code complet du projet actuel (boot, psramFound, heap_caps, test_psram_block avec delay(1) tous les 128 octets, buffer 4 Ko, boucle 5 s). Pour un autre projet applicatif : garder platformio.ini, adapter main.cpp en gardant les règles watchdog si vous faites des boucles longues en PSRAM. |

Avec cela, la configuration est documentée de façon précise et réutilisable dans un autre projet.
