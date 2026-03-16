# Procédure : analyser un PANIC après crash (ESP32-S3 wroom-s3-test)

> **Archivé (2026-03)** : Le core dump est désactivé pour tous les environnements FFP5CS (source de problèmes au boot). Cette procédure reste documentée au cas où vous réactiveriez manuellement une partition coredump pour un debug ponctuel.

Après un reset **PANIC** sur l’environnement **wroom-s3-test** (ESP32-S3 avec partition coredump), on peut extraire et analyser le coredump pour obtenir la tâche en cause et la backtrace.

## Prérequis

- Environnement **wroom-s3-test** (partition `partitions_esp32_s3_test_coredump.csv`, coredump 64 KB à l’offset `0xE00000`).
- Build avec `FFP5CS_COREDUMP_ENABLE=1` (déjà activé pour wroom-s3-test dans `platformio.ini`).
- Au boot suivant le crash, les infos panic sont capturées et sauvegardées en NVS via `Diagnostics::capturePanicInfo()` et `savePanicInfo()` dans `diagnostics.cpp`.

## 1. Extraire le coredump depuis la flash

Depuis la racine du projet, avec l’ESP32 connecté sur le port série (ex. COM7) :

```powershell
# ESP32-S3 wroom-s3-test : partition coredump à 0xE00000, taille 0x10000 (64 KB)
python tools/coredump/extract_coredump.py --port COM7 --offset 0xE00000 --size 0x10000 --output coredump_s3.elf
```

Adapter `--port` (COM7, COM4, etc.) et éventuellement `--output`.

## 2. Analyser le fichier extrait

```powershell
# Avec le firmware ELF du build wroom-s3-test (pour les symboles)
python tools/coredump/analyze_coredump.py coredump_s3.elf --elf .pio/build/wroom-s3-test/firmware.elf --output rapport_coredump.txt
```

Ou en une seule commande (extraction + analyse) avec l’outil intégré, en passant l’offset S3 :

```powershell
python tools/coredump/coredump_tool.py --port COM7 --extract-only
# Puis éditer le script ou utiliser extract_coredump.py avec -o 0xE00000 pour S3, puis :
python tools/coredump/analyze_coredump.py <fichier_extraite>.elf --elf .pio/build/wroom-s3-test/firmware.elf
```

## 3. Interpréter le résultat

- Le rapport (ou la sortie `esp_coredump info_corefile`) indique la **tâche** et la **backtrace** au moment du crash.
- Croiser avec le code source (fichier/fonction) pour identifier la cause (nullptr, stack overflow, race, etc.).

## Références

- Partition (si réactivée) : coredump à `0xE00000`, taille `0x10000` (fichier supprimé, recréer si besoin).
- Outils : `tools/coredump/README.md`, `tools/coredump/extract_coredump.py`, `tools/coredump/analyze_coredump.py`.
- Diagnostic panic : `src/diagnostics.cpp` (`capturePanicInfo`, `savePanicInfo`, `getRebootReason`).
