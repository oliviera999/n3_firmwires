# Référence matériel ESP32-S3 (FFP5CS)

Ce document décrit le **modèle exact** de l’ESP32-S3 utilisé comme référence pour les builds S3 du projet FFP5CS, et le lien avec les environnements PlatformIO.

## Modèle exact (référence)

Caractéristiques identifiées par les logs au boot et par esptool :

| Caractéristique | Valeur |
|-----------------|--------|
| **Chip** | ESP32-S3 (QFN56) |
| **Révision silicium** | v0.2 |
| **Flash** | 16 Mo |
| **PSRAM** | 8 Mo (Embedded, AP_3v3) |
| **CPU** | 2 cœurs, 240 MHz |
| **ROM** | esp32s3-20210327 (Build Mar 27 2021) |

Référence module : type **N16R8** (16 Mo flash + 8 Mo PSRAM).

## Environnements PlatformIO S3

- **Sans PSRAM** (boot stable, défaut) : `wroom-s3-base`, `wroom-s3-test`, `wroom-s3-prod`  
  - 16 Mo flash, PSRAM désactivée dans le sdkconfig.  
  - Utiliser pour la stabilité au boot et les builds de test/prod courants.

- **Avec PSRAM** (matériel N16R8) : `wroom-s3-test-psram`  
  - 16 Mo flash + 8 Mo PSRAM, flag `BOARD_HAS_PSRAM` défini.  
  - Utiliser lorsque l’application doit exploiter la PSRAM (heap étendu, buffers, etc.).

Pour les cartes **8 Mo flash** + PSRAM (DevKit 8 Mo), utiliser l’env `wroom-s3-test-devkit` ; pour le matériel 16 Mo flash + 8 Mo PSRAM (N16R8), utiliser `wroom-s3-test-psram`.
