# ESP32-S3 N16R8 – Boot et test PSRAM OPI

Projet PlatformIO (Arduino) pour **ESP32-S3-WROOM-1-N16R8** : 16 Mo Flash (QIO), 8 Mo PSRAM (OPI).  
Objectif : faire booter l’ESP avec la PSRAM activée, vérifier qu’elle fonctionne et qu’elle reste stable.

**Documentation complète pour réutilisation dans un autre projet : [DOCUMENTATION.md](DOCUMENTATION.md).**

---

## Prérequis

- ESP32-S3 branché sur **COM7** (ou modifier `upload_port` / `monitor_port` dans `platformio.ini`).
- Si l’upload échoue (« Could not open COM7 ») : fermer tout logiciel qui utilise le port (moniteur série, Arduino IDE, etc.), puis réessayer. En dernier recours : maintenir **BOOT**, appuyer sur **RST**, relâcher **BOOT**.

## Commandes

```bash
pio run
pio run --target upload
pio run --target monitor
```

## Comportement attendu

1. **Boot** : « ESP32-S3 N16R8 - Boot PSRAM OPI ».
2. **PSRAM** : « PSRAM detectee » avec taille libre (~8 Mo) et plus grand bloc.
3. **Test** : « [OK] Premier test PSRAM reussi » (alloc 4 Ko, write/read 0x55/0xAA, free).
4. **Stabilité** : toutes les 5 s, « Cycle N: OK ».

En cas de problème (PSRAM non détectée, boot loop, port bloqué), voir la section **Dépannage** de [DOCUMENTATION.md](DOCUMENTATION.md).
