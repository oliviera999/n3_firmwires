# Proposition — Refactorisation vers `shared/` (n3pp, msp, ffp5cs)

> **Statut : PROPOSITION — aucune modification de code n'a été appliquée.**
> Ce document répond à la question : *quels éléments de n3pp / msp (et ffp5cs)
> peuvent être mutualisés dans `shared/`, sachant que certains feraient
> **progresser** ce qui existe déjà — et inversement, quelles briques de ffp5cs
> pourraient **enrichir** `shared/` ?*
>
> Objectif : décider **quels lots ouvrir**, dans quel ordre, avant d'écrire la
> moindre ligne. Chaque lot est indépendant et incrémentable.

## Résumé exécutif

`shared/` est déjà bien factorisé pour le **cœur** (WiFi scan/RSSI, POST+HMAC,
SMTP, ADC filtré, OTA sha256+ECDSA, deep sleep, OLED). Trois angles morts
subsistent :

1. **n3pp ↔ msp** : le *cœur* passe par `shared/`, mais toute la **colle
   d'orchestration** (harnais OTA + OLED, heartbeat, notifications mail,
   rapport réseau, réveil, sommeil) est **recopiée à l'identique** dans les deux
   firmwares. Plusieurs fonctions sont **verbatim**.
2. **ffp5cs → shared** : ffp5cs **réimplémente** en interne HMAC, stats réseau et
   plusieurs logiques *pures* (déjà extraites en headers testables) qui
   devraient descendre dans `shared/`. Le HMAC canonique existe aujourd'hui en
   **trois exemplaires** (`ffp5cs/hmac_sign`, `shared/n3_data`, `ffp5cs/web_client`).
