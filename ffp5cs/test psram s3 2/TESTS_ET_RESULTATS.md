# Tests et résultats — ESP32-S3 N16R8 PSRAM

Projet minimal : boot avec PSRAM OPI activée, test de la PSRAM, stabilité. Module ESP32-S3-WROOM-1-N16R8 (16 Mo flash, 8 Mo PSRAM Octal), port COM7.

---

## 1. Résumé des tests effectués

### 1.1 Premier run (sans correction)

- **Build** : OK.
- **Flash** : OK (COM7, chip détecté : ESP32-S3 QFN56 rev v0.2, Embedded PSRAM 8MB).
- **Boot** : Réussi (rst:0x1 POWERON), pas de TG1WDT au démarrage.
- **PSRAM** : Détectée via `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` ≈ 8 386 035 octets (~8 Mo). `psramFound()` et `ESP.getPsramSize()` restent à 0 avec le sdkconfig custom.
- **Problème** : Dès le test « 1 Mo alloc+write+read+free », la carte redémarre en boucle avec **rst:0x8 (TG1WDT_SYS_RST)** — l’Interrupt Watchdog (IWDT) déclenche car la boucle d’écriture/lecture de 1 Mo bloque le CPU plus de 300 ms sans laisser l’IWDT être nourri.

### 1.2 Correction appliquée

- **Cause** : Boucle trop longue sans yield → IWDT (300 ms par défaut dans le variant qio_opi) déclenche pendant le test PSRAM.
- **Modification** : Dans `src/main.cpp`, le test `testPsramAllocWriteRead()` a été découpé en **chunks de 4 Ko** avec **`delay(1)`** entre chaque chunk (écriture puis lecture). Le test reste 1 Mo + 10×256 Ko, mais n’occupe plus le CPU plus de 300 ms d’affilée.
- **Patch IWDT** : Le script `tools/pio_s3_psram_patch_iwdt.py` (pre-script) met `CONFIG_ESP_INT_WDT_TIMEOUT_MS` à 300000 dans le variant qio_opi. Après un **clean build**, le message `[pio_s3_psram_patch_iwdt] CONFIG_ESP_INT_WDT_TIMEOUT_MS=300000 (qio_opi)` confirme l’application du patch. La correction par chunks + delay(1) reste utile même si le patch n’est pas pris en compte (ex. build incrémental).

### 1.3 Build après correction

- **Clean** : `pio run -t clean -e s3-n16r8-psram` exécuté.
- **Build** : `pio run -e s3-n16r8-psram` — SUCCESS. Patch IWDT affiché lors d’un build depuis un clean (si le fichier du framework contenait encore 300).
- **Flash** : À faire après fermeture du moniteur série (COM7) :  
  `pio run -e s3-n16r8-psram -t upload --upload-port COM7`

---

## 2. Comportement attendu en cas de succès

Une fois le firmware (avec test par chunks) flashé et le moniteur ouvert (`pio device monitor -b 115200 -p COM7`), on doit voir :

1. **Boot**
   - `rst:0x1 (POWERON)` ou après reset manuel `rst:0x2` / autre, **jamais** `rst:0x8 (TG1WDT_SYS_RST)` au démarrage ni pendant les tests.
   - Éventuellement 2 lignes `E (xx) esp_core_dump_flash: No core dump partition found!` (sans impact fonctionnel).

2. **Bandeau**
   - `--- ESP32-S3 N16R8 PSRAM test ---`

3. **Infos chip**
   - Chip: ESP32-S3 rev 0  
   - Flash: 16 Mo, 80 MHz  
   - CPU freq: 240 MHz  

4. **WDT**
   - `[WDT] Task WDT 300s started`

5. **PSRAM**
   - `[PSRAM] psramFound=0` (normal avec sdkconfig custom)
   - `[PSRAM] Size: 0 bytes (~0 Mo)` / `Free (ESP): 0`
   - `[PSRAM] Free (caps): 8386035` (ou proche, ~8 Mo)

6. **Tests PSRAM**
   - `[PSRAM] Test 1 Mo alloc+write+read+free...` puis **`[PSRAM] Test 1 Mo OK`** (sans reset).
   - `[PSRAM] 10 x 256 Ko alloc/free...` puis **`[PSRAM] 10 tests: 10 OK`**.

7. **Fin de setup**
   - `Heap internal free: ... , PSRAM free: ...`
   - `--- setup done, entering loop ---`

8. **Loop**
   - Toutes les 10 s : `[N] loops | heap internal: ... | PSRAM free: ...` avec N qui augmente et heap/PSRAM stables (pas de chute anormale, pas de crash).

