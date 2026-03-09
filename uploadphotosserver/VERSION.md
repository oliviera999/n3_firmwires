# Upload Photos Server (ESP32-CAM unifie) — Historique des versions

## v2.8 (2026-03)
- Ajout authentification par header X-Api-Key sur les uploads photo
- Verrouillage des versions lib_deps dans platformio.ini

## v2.7 (2026-03)
- Stabilisation upload photo et OTA distant pour les 3 cibles

## v2.2 (2026-02)
- Ajout OTA distant HTTP pour msp1 ; credentials externalises

## v2.0 (2026-01)
- Firmware unifie : 3 cibles (msp1, n3pp, ffp3) dans un seul code source
- OTA distant HTTP pour toutes les cibles
- Credentials externalises dans credentials.h
