# 🛠️ Outils Python pour FFP3CS4

## 📂 Structure

### `/flash` - Outils de programmation
- **`flash_esp32.py`** : Flash manuel de l'ESP32 via USB
- **`deploy_ota.py`** : Déploiement OTA sur le réseau

### `/monitor` - Outils de monitoring
- **`serial_capture.py`** : Capture des logs série en temps réel
- **`monitor_ota.py`** : Surveillance des mises à jour OTA
- **`diagnostic_analyzer.py`** : Analyse des diagnostics système
- **`capture_boot.py`** : Capture des logs de démarrage

### `/recovery` - Outils de récupération
- **`emergency_recovery.py`** : Récupération d'urgence complète
- **`reset_flash.py`** : Effacement complet de la flash
- **`quick_reset.py`** : Reset rapide de l'ESP32

## 📖 Utilisation

### Flash de l'ESP32
```bash
python tools/flash/flash_esp32.py --port COM3
```

### Capture des logs
```bash
python tools/monitor/serial_capture.py --port COM3 --output logs.txt
```

### Reset d'urgence
```bash
python tools/recovery/emergency_recovery.py --port COM3
```

## 📝 Notes
- Tous les scripts nécessitent `esptool` et `pyserial`
- Installation : `pip install esptool pyserial`
- Port par défaut : COM3 (Windows) ou /dev/ttyUSB0 (Linux)
