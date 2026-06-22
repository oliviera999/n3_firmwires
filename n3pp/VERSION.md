# Version n3pp (N3PhasmesProto — Serre / aquaponie)

Version actuelle : **4.43** (définie dans `include/n3pp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 4.43 | 2026-06 | Publication OTA : incrément version pour déploiement distant |
| 4.42 | 2026-06 | Publication OTA : incrément version pour déploiement distant |
| 4.41 | 2026-06 | Rapport mail reseau periodique (6 h) : stats POST/GET via `n3_data` 1.2 + `n3MailBuildNetReportBody` ; comparaison explicite avec logs ffp5cs |
| 4.40 | 2026-06 | Lib partagée `n3_data` 1.1.0 : log `[SERVER][POST] Verdict` avec `duree_totale` (ms), RSSI et alerte si proche du timeout 5 s (diagnostic latence POST, aligné ffp5cs) |
| 4.40 | 2026-06 | Audit optimisation : migration SMTP → `n3_mail` et RTC → `n3_time` (≈120 lignes dédupliquées avec msp) ; retrait du global `SMTPSession smtp` inutilisé (−392 o RAM statique) ; `n3_http`/`libn3_iot` supprimées (code mort) |
| 4.39 | 2026-05 | Build : `n3pp_globals.cpp`, fallback `API_SIG_SECRET`, constantes OLED `N3_OLED_*` dans `n3_defaults.h` |
| 4.38 | 2026-05 | Phase 1 audit : extraction de `n3pp_globals.cpp` (main.cpp passe sous 300 lignes), correction `String emailMessage` locale qui masquait la globale (alertes batterie/sécheresse/arrosage envoyaient un message vide), retrait de `server.begin()` du `loop()` (aucune route enregistrée), bloc périodique `intervalDatas` repositionné AVANT `sommeil()` (était mort en deep sleep), cooldown 5 min sur l'arrosage auto pour éviter la pompe en boucle quand le sol reste sec, clamp `tempsArrosageSec ≤ 20 s`, suppression de la double mesure `PontDiv` dans `batterie()` (analogRead brut écrasait la valeur filtrée), `FreqWakeUp` par défaut aligné sur `N3_DEFAULT_FREQ_WAKE_UP_S = 300 s`, suppression du prototype mort `httpGETRequest()` et du bloc commenté touchpad, `configTime` appelé 1× par réveil au lieu de chaque `loop()` |
| 4.37 | 2026-03 | Durcissement logs/parsing config distante (`outputs_state`) : concaténation `String` pour éviter les artefacts `printf` multi-`%s` ; vérification `hasOwnProperty` avant accès JSON ; ajout de traces deep sleep `[SLEEP][TRACE]` (entrée, branche, timer appliqué, skip `WakeUp=1`) |
| 4.36 | 2026-03 | Affichage OTA sur OLED : écran d'état avec version courante/cible et progression (%) pendant le téléchargement OTA (check périodique + OTA avant reset distant) |
| 4.35 | 2026-03 | Ajout des logs de progression OTA en pourcentage (`[OTA][PROGRESS]`) via la lib partagée pour suivre l'avancement du téléchargement dans le moniteur série |
| 4.34 | 2026-03 | Durcissement sync config distante (`110/106/107`) avec logs d'application ; suppression de l'écrasement local `resetMode` au setup ; reconfiguration explicite du timer deep sleep avant sommeil |
| 4.33 | 2026-03 | Ajout de logs détaillés des échanges serveur : affichage du payload POST (masquage `api_key`) et du body GET (`outputs_state`) avec code HTTP |
| 4.32 | 2026-03 | Logs série catégorisés (`[BOOT]`, `[TIME]`, `[REMOTE]`, `[SERVER]`) et mise en évidence explicite des échanges serveur (poll config, envoi diagnostic, envoi périodique) |
| 4.31 | 2026-03 | OTA périodique: ajout d'une vérification OTA toutes les 2h (cumul RTC du deep sleep), maintien du check OTA prioritaire sur reset distant |
| 4.30 | 2026-03 | compatibilité toolchain GCC 14/newlib: symboles `__atomic_fetch_add_4` et `__atomic_fetch_sub_4` passés en `weak` dans `gcc_atomic_compat.c` pour éviter le conflit de linkage avec `libnewlib` |
| 4.29 | 2026-03 | resetMode distant: detection front montant avec seed au 1er poll, tentative OTA prioritaire (`n3OtaCheck`) avant reset, fallback `ESP.restart()` si aucune MAJ |
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
| 4.11 | — | Ancienne reference inventaire appareils |

---

## Références

- Configuration : `include/n3pp_config.h` → `FIRMWARE_VERSION`
- Inventaire : `docs/inventaire_appareils.md`
