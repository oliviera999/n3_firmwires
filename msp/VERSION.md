# Version msp (MeteoStationPrototype — Station météo)

Version actuelle : **2.33** (définie dans `include/msp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 2.33 | 2026-03 | Ajout de logs détaillés des échanges serveur : affichage du payload POST (masquage `api_key`) et du body GET (`outputs_state`) dans le moniteur série |
| 2.32 | 2026-03 | Logs série harmonisés par thème (`[WIFI]`, `[SERVER]`, `[DHT]`, `[SERVO]`) ; réduction de verbosité du scan `position max` avec résumé final ; mise en avant des échanges serveur GET/POST |
| 2.31 | 2026-03 | OTA périodique: ajout d'une vérification OTA toutes les 2h (cumul RTC du deep sleep), maintien du check OTA prioritaire sur reset distant |
| 2.30 | 2026-03 | resetMode distant: detection front montant avec seed au 1er poll, tentative OTA prioritaire (`n3OtaCheck`) avant reset, fallback `ESP.restart()` si aucune MAJ |
| 2.29 | 2026-03 | Compatibilité build OTA : `gcc_atomic_compat.c` passe en symboles faibles (`weak`) pour éviter le conflit de linkage avec les définitions `libnewlib` selon toolchain/framework |
| 2.28 | 2026-03 | Correction de la mesure de luminosité du tracker solaire : remise à zéro des accumulateurs à chaque scan et incrément circulaire de l’index de moyenne glissante (suppression de la dérive linéaire des valeurs) |
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
