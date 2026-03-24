# Version uploadphotosserver (ESP32-CAM unifié)

Version actuelle : **2.26** (définie dans `include/config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 2.26 | 2026-03-24 | Ajout de logs explicites pour le contrôle distant : URL/HTTP/body du GET `outputs_state` et payload POST version (api key masquée) |
| 2.25 | 2026-03-24 | Catégorisation des logs série (`[SERVER]`, `[CAM]`, `[SD]`, `[WIFI]`, `[CAPTURE]`, `[SLEEP]`) ; mise en avant des échanges HTTP serveur (connexion, POST upload, réponse et body) |
| 2.24 | 2026-03-24 | OTA périodique: remplacement du check OTA à chaque réveil par une vérification toutes les 2h (cumul RTC du deep sleep) |
| 2.22 | 2026-03-23 | Deep sleep temporaire réduit à 15 s + ajout de points de monitoring (boot, WiFi, SD, caméra, NTP, upload HTTP, sleep) |
| 2.10 | 2026-03-12 | Audit échanges firmware-serveur (incrément cohérence) |
| 2.9 | 2026-03-10 | TIME_TO_SLEEP 3→600 s ; HTTP_RESPONSE_TIMEOUT_MS 15 s (dérogation conventions) |
| 2.8 | — | Version précédente |

---

## Références

- Configuration : `include/config.h` → `FIRMWARE_VERSION`
- Inventaire : `docs/inventaire_appareils.md`