3. **ffp5cs enrichit shared (l'inverse)** : ffp5cs porte des briques **plus
   évoluées** que shared (OTA multi-artefacts, gestion de défaillance capteur,
   throttle login, HMAC anti-rejeu par nonce) — remontables telles quelles, au
   bénéfice immédiat de n3pp/msp/poissonglouton.

Le principe directeur, surtout pour ffp5cs : **remonter la primitive pure vers
`shared/`**, presque jamais *remplacer* un module ffp5cs par shared — parce que
`shared/` cible un modèle d'exécution **bloquant / deep-sleep** que ffp5cs a
délibérément abandonné (FreeRTOS multi-tâches, mutex TLS, file SD/NVS, light
sleep).

## État des lieux — qui utilise quoi

| Firmware | Modèle d'exécution | Usage de `shared/` |
|---|---|---|
| **n3pp** | mono-boucle → deep sleep | Large : `n3_wifi`, `n3_data`, `n3_mail`/`n3_notify`, `n3_outputs_json`, `n3_battery`, `n3_analog_sensors`, `n3_time`, `n3_sleep`, `n3_display`, `n3_ota` |
| **msp** | mono-boucle → deep sleep | Idem n3pp (quasi-jumeau) |
| **ffp5cs** | FreeRTOS multi-tâches, light sleep | **Très partiel** : seulement `n3_notify` (taxonomie sévérité), `n3_mail` (session SMTP), `n3_analog_sensors` (luminosité). Tout le reste réimplémenté. |

**Observation clé** : msp/n3pp sont en **retrait**, pas en avance. Ils sont les
**consommateurs** naturels des briques que ffp5cs a déjà mûries (défaillance
capteur, OTA multi-cible, throttle, anti-rejeu). Il n'y a quasiment rien à
remonter *depuis* msp/n3pp — leur problème est la **duplication mutuelle**.

---

## Axe A — n3pp ↔ msp : mutualiser la colle dupliquée

Le cœur est déjà partagé ; ce sont les **wrappers et l'orchestration** autour
qui sont recopiés fichier à fichier. Classés par ROI décroissant / risque
croissant.

| # | Zone | Fonctions (n3pp / msp) | Déjà dans shared ? | Verdict | Risque |
|---|---|---|---|---|---|
| A1 | **Heartbeat** | `sendHeartbeat` (`n3pp_network.cpp:110` / `msp_network.cpp:115`) | Cœur `n3DataPost` oui, wrapper non | **Extraire** `n3DataSendHeartbeat(...)` | **Faible** — *verbatim identique* |
| A2 | **Réveil** | `print_wakeup_reason` (`n3pp_automation.cpp:381` / `msp_automation.cpp:197`) | Non | **Extraire** vers `n3_time` | **Faible** — logique identique (seuls les logs FR/EN diffèrent) |
| A3 | **Notif SMTP d'alerte** | `sendEmailNotification` (`n3pp_automation.cpp:61` / `msp_automation.cpp:48`) | Cœur `n3MailSendText`/`n3Notif*` oui, failover non | **Extraire** `n3MailNotify(project, severity, …)` | **Faible** — verbatim sauf préfixe projet |
| A4 | **Harnais OTA + OLED + timer 2 h** | `renderOtaScreen`, `otaDisplay*Callback`, `tryOtaBeforeReset…`, `maybeRunPeriodicOtaCheck`, `accumulate…` (`main.cpp` n3pp 30-160 / msp 146-276) | Cœur `n3OtaCheck` oui, harnais non | **Extraire** `n3_ota_ui` / extension `n3_common` avec `N3OtaPeriodicConfig{title,prodUrl,testUrl,display,version}` | Moyen — ~130 lignes verbatim ×2 |
| A5 | **Rapport réseau mail (P4)** | `…MaybeSendNetworkReportEmail` + `…Accumulate…` (`n3pp_network.cpp:318` / `msp_automation.cpp:96`) | Builder `n3MailBuildNetReportBody` oui, orchestration non | **Extraire** l'orchestration (timer RTC + gardes) | Moyen |
| A6 | **Connexion WiFi (wrapper OLED)** | `Wificonnect` (`n3pp_network.cpp:278` / `msp_network.cpp:295`) | Cœur `n3WifiConnect` oui, callbacks OLED non | **Extraire** un wrapper avec callbacks OLED partagés | Faible/Moyen |
| A7 | **Batterie (affichage)** | `batterie` (`n3pp_sensors.cpp:140` / `msp_sensors.cpp:154`) | Cœur `n3BatteryRead` oui, rendu OLED non | Descendre le rendu OLED paramétré dans `n3_battery` | Moyen |
| A8 | **Sommeil / deep sleep** | `sommeil` (`n3pp_automation.cpp:314` / `msp_automation.cpp:221`) | Cœur `n3Sleep*` oui, séquence non | Mutualiser **après** harmonisation | Moyen/Haut — flags divergents (voir ci-dessous) |
| A9 | **POST données** | `datatobdd` (`n3pp_network.cpp:21` / `msp_network.cpp:20`) | Cœur `n3DataPost` oui | Partiel : squelette commun, champs spécifiques | Moyen |
| A10 | **Poll config serveur** | `variablestoesp` (`n3pp_network.cpp:161` / `msp_network.cpp:165`) | Helpers `n3_outputs_json` oui | Partiel : clés 106/107/110/112/100/101 communes, queue divergente | Haut |

### Détail des candidats forts

**A1 — `sendHeartbeat` (verbatim).** Les deux corps sont **strictement identiques**
(vérifié : `diff` vide). Mêmes champs `uptime/free/min/reboots/rssi`, même
`N3PostConfig`, même `minHeap`. Aucune variabilité (tout vient de globals).
→ `n3DataSendHeartbeat(url, apiKey, sensor, version, bootCount, sigSecret, epoch)`
dans `n3_data`. **Candidat idéal pour le premier extract.**

**A3 — `sendEmailNotification`.** Verbatim hormis le préfixe projet (`"N3PP"` /
`"MSP1"`). Toute la mécanique failover (cap sévérité, budget `FAILOVER_MAIL_BUDGET=8`,
garde WiFi, latch) est dupliquée. `n3_mail` possède déjà `n3MailFormatSubject`
et `n3Notif*` — il manque le wrapper d'orchestration.

**A4 — Harnais OTA.** `n3pp/src/main.cpp:30-160` et `msp/src/main.cpp:146-276`
sont quasi identiques caractère pour caractère ; seule variabilité : titre OLED,
URL OTA, `FIRMWARE_VERSION`, handle `display`. Les callbacks d'affichage OTA sont
**verbatim**. ~130 lignes × 2 factorisables.

**A2 + `n3_time`.** Le bloc « resync des 6 globals `seconde…annee` depuis le RTC »
est dupliqué **3 fois** (les deux `print_wakeup_reason` + `HeureSansWifi`).
→ candidat `n3TimeSyncBrokenDown(rtc, &s, &mi, &h, &j, &mo, &a)`.

### Prérequis Axe A

- **Extraire un `msp_globals.cpp`.** msp n'a pas de fichier globals dédié : ses
  globals sont inline dans `msp/src/main.cpp:29-289`, alors que n3pp les isole
  dans `n3pp_globals.cpp`. Aligner cette asymétrie **avant** toute mutualisation.
- **Harmoniser les divergences avant A8.** Le flag anti-spam batterie est un
  **global** côté n3pp (`emailPontDivSent`) mais un **static** côté msp
  (`s_mspBatteryMailSent`). Unifier le contrat d'abord, mutualiser ensuite.

---

## Axe B — ffp5cs réimplémente `shared/` : remonter les primitives pures

ffp5cs a `lib_extra_dirs = ../shared` mais n'appelle presque rien. Une partie
des réimplémentations est **légitime** (architecture FreeRTOS) ; une autre est
de la **pure redondance** à corriger.

| Module ffp5cs | Équivalent shared | Verdict | Justification |
|---|---|---|---|
| `hmac_sign.cpp/.h` | `n3_hmac` | **Remonter** | Primitive mbedtls pure, dupliquée **3×** (voir B1) |
| Compteurs HTTP (`diagnostics._stats`, `automatism_sync._postOkCount`) | `n3_net_stats` (`N3NetStatsSnapshot`) | **Remplacer / brancher** | Snapshot shared plus riche, conçu *pour* ffp5cs mais non branché |
| `sleep_decision.h`, `clock_decision.h` | (aucune — logique pure) | **Remonter** vers `n3_common` | Déjà pures/testées, zéro couplage |
| `mailer.cpp` builders de corps | `n3MailBuild*Body` | **Compléter** | Session SMTP déjà mutualisée ; corps encore locaux |
| `rtc_ds3231.cpp/.h` | (aucune) | **Remonter** (optionnel) vers `n3_time` | Driver DS3231 réutilisable (poissonglouton) |
| `wifi_manager.cpp` | `n3_wifi` | **Garder** — extraire la primitive de tri RSSI | Même algo scan+RSSI+BSSID, mais surcouche AP/captive/S3 |
| `web_client*`, `net_request_pool`, `ffp3_post_body` | `n3_data` | **Garder** | Async + mutex TLS + pool statique + file SD/NVS + ordre canonique FFP3 |
| `ota_manager*` | `n3_ota` | **Garder** | Range resumable + relecture flash + FS update + task dédiée |
| `power.cpp` (light sleep), `sleep_decision` orchestration | `n3_sleep` (deep sleep) | **Garder** | Paradigmes **opposés** (light vs deep) |
| `nvs_manager*` | (aucune) | **Garder** (candidat future lib `n3_nvs`) | Pas d'équivalent shared |
| `display_view`, `i2c_bus` | `n3_display` | **Garder** — réutiliser `n3DisplayInit` pour detect+begin | Mutex I2C + splash + cache spécifiques |

### Détail

**B1 — HMAC canonique dupliqué 3×.** `ffp5cs/hmac_sign.cpp:45-91` calcule
`HMAC-SHA256(timestamp + "\n" + nonce + "\n" + body)` en **mbedtls direct** (vérifié :
n'inclut pas `n3_hmac`). `shared/n3_data/src/n3_data.cpp:118-138` reconstruit le
**même** schéma `X-Sig-Timestamp/Nonce/Hmac` inline. `shared/n3_hmac` ne fournit
qu'un HMAC plat (`X-Signature`), sans timestamp ni nonce. → **Remonter**
`computeHmacHex` + `generateNonce` dans `n3_hmac` (pur, testable — la suite
`shared/tests_native/test/test_hmac` existe déjà), puis faire consommer cette API
par `n3_data.cpp` **et** `ffp5cs/hmac_sign.cpp`. Le `getSecret/isEnabled`
(couplé `Secrets::API_SIG_SECRET`) reste côté ffp5cs.

**B2 — Stats réseau.** `N3NetStatsSnapshot` (`n3_data.h:49-71`) est alimenté
automatiquement à chaque `n3DataPost` et consommé par `n3MailBuildNetReportBody`.
Le commentaire shared indique un alignement « logs ffp5cs » : **shared a été pensé
pour ffp5cs, mais ffp5cs ne l'a pas branché** et maintient des compteurs plus
pauvres (`diagnostics.cpp:555`, `automatism_sync.cpp:631`). ffp5cs n'utilisant pas
`n3DataPost`, il faudrait appeler `n3NetStatsRecordPost()` depuis `web_client.cpp`
(comptage dans le pool async — difficulté moyenne).

**B3 — Logiques pures.** `sleep_decision.h:24` (`adaptiveSleepDelay`) et
`clock_decision.h:38` (`isNtpEpochPlausible`, `computeDriftPpm`) sont **plus
shared-ready que du code déjà dans shared** (100 % purs). → `n3_common`.
`clock_decision` tire `epoch_util.h` : le remonter ensemble (voir C).

---

## Axe C — l'inverse : ffp5cs fait progresser `shared/`

Les briques les plus mûres de ffp5cs ont **déjà été extraites en headers purs
« pour testabilité native »** — la frontière propre/couplé est donc déjà tracée,
ce qui en fait les meilleurs candidats à la mutualisation *montante*.

| Brique ffp5cs | Lib shared à enrichir | Apport | Faisabilité |
|---|---|---|---|
| `ota_artifact_select.h` | `n3_ota` | Manifeste OTA multi-cible `env × modèle` + cascade de fallback + sha256/signature | **Forte** (header-only ArduinoJson) |
| `login_throttle.h` | `n3_common` | Anti-brute-force fail-safe, 0 heap, testé | **Forte** (template pur) |
| `epoch_util.h` | `n3_time` | `isValidEpoch` **unsigned** anti-overflow 32-bit (borne haute robuste absente de shared) | **Forte** (pur) |
| `uptime_format.h` | `n3_time`/`n3_mail` | Format `"Jd HH:MM:SS"` pour les rapports mail | **Forte** (pur) |
| `reset_reason.h` | `n3_common` | Libellé + classification `isCrash` pour l'alerting | **Forte** (pur) |
| `sensor_failure_manager.h/.cpp` | `n3_analog_sensors` (ou nouvelle lib) | Machine d'état : désactivation/réactivation auto d'un capteur défaillant | **Forte** |
| `sensor_reading_fallback.h` | `n3_analog_sensors` | Cascade `current → lastValid → fallback` | **Forte** (renommer le champ « waterLevel ») |
| `hmac_sign` (nonce) | `n3_hmac`/`n3_data` | Anti-**rejeu** par nonce (vs signature body seule) | **Forte** conceptuellement |
| `data_queue.h/.cpp` | future `n3_queue` | Ring buffer FIFO statique (0 malloc) | **Forte** (à templatiser) |
| `ultrasonic_filter.h` | `n3_analog_sensors` | Détection bimodale d'échos + rejet de saut directionnel | Moyenne (extraire les seuils en config) |
| `web_client_queue.cpp` | future `n3_data_queue` | Retry POST persistant NVS (non-perte hors-ligne) | Moyenne (abstraire le backend de stockage) |
| `nvs_manager_typed.cpp` | future `n3_nvs` | NVS typé avec **skip-write si inchangé** (épargne le flash) | Moyenne |

### Ce que shared gagne concrètement

- **`sensor_failure_manager` + `sensor_reading_fallback`** : `n3_analog_sensors`
  fait aujourd'hui du filtrage *intra-lecture* (médiane, outliers, EMA) mais n'a
  **aucune mémoire inter-lectures** — il ne sait pas désactiver un capteur mort ni
  garder le dernier bon échantillon. msp/n3pp gèrent ça de façon ad hoc et
  **inférieure** (`msp_sensors.cpp:108-146` : simple log + fallback constant).
  Remonter la machine d'état ffp5cs bénéficie directement à n3pp/msp/poissonglouton.
- **`ota_artifact_select`** : `n3_ota` ne gère qu'une **cible unique** ; la cascade
  `env × modèle` est exactement ce qu'il faudrait à n3pp/msp s'ils passent
  multi-cartes, avec un seul `metadata.json`. Rétro-compatible (fallback legacy).
- **`login_throttle`** : aucune protection anti-brute-force n'existe dans shared.

---

## Ce qu'il ne faut **PAS** toucher

Ces modules ffp5cs encapsulent l'architecture qui *distingue* ffp5cs des
firmwares mono-boucle. Les remplacer par shared **régresserait** le firmware :

- `web_client` / `net_request_pool` / `ffp3_post_body` — async, mutex TLS, pool
  de slots statiques, file SD/NVS offline-first, ordre canonique anti-401 HMAC.
- `ota_manager*` — téléchargement HTTP Range resumable, vérif en relisant la
  partition flashée, mise à jour FS séparée, task dédiée.
- `power.cpp` (light sleep) — **paradigme opposé** au deep sleep de `n3_sleep` :
  préserve les tâches FreeRTOS et les credentials WiFi.
- `nvs_manager*`, `wifi_manager` (surcouche AP/captive/S3), `i2c_bus` (mutex I2C).

---

## Plan proposé — lots par ordre de ROI / risque

**Lot 0 — Prérequis (Axe A).** Extraire `msp_globals.cpp` ; harmoniser le flag
anti-spam batterie n3pp/msp. *Pas de mutualisation encore, juste l'alignement.*

**Lot 1 — Extractions verbatim, risque minimal.**
- A1 `n3DataSendHeartbeat` → `n3_data`
- A2 `print_wakeup_reason` + `n3TimeSyncBrokenDown` → `n3_time`
- C `epoch_util` / `uptime_format` / `reset_reason` → `n3_time` / `n3_common`
- B1 remonter le HMAC canonique (nonce) → `n3_hmac`, dédoublonner les 3 copies

**Lot 2 — Orchestration mail & OTA (Axe A, volumétrie forte).**
- A3 `n3MailNotify(...)` → `n3_mail`
- A4 `n3_ota_ui` (`N3OtaPeriodicConfig`) → `n3_common`
- A5 orchestration rapport réseau P4 → `n3_mail`

**Lot 3 — Robustesse capteurs (Axe C → bénéfice n3pp/msp).**
- `sensor_failure_manager` + `sensor_reading_fallback` → `n3_analog_sensors`
- A7 rendu batterie OLED paramétré → `n3_battery`

**Lot 4 — OTA multi-cible & sécurité (Axe C).**
- `ota_artifact_select` → `n3_ota` (rétro-compatible)
- `login_throttle` → `n3_common`
- B2 brancher `N3NetStatsSnapshot` dans ffp5cs

**Lot 5 — Files & NVS (nouvelles libs, plus lourd).**
- `data_queue` → `n3_queue` ; `web_client_queue` → `n3_data_queue`
- `nvs_manager_typed` → `n3_nvs` ; `rtc_ds3231` → `n3_time`

## Précautions transverses

- **Versionnage** : toute modif d'une lib incrémente sa `version` dans
  `library.json` (semver) + met à jour la table de `shared/README.md`, et bumpe la
  version du/des firmware(s) touché(s) (`VERSION.md`) — cf. skill
  `bump-firmware-version`.
- **Tests natifs** : chaque brique pure remontée doit venir avec sa suite Unity
  (`shared/tests_native/`), lancée suite par suite comme en CI.
- **Compatibilité toolchain** : WROOM = pioarduino arduino-esp32 3.3.x ;
  S3 / `*-cam` = espressif32@6.13.0 (2.0.x). Toute lib remontée doit compiler sous
  les deux (le cas `ESP32Servo` montre que ce n'est pas acquis).
- **Budget DRAM ffp5cs** : profil WROOM ~99,9 % — proscrire toute API shared qui
  introduirait des `String`/allocations dans les chemins chauds de ffp5cs.
- **Incrémental** : chaque lot est autonome et livrable seul ; commencer par le
  Lot 1 (gain immédiat, risque quasi nul) valide la démarche.
