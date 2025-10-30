# Refactorisation FFP5CS – Baseline 2025-10-29

## Synthèse compilation

- Commande: `pio run -t size` (env `wroom-test`).
- Résultat `firmware.elf`: text 1 812 007 bytes, data 452 844 bytes, bss 47 305 bytes, total 2 312 156 bytes.
- Build après extraction helpers affichage (`v11.89`): text 1 812 115 bytes, data 452 844 bytes, bss 47 305 bytes, total 2 312 264 bytes.
- Build refactor renderers (`v11.90`): text 1 811 539 bytes, data 452 844 bytes, bss 47 305 bytes, total 2 311 688 bytes.
- Plateforme: ESP32 Dev Module @ 240 MHz, framework Arduino 3.3.0.

## Métriques runtime (log série v11.81)

- Source: `monitor_90s_v11.81_post_flash_2025-10-20_20-44-15.log`.
- Heap libre au démarrage: 131 356 bytes.
- Heap minimum observé: 52 732 bytes.
- Version firmware: 11.81 (profil TEST).
- Taille sketch reportée par le firmware: 2 225 904 bytes (LittleFS plein à 0 byte libre).
- HTTP POST initial: succès 200 en ~656 ms, heap finale 133 340 bytes.

## Points de monitoring récurrents

- Exécuter un monitoring de 90 s (`monitor_90s_*`) après chaque flash, archiver le log et analyser: Guru Meditation, panic, watchdog, heap min, erreurs WiFi/WebSocket.
- Relever systématiquement la heap libre au démarrage et la heap min historique dans les logs.
- Contrôler la taille du firmware (`pio run -t size`) et la place restante SPIFFS (`mklittlefs`).
- Vérifier le temps de réponse et le taux de succès des requêtes HTTP/POST.
- Suivre les alertes watchdog (`esp_task_wdt`), les snapshots `TaskMonitor` et les timers critiques.

