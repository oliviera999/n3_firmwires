# Version msp (MeteoStationPrototype — Station météo)

Version actuelle : **2.27** (définie dans `include/msp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 2.27 | 2026-03 | Test OTA : incrément version et publication OTA msp pour validation de mise à jour distante |
| 2.26 | 2026-03 | Correction crash `LoadProhibited` au parsing JSON des outputs (cast sécurisé, fallback types number/string/null) |
| 2.20 | 2026-03 | Réduction des délais bloquants OLED/batterie pour améliorer la réactivité du cycle |
| 2.19 | 2026-03 | Fallback DHT harmonisé (20°C / 50%) sur lectures invalides |
| 2.18 | 2026-03 | Parsing outputs robuste (GPIO explicite + fallback legacy) et timeout HTTP 5s via lib partagée |
| 2.15 | 2026-03 | Renommage projet msp2_5 → msp |
| 2.14 | 2026-03 | Audit échanges firmware-serveur (incrément cohérence) |
| 2.13 | 2026-03 | Migration vers libn3_iot (drivers capteurs génériques, DS18B20) |
| 2.11 | — | Version actuelle (inventaire appareils) |

---

## Références

- Inventaire : `docs/inventaire_appareils.md`
