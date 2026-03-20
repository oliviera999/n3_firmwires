# Version n3pp (N3PhasmesProto — Serre / aquaponie)

Version actuelle : **4.20** (définie dans `include/n3pp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 4.20 | 2026-03 | Réduction des délais bloquants (OLED, lecture DHT, attente après fetch outputs) |
| 4.19 | 2026-03 | Fallback DHT harmonisé (20°C / 50%) et correction condition mail batterie (`checked`) |
| 4.18 | 2026-03 | Parsing outputs robuste (GPIO explicite + fallback legacy) et timeout HTTP 5s via lib partagée |
| 4.15 | 2026-03 | Renommage projet n3pp4_2 → n3pp |
| 4.14 | 2026-03 | Incrémentation pour OTA (audit échanges) |
| 4.13 | 2026-03 | Migration vers libn3_iot (drivers capteurs génériques) |
| 4.12 | 2026-03 | Fallback DHT harmonise (20°C / 50 % si isnan) |
| 4.11 | — | Version actuelle (inventaire appareils) |

---

## Références

- Configuration : `include/n3pp_config.h` → `FIRMWARE_VERSION`
- Inventaire : `docs/inventaire_appareils.md`