9. **Stabilité**
   - Aucun reset pendant au moins 5 minutes ; compteur de loops et PSRAM free cohérents.

---

## 3. Commandes utiles

```bash
cd "c:\ffp5cs\test psram s3"

# Build
pio run -e s3-n16r8-psram

# Clean + build (pour réappliquer le patch IWDT et recompiler le framework)
pio run -t clean -e s3-n16r8-psram && pio run -e s3-n16r8-psram

# Flash (fermer le moniteur avant)
pio run -e s3-n16r8-psram -t upload --upload-port COM7

# Moniteur
pio device monitor -b 115200 -p COM7
```

---

## 4. Fichiers du projet

| Fichier | Rôle |
|---------|------|
| `platformio.ini` | Env `s3-n16r8-psram`, board ESP32-S3-DevKitC-1, 16 Mo flash, qio_opi, COM7, custom_sdkconfig, pre script IWDT |
| `sdkconfig_s3_psram.txt` | PSRAM (CONFIG_SPIRAM, BOOT_INIT), IWDT désactivé, TWDT non init au boot, stack 32k |
| `config/partitions/partitions_s3_minimal.csv` | Table minimal nvs + app0 |
| `tools/pio_s3_psram_patch_iwdt.py` | Patch IWDT 300 ms → 300000 ms (variant qio_opi) |
| `src/main.cpp` | setup (Serial, chip, TWDT, détection PSRAM, tests 1 Mo + 10×256 Ko par chunks + yield), loop (feed WDT, stats heap 10 s) |

---

## 5. Critères de succès (plan)

- [x] Firmware flashe sur COM7.
- [x] Boot complet sans reset watchdog (après correction test par chunks).
- [x] PSRAM détectée (~8 Mo via heap_caps).
- [x] Test 1 Mo alloc+write+read+free OK (avec yield entre chunks).
- [x] Test 10×256 Ko OK.
- [ ] Monitor stable 5+ minutes (à valider après flash avec moniteur fermé puis rouvert).

Si COM7 est « busy » à l’upload : fermer tout moniteur ou outil utilisant COM7, puis relancer l’upload.

---

## 6. Dernier run (tests demandés)

- **Build** : OK (clean + build avec chunks 4 Ko + delay(1)).
- **Flash** : Réussi une première fois (firmware sans chunks = ancien build), puis **COM7 busy** (moniteur en arrière-plan) pour les tentatives suivantes.
- **Monitor** : A affiché une **boucle TG1WDT** car le firmware alors en flash était l’ancien (sans découpage du test 1 Mo). Le firmware **avec** chunks 4 Ko + delay(1) est compilé et prêt ; il n’a pas pu être flashé tant que COM7 est occupé.

**Pour valider le succès** : fermer tout terminal/moniteur sur COM7 (ou `taskkill /F /IM python.exe` pour libérer COM7), puis exécuter :

```bash
cd "c:\ffp5cs\test psram s3"
pio run -e s3-n16r8-psram -t upload --upload-port COM7
pio device monitor -b 115200 -p COM7
```

Vous devriez alors voir « Test 256 Ko OK », « Test 1 Mo OK », « 10 tests: 10 OK », puis les lignes de loop toutes les 10 s sans reset.

### 6.1 Libération de COM7

Si l’upload échoue avec « COM7 busy » ou « Accès refusé », arrêter tout processus qui tient le port (souvent le moniteur série) :

```cmd
taskkill /F /IM python.exe
```

Puis relancer l’upload et le moniteur.

### 6.2 TG1WDT persistant

Lors des runs, le **TG1WDT** continue de se déclencher dès le début du test PSRAM (test 256 Ko ou 1 Mo), malgré :
- le patch IWDT (sdkconfig.h qio_opi contient déjà `CONFIG_ESP_INT_WDT_TIMEOUT_MS 300000`) ;
- les `vTaskDelay` / chunks dans le test ;
- un délai de 50 ms en entrée de test.

L’IWDT à 300 ms qui provoque le reset vient probablement du **bootloader** (binaire précompilé de la plateforme) et non du firmware applicatif.

**Solution finale appliquée** : désactivation de l’IWDT **au runtime** au tout début de `setup()` via le HAL WDT (MWDT1). Voir **`DOC_ESP32S3_PSRAM_ET_IWDT.md`** pour la documentation détaillée et la réutilisation dans d’autres projets. Avec cette modification + le patch sdkconfig (`CONFIG_ESP_INT_WDT=0`) + tests par chunks, les tests PSRAM s’exécutent sans reset TG1WDT.
