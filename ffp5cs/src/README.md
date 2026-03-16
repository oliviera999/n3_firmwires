# Dossier `src/` – Implémentations C++ du firmware

Ce dossier contient **l’implémentation concrète** de toutes les classes déclarées dans `include/`.  
Chaque fichier est volontairement court (max ~400 lignes) pour faciliter la maintenance.

| Fichier | Correspondance header | Brève description |
|---------|----------------------|-------------------|
| `app.cpp` | – (point d’entrée) | Initialise le système, crée les tâches FreeRTOS, gère OTA. |
| `wifi_manager.cpp` | `wifi_manager.h` | Tentatives multi-SSID, gestion RSSI. |
| `sensors.cpp` | `sensors.h` | Abstractions bas-niveau capteurs (OneWire, DHT, ultrasons, ADC). |
| `system_sensors.cpp` | `system_sensors.h` | Lecture groupée et filtrage des mesures. |
| `actuators.cpp` | `actuators.h` | Implémentation relais & servos (inclut timers non bloquants). |
| `system_actuators.cpp` | `system_actuators.h` | Méthodes utilitaires (start/stop) + séquences complex. |
| `display_view.cpp` | `display_view.h` | Rendu texte & graphiques sur OLED (Adafruit GFX). |
| `power.cpp` | `power.h` | NTP, sauvegarde temps en flash, watchdog, sleep. |
| `web_client.cpp` | `web_client.h` | Envoi POST multipart/form-urlencoded, analyse JSON distant. |
| `web_server.cpp` | `web_server.h` | Serveur AsyncWebServer + page OTA `/update` + API JSON `/api/*`. |
| `mailer.cpp` | `mailer.h` | Envoi SMTP SSL (Gmail) + file d’attente simple. |
| `config.cpp` | `config.h` | Accès Preferences (NVS) pour persister seuils & flags. |
| `diagnostics.cpp` | `diagnostics.h` | Lecture VIN (ADC), analyse raison reset, uptime, Brownout. |
| `automatism.cpp` | `automatism.h` | **Cœur métier** : gestion nourrissage, pompage, chauffage, alertes, veille. |
| `task_monitor.cpp` | `task_monitor.h` | Monitoring léger des piles des tâches FreeRTOS. |

---

## Cycle d’exécution
1. `setup()` (dans `app.cpp`) configure le watchdog, le Wi-Fi, l’OLED, les capteurs et actionneurs puis lance les tâches :
   * **sensorTask** (core 1, priorité 3) – Lecture capteurs → Queue FreeRTOS
   * **automationTask** (core 1, priorité 2) – Consomme la queue, exécute la logique `Automatism`, heartbeat/diag
   * **displayTask** (core 1, priorité 1) – Mise à jour OLED (~250 ms)
   * **webTask** (core 0, priorité 2) - Gestion serveur HTTP
2. La boucle `loop()` (core 1) gère le Wi‑Fi et fait un `vTaskDelay(200 ms)`.

---

## Compilation conditionnelle
Certains fichiers utilisent `#ifdef UNIT_TEST` pour s’adapter aux tests sur host.  
Le flag est ajouté dans l’environnement PlatformIO `[env:test-esp32]` (commenté par défaut).

---

> Consultez également le README racine pour le brochage et les instructions de build.
