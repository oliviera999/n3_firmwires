# Version n3pp (N3PhasmesProto — Serre / aquaponie)

Version actuelle : **4.28** (définie dans `include/n3pp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 4.28 | 2026-03 | Test OTA monitoré : incrément version et validation de la mise à jour distante sur COM4 |
| 4.27 | 2026-03 | Correction crash `LoadProhibited` sur parsing JSON outputs (cast sécurisé, fallback types number/string/null) |
| 4.26 | 2026-03 | Test OTA n3pp : incrément version et validation de la mise à jour distante |
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
