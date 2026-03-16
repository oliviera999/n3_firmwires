# Dépannage carte SD sur ESP32-S3 (WROOM-S3)

## Symptômes observés (logs)

```
[E][sd_diskio.cpp:199] sdCommand(): Card Failed! cmd: 0x3a
[E][sd_diskio.cpp:806] sdcard_mount(): f_mount failed: (3) The physical drive cannot work
[SD] Carte non montée
[BOOT] SD carte absente ou indisponible
```

- **cmd 0x3a** = CMD58 (Read OCR) — échec lors de la phase d’init SPI
- **f_mount (3)** = FR_NOT_READY — le périphérique physique ne répond pas

## Causes possibles et solutions

### 1. Fréquence SPI trop élevée au démarrage

La doc Espressif recommande une fréquence de **400 kHz** pendant la phase de probing (init) pour éviter les problèmes de timing et de charge capacitive.

**Action** : réduction de la fréquence init à 400 kHz dans `sd_card.cpp` (au lieu de 4 MHz).

### 2. Absence de pull-ups (ESP32-S3-DevKitC-1)

L’ESP32-S3-DevKitC-1 **n’a pas de pull-ups intégrés** pour la carte SD. En mode SPI, les lignes CMD et DATA (MISO = DAT0) doivent être reliées à VDD par des résistances **10 kΩ**.

**Action** :
- Ajouter une résistance 10 kΩ entre **MISO (GPIO 14)** et 3,3 V
- Optionnel mais conseillé : 10 kΩ entre **MOSI (GPIO 11)** et 3,3 V

Réf. : [ESP-IDF SD Pull-up Requirements](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/sd_pullup_requirements.html)

### 3. Broches utilisées

Configuration actuelle (`pins.h`, BOARD_S3) :
- **CS** = GPIO 10  
- **MOSI** = GPIO 11  
- **CLK** = GPIO 12  
- **MISO** = GPIO 14  

À éviter : GPIO 0, 45, 46 (strapping), 26–37 (flash/PSRAM), 43–44 (UART0), 19–20 (USB).  
Les broches 10, 11, 12, 14 sont dans la plage recommandée pour un usage SPI général.

### 4. Vérifications matérielles

- **Câblage** : Vérifier CS, MOSI, MISO, CLK, VCC (3,3 V), GND
- **Carte SD** : Tester une autre carte, formatée en FAT32
- **Alimentation** : Certaines cartes consomment plus au démarrage ; vérifier la stabilité du 3,3 V
- **Délai après mise sous tension** : Délai de 100 ms avant l’init (ajouté dans le code)

### 5. Ordre des paramètres `SPI.begin()`

Pour ESP32 : `SPI.begin(SCK, MISO, MOSI, SS)`  
Configuration actuelle : `SPI.begin(12, 14, 11, 10)` — ordre correct.

## Modifications du firmware

1. **Fréquence** : 4 MHz → 400 kHz (ou 1 MHz en compromis)
2. **Délai** : `delay(100)` avant `SD.begin()` pour laisser la carte s’initialiser
3. **Pull-ups** : documenter dans la doc matériel et dans ce fichier

## Références

- [arduino-esp32 #7707](https://github.com/espressif/arduino-esp32/issues/7707) — SD cards not mounting
- [arduino-esp32 #9274](https://github.com/espressif/arduino-esp32/issues/9274) — ESP32-S3 SDMMC
- [SdFat #473](https://github.com/greiman/SdFat/issues/473) — ESP32 S3 mount failures
- [ESP-IDF SD Pull-up](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/sd_pullup_requirements.html)
