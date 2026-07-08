# Version n3pp (N3PhasmesProto — Serre / aquaponie)

Version actuelle : **4.55** (définie dans `include/n3pp_config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 4.55 | 2026-07-08 | **Veille infinie sous seuil batterie télécommandable.** Nouvel interrupteur serveur (GPIO virtuel **112**, `VeilleInfinie`, défaut `1` = comportement historique) lu dans `variablestoesp()` et conservé en RTC (comme `SeuilPontDiv`) : quand il vaut `0`, l'ESP **n'entre plus** en veille infinie (sommeil GPIO-only `sleepSeconds=0`) sous `SeuilPontDiv` et retombe sur le sommeil timer normal (`FreqWakeUp`). Gate appliquée aux deux sites de déclenchement (`automatismes()` et `sommeil()`) ; l'alerte batterie faible reste émise (arbitrée serveur/failover, découplée de l'interrupteur). Valeur echo-back au POST (`VeilleInfinie`). Piloté depuis l'interface de contrôle serre/élevage (n3_serveur, section « Énergie »). |
| 4.54 | 2026-07-07 | **Phase 3 (suite) — arrosages et « arrosage continu » arbitrés.** Le serveur (n3_serveur 6.18.0) dérive désormais « Arrosage effectué » (transition `etatPompe` au POST) et « Arrosage continu » (`etatPompe=1` sur ≥ 2 lignes). Les trois confirmations d'arrosage (auto sol sec, heure programmée, manuel) sont gatées par `postOkThisWake` (émises seulement en failover, où le P3 est filtré). Alerte « pompe continue » (main.cpp) : le POST immédiat `etatPompe=1` est conservé (c'est la donnée dont le serveur dérive l'alerte) ; s'il réussit → latch sans mail local, sinon failover P1 comme avant. |
| 4.53 | 2026-07-07 | **Phase 3 arbitrage mails — ESP en relais.** Nouveau flag RTC `postOkThisWake` capturant le succès HTTP du POST de chaque réveil (résultat auparavant ignoré). POST OK → le serveur (n3_serveur 6.16.0) est l'émetteur primaire des alertes partagées : l'ESP se tait sur **sol sec** (raise/clear, latch ré-armé en silence) et **batterie** (mail seulement ; la protection sommeil reste inconditionnelle). POST échoué → failover anti-congestion (§3.4) : sévérité plafonnée P1/P2 (`n3NotifModeCapFailover`), SMTP tenté seulement si WiFi connecté, budget RTC de 8 mails par épisode hors-ligne (ré-armé au POST OK), rapport réseau P4 reporté. Kill-switch GPIO 101 inchangé. Non gatés : pompe continue, arrosages (pas de couverture serveur). |
| 4.52 | 2026-07-07 | **Phase 0 arbitrage mails — latch anti-spam sur livraison SMTP confirmée** (cf. `n3_serveur/docs/ARCHITECTURE_MAILS_ARBITRAGE.md`). `sendEmailNotification()` retourne désormais un booléen de succès (true = livré SMTP ou volontairement filtré par le mode ; false = échec d'envoi) et les flags anti-spam (`emailHumidSent`, `emailPontDivSent`, `emailPompeSent`) ne sont latchés que sur succès — un envoi échoué hors ligne est retenté au réveil suivant au lieu d'être perdu. Aucun changement d'arbitrage. |
| 4.51 | 2026-07-06 | **Test OTA** : publication distante canal `n3pp` (validation pipeline OTA prod). |
| 4.50 | 2026-07-05 | **Vague 1 audit serveur.** URLs canoniques Slim (`/n3pp/post-data`, `/n3pp/api/outputs/state`, `/n3pp/heartbeat` ; préfixe `/n3pp-test/` en TEST_MODE) ; heartbeat POST par cycle de réveil (`sendHeartbeat` via `n3_data`) ; `N3PP_SERVER_SCHEME` HTTPS par défaut dans `n3pp_config.h` ; transport TLS activé sur l'env par défaut (`USE_HTTPS_ENDPOINTS`). |
| 4.49 | 2026-07-05 | Publication OTA prod : déploiement distant canal `n3pp` (alignement code audit + serveur v6.6.x) |
| 4.48 | 2026-07-04 | **Audit n3pp/msp1 (2/2) — durcissement communication (additif, rétro-compatible).** Lib `n3_data` : signature HMAC **couvrant tout le corps** via en-têtes `X-Sig-Timestamp`/`X-Sig-Nonce`/`X-Sig-Hmac` = `HMAC(ts\nnonce\nbody)` (un serveur ancien ignore ces en-têtes et retombe sur le timestamp/signature du body). GET d'état : envoi de `X-Api-Key` (exigible côté serveur via flag). Lib `n3_ota` : flag opt-in `N3_OTA_REQUIRE_SIGNATURE` (refuse un binaire sans signature ECDSA). |
| 4.47 | 2026-07-04 | **Audit n3pp/msp1.** Corrige `SeuilSec` (défaut 5000 hors plage ADC 12 bits → 1500, borné 0..4095 à la clé 102, persistant en RTC comme `FreqWakeUp`/`SeuilPontDiv`/`HeureArrosage`/`tempsArrosageSec`) ; comptage du temps écoulé mesuré (millis) en mode éveillé `WakeUp=1` au lieu d'ajouter `FreqWakeUp` par itération (évitait OTA/rapport en rafale + cooldown arrosage neutralisé) ; latch anti-spam `emailPompeSent` (alerte pompe active) ; protection batterie faible = sommeil GPIO-only décorrélé de l'email (via fix `n3_sleep` sur `sleepSeconds==0`) ; détection de front du reset distant (110) persistée en RTC ; retrait des `RTC_DATA_ATTR String` (motif UAF/inefficace) ; brown-out détecteur réactivé après le boot ; attente SNTP bornée avant le 1er POST ; bornage affichage batterie. Lib partagée `n3_sleep` : `sleepSeconds==0` désactive la source timer (réveil GPIO uniquement). |
| 4.46 | 2026-07-03 | Publication OTA prod : incrément version pour déploiement distant (canal `n3pp`) |
| 4.45 | 2026-07-03 | Publication OTA test : incrément version pour déploiement distant (canal `n3pp-test`) |
| — | 2026-07-03 | **Lib partagée** `n3_defaults.h` : `N3_DAYLIGHT_OFFSET` 3600→0 (UTC+1 Casablanca permanent, sans double décalage `isDST()` Arduino). Rebuild n3pp recommandé au prochain flash. |
| 4.44 | 2026-06 | Notifications par sévérité (P1–P4) + mode de verbosité télécommandable (GPIO 101 étendu : `none`/`important`/`partial`/`full`, rétro-compatible `checked`/`unchecked`) via lib partagée `n3_notify`. Anti-spam deep-sleep corrigé (`emailHumidSent`/`emailPontDivSent` en `RTC_DATA_ATTR`), dédup batterie + re-armement au retour à la normale, sujets `[N3PP][Pn]`, rapport réseau = P4 (diagnostic) |
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
