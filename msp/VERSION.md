# Version msp (MeteoStationPrototype — Station météo)

Version actuelle : **2.41** (définie dans `include/msp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 2.41 | 2026-04 | Luminosité : lecture systématique des 4 photorésistances + moyenne avant la logique servo (mode manuel et seuil de scan), pour éviter l'envoi de zéros au serveur quand le balayage tracker est désactivé |
| 2.40 | 2026-03 | Ajout de traces deep sleep `[SLEEP][TRACE]` (entrée, branche emergency/regular, timer appliqué, cas `WakeUp=1` sans sommeil) pour diagnostiquer l'application des paramètres distants (`106/107`) |
| 2.39 | 2026-03 | Logs GET `outputs_state` : concaténation `String` au lieu de `Serial.printf` multi-`%s` (affichage fiable sur ESP32) ; lecture JSON `hasOwnProperty` avant `operator[]` pour éviter l'injection de clés nulles (Arduino_JSON) |
| 2.38 | 2026-03 | Affichage OTA sur OLED : écran d'état avec version courante/cible et progression (%) pendant le téléchargement OTA (check périodique + OTA avant reset distant) |
| 2.37 | 2026-03 | Ajout des logs de progression OTA en pourcentage (`[OTA][PROGRESS]`) via la lib partagée pour suivre le téléchargement dans le moniteur série |
| 2.36 | 2026-03 | Ajout du mode servo Auto/Manuel piloté par BDD (clé `111`), application immédiate des angles manuels avec clamp firmware (`GD 1-179`, `HB 40-145`) et logs dédiés |
| 2.35 | 2026-03 | Publication OTA MSP après correction de cohérence config distante (reset/wakeup/sleep) |
| 2.34 | 2026-03 | Durcissement de la sync config distante (logs d'application des clés 110/106/107, validation stricte) ; suppression de l'écrasement local `resetMode` au boot ; reconfiguration explicite du timer deep sleep avant sommeil |
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
