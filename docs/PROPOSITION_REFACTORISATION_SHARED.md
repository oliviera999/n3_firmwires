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

## ⚠️ Vérification adversariale — pièges de fonctionnement à ne pas rater

Une passe de vérification (lecture croisée du code réel, n° de ligne) a confirmé
la solidité globale des propositions **mais** a mis au jour des **divergences
subtiles et fonctionnellement critiques** entre firmwares jumeaux : une
mutualisation *naïve* (copier le corps commun) **régresserait** un firmware.
Ces pièges commandent le découpage des lots.

| Piège | Où | Conséquence d'une fusion naïve | Parade |
|---|---|---|---|
| **`HeureSansWifi` asymétrique** | `n3pp_network.cpp:301` (onFailure) — **absent de msp** | n3pp perd la récupération d'horloge NVS hors-ligne quand tout le WiFi échoue | Garder les callbacks `onFailure/onSuccess` **injectés par firmware** (le design `n3WifiConnect` le permet déjà) |
| **`PontDiv = analogRead` brut** | `msp_sensors.cpp:155` ; n3pp le **refuse** (`n3pp_sensors.cpp:141-143`, « audit 4.38 ») | Réintroduit une valeur bruitée non filtrée dans n3pp → **fausses veilles d'urgence** (PontDiv pilote la protection batterie / veille infinie) | L'**acquisition** de `PontDiv` reste **hors** du code mutualisé ; ne partager que le rendu OLED + les formules |
| **Clés serveur 104/105 à sens opposé** | n3pp `104=HeureArrosage,105=tempsArrosage` ; msp `104=AngleServoHB,105=AngleServoGD` (`*_network.cpp:266-268`) | Un parseur partagé **indexé par numéro croiserait les câblages** (arrosage ↔ servo) | **Exclure** 104/105 du parseur commun ; ne mutualiser que 100/101/103/106/107/110/112 |
| **Clamp clé 102 (SeuilSec)** | n3pp clampe `0..4095` (`n3pp_network.cpp:257-264`) ; msp **ne clampe pas** (`:265`) | Comportement divergent conservé/introduit selon le sens de la fusion | Harmoniser le clamp **avant** de partager la clé 102 |
| **Flag anti-spam batterie divergent** | n3pp global `emailPontDivSent` (`n3pp_globals.cpp:51`) ré-armé dans `automatismes()` ; msp static `s_mspBatteryMailSent` ré-armé en tête de `sommeil()`. **Les deux sont `RTC_DATA_ATTR`** (survivent au deep sleep) | Double gestion d'alerte n3pp (`automatismes()` **et** `sommeil()`) vs mono-site msp → logique d'alerte cassée | Unifier contrat + lieu de reset + supprimer le double-site n3pp **avant** A8 |
| **`outputsGetFailureStreak`** | n3pp passe le vrai compteur (`n3pp_network.cpp:387`) ; msp force `0` (`msp_automation.cpp:160`) **alors qu'il l'incrémente** | Le rapport réseau msp changerait de contenu | Exposer le champ **en paramètre** de l'API mutualisée (valeur réelle ou 0 au choix du firmware) |
| **`N3NetStats` non thread-safe** | `s_stats` statique sans mutex (`n3_net_stats.cpp:26`) | Brancher `n3NetStatsRecordPost()` depuis les tâches async ffp5cs = **data race** (RMW `++` non atomiques) | Enregistrer **sous** `s_httpMutex` (déjà partagé POST/GET) **ou** ajouter un lock interne à `n3_net_stats` |
| **`String` dans le chemin chaud** | helper HMAC : `n3_data.cpp:131` utilise `String` ; ffp5cs (`hmac_sign.cpp:69-79`) l'évite (DRAM ~99,9 %) | Remonter la variante `String` régresserait la DRAM ffp5cs | Le helper partagé **doit** être la version mbedtls incrémentale **sans `String`** |

**Correction factuelle** : `n3_hmac` n'est **pas** une lib « pure » au niveau unité
de traduction — `n3_hmac.h` inclut `<Arduino.h>` et `n3_hmac.cpp` inclut
`<HTTPClient.h>`. Seule la fonction `n3HmacSha256` est pure. Pour que ffp5cs
(ESP-IDF, sans HTTPClient, budget DRAM) linke le futur `computeHmacHex`, l'isoler
dans une **unité de traduction / lib dédiée sans HTTPClient** (pas `n3_hmac` tel quel).

**Correction factuelle** : `n3PrintWakeupReason(prefs, rtc)` **existe déjà** dans
`n3_time` (`n3_time.cpp:104`) mais n3pp/msp ne l'utilisent pas et réimplémentent
`print_wakeup_reason`. A2 est donc une dé-duplication vers du code **déjà présent**
dans shared (à compléter du helper `n3TimeSyncBrokenDown`), pas une création.

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
| A6 | **Connexion WiFi (wrapper OLED)** | `Wificonnect` (`n3pp_network.cpp:278` / `msp_network.cpp:295`) | Cœur `n3WifiConnect` oui, callbacks OLED non | Callbacks **injectés par firmware** (garder `HeureSansWifi` n3pp) ; ne pas fusionner le corps | **RISQUÉ** — asymétrie `HeureSansWifi` |
| A7 | **Batterie (affichage)** | `batterie` (`n3pp_sensors.cpp:140` / `msp_sensors.cpp:154`) | Cœur `n3BatteryRead` oui, rendu OLED non | Rendu OLED + formules seulement ; **exclure** l'acquisition `PontDiv` | **RISQUÉ** — `analogRead` brut msp (régression audit 4.38) |
| A8 | **Sommeil / deep sleep** | `sommeil` (`n3pp_automation.cpp:314` / `msp_automation.cpp:221`) | Cœur `n3Sleep*` oui, séquence non | Mutualiser **après** harmonisation | **Haut** — flag anti-spam + double-site n3pp + reset servo msp |
| A9 | **POST données** | `datatobdd` (`n3pp_network.cpp:21` / `msp_network.cpp:20`) | Cœur `n3DataPost` oui | Partiel : ~15 lignes de squelette HMAC/epoch ; champs spécifiques | Faible — ROI faible, rien au-delà du squelette |
| A10 | **Poll config serveur** | `variablestoesp` (`n3pp_network.cpp:161` / `msp_network.cpp:165`) | Helpers `n3_outputs_json` oui | Partiel : mutualiser **seulement** 100/101/103/106/107/110/112 ; **exclure 104/105** (collision) | **Haut** — collision 104/105, clamp 102 |

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
| `hmac_sign.cpp/.h` | `n3_hmac` (unité dédiée **sans HTTPClient**) | **Remonter** | Message canonique dupliqué **2×** indépendamment (ffp5cs + n3_data) ; voir B1. Cible : la variante mbedtls **sans `String`** |
| Compteurs HTTP (`diagnostics._stats`, `automatism_sync._postOkCount`) | `n3_net_stats` (`N3NetStatsSnapshot`) | **Brancher (RISQUÉ)** | Snapshot shared plus riche, conçu *pour* ffp5cs mais non branché — **`s_stats` non thread-safe** : n'enregistrer que sous `s_httpMutex` |
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

**B1 — HMAC canonique dupliqué 2× (vérifié).** `ffp5cs/hmac_sign.cpp:45-91`
calcule `HMAC-SHA256(timestamp + "\n" + nonce + "\n" + body)` en **mbedtls
incrémental, sans `String`** (n'inclut pas `n3_hmac`). `shared/n3_data/src/n3_data.cpp:124-138`
reconstruit le **même message canonique** mais via `String signedMsg = …`
(`:131`, allocation heap) + `n3HmacSha256`. `ffp5cs/web_client.cpp:214` n'est
**pas** une 3ᵉ implémentation : il *délègue* à `HmacSign::computeHmacHex`.
`shared/n3_hmac` ne fournit qu'un HMAC plat (`X-Signature`), sans timestamp ni nonce.
→ **Remonter** `computeHmacHex` + `generateNonce` dans une **unité de traduction
dédiée sans `HTTPClient`** (car `n3_hmac.cpp` inclut aujourd'hui `<HTTPClient.h>` et
`n3_hmac.h` `<Arduino.h>` — incompatible avec le lien ESP-IDF de ffp5cs), en
adoptant la **variante mbedtls sans `String`** (celle de ffp5cs). Bénéfice net :
supprime aussi la `String` heap de `n3_data.cpp:131`. Faire consommer cette API par
`n3_data.cpp` **et** `ffp5cs/hmac_sign.cpp`. Le `getSecret/isEnabled`
(couplé `Secrets::API_SIG_SECRET`) reste côté ffp5cs. **Nonce** : conserver le
format **aléatoire** ffp5cs (`esp_fill_random`), supérieur pour l'anti-rejeu au
`epoch-compteur` de n3_data (compteur remis à 0 au reboot → collision possible).
Le format du nonce est **opaque au contrat serveur** (réinjecté dans `X-Sig-Nonce`),
donc unifier ne casse pas la validation.

**B2 — Stats réseau.** `N3NetStatsSnapshot` (`n3_data.h:49-71`) est alimenté
automatiquement à chaque `n3DataPost` et consommé par `n3MailBuildNetReportBody`.
Le commentaire shared indique un alignement « logs ffp5cs » : **shared a été pensé
pour ffp5cs, mais ffp5cs ne l'a pas branché** et maintient des compteurs plus
pauvres (`diagnostics.cpp:555`, `automatism_sync.cpp:631`). ffp5cs n'utilisant pas
`n3DataPost`, il faudrait appeler `n3NetStatsRecordPost()` depuis `web_client.cpp`.
**Risque vérifié** : `s_stats` (`n3_net_stats.cpp:26`) est un statique **sans
mutex** ; ffp5cs sérialise POST (`postSenderTask`) et GET (`netTask`) via **un
même** `s_httpMutex` (`web_client.cpp:23-24`). L'appel `n3NetStatsRecord*` doit donc
se faire **à l'intérieur** de cette section critique, sinon course de données sur
les `++`/`+=` non atomiques. À traiter comme **RISQUÉ**, pas « moyen ».

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

### Réserves d'implémentation (vérifiées) pour l'Axe C

Les frontières « pur vs couplé » ci-dessus sont exactes, mais chaque remontée
porte une contrainte concrète confirmée par lecture du code :

- `sensor_failure_manager.h/.cpp` : la classe est **entièrement paramétrée par
  constructeur** (aucun symbole `SensorConfig` utilisé) ; `config.h` n'y sert que
  pour la macro `SENSOR_LOG_PRINTF`. → retirer `config.h`, injecter la macro de log.
- `sensor_reading_fallback.h` : pur, mais API nommée `waterLevel`/`resolveWaterLevel`
  → **renommer** en neutre avant remontée.
- `ota_artifact_select.h` : dépend d'ArduinoJson **v7** (`.is<int>()`, `JsonVariantConst`).
  Valider la **parité v6/v7** entre toolchains WROOM 3.3.x / S3 2.0.x (même piège que `ESP32Servo`).
- `data_queue.h` : stockage fixe **5×1024 = 5 KB DRAM/instance** ; sa « thread-safety »
  est **par hypothèse mono-tâche**, pas intrinsèque → templatiser la taille, ne pas
  la présenter comme thread-safe multi-tâches.
- `clock_decision.h` inclut `epoch_util.h` → les **remonter ensemble**.

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
- B1 remonter le HMAC canonique (variante sans `String`, unité sans HTTPClient),
  dédoublonner les 2 implémentations ; garder le nonce aléatoire ffp5cs

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
- B2 brancher `N3NetStatsSnapshot` dans ffp5cs (**sous `s_httpMutex`** — voir risque)

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

---

# Core architectural partagé — feuille de route **sûre**

Objectif ciblé : bâtir un **socle commun** que tous les firmwares consomment, en
**faisant remonter le meilleur de ffp5cs** (le plus mûr), puis de n3pp (2ᵉ plus
mûr), pour **élever** msp / poissonglouton au même niveau. Cette section isole
**strictement le sous-ensemble sans risque** : que des briques *pures* ou
*verbatim*, à état non partagé, sans piège de fonctionnement.

> Elle **exclut délibérément** tout ce qui a été classé RISQUÉ/Haut dans la passe
> de vérification (A6 `HeureSansWifi`, A7 `PontDiv`, A8 `sommeil`, A10 clés
> 104/105, B2 non gardé) et tout ce qui est « ne pas toucher » (web_client,
> net_request_pool, ota_manager, power light-sleep, nvs_manager, wifi_manager).

## Vue en couches du core

```
┌─────────────────────────────────────────────────────────────┐
│  Couche 3 — Orchestration (helpers paramétrés)               │  risque faible/modéré
│  n3MailNotify · n3_ota_ui (harnais OTA+OLED) · ota multi-cible│
├─────────────────────────────────────────────────────────────┤
│  Couche 2 — Robustesse capteurs                              │  risque faible
│  sensor_failure_manager · sensor_reading_fallback            │
├─────────────────────────────────────────────────────────────┤
│  Couche 1 — Primitives (data / sécurité / temps)            │  risque faible
│  HMAC canonique (sans String) · n3DataSendHeartbeat ·        │
│  n3TimeSyncBrokenDown · (n3PrintWakeupReason déjà présent)   │
├─────────────────────────────────────────────────────────────┤
│  Couche 0 — Logique pure (header-only, testable natif)      │  risque NUL
│  epoch_util · clock_decision · sleep_decision · reset_reason │
│  uptime_format · login_throttle                              │
└─────────────────────────────────────────────────────────────┘
   Transversal : n3_log (couche de log unifiée, cible à terme)
```

## Couche 0 — Logique pure (risque **nul**, à faire en premier)

Toutes ces briques viennent de **ffp5cs**, sont **header-only**, sans dépendance
Arduino/FreeRTOS/config, et **déjà extraites pour test natif** — la frontière
propre est tracée par ffp5cs lui-même. Aucun état partagé → zéro risque de
concurrence, zéro régression possible.

| Brique (source ffp5cs) | Destination shared | Ce que gagnent n3pp/msp/pgl | Vérifié |
|---|---|---|---|
| `epoch_util.h` (`isValidEpoch` unsigned, `epochAbsDiff`) | `n3_time` | Validation epoch anti-overflow 32-bit avec **borne haute** (aujourd'hui `n3TimeHasPlausibleEpoch` n'a qu'une borne basse) | Pur `<ctime>` |
| `clock_decision.h` (`isNtpEpochPlausible`, `computeDriftPpm`, `computeDriftSeconds`) | `n3_time` (avec `epoch_util`) | Détection de dérive d'horloge / plausibilité NTP | Pur `<cmath>/<ctime>` |
| `sleep_decision.h` (`adaptiveSleepDelay`) | `n3_common` | Délai de sommeil adaptatif (nuit/erreurs/backoff, borné) | Pur `<algorithm>/<cstdint>` |
| `reset_reason.h` (`resetReasonToString`, `isCrash`) | `n3_common` | Libellé + classification crash pour l'alerting mail | Pur (`esp_system.h`, mocké en natif) |
| `uptime_format.h` (`formatUptime` "Jd HH:MM:SS") | `n3_time` / `n3_mail` | Uptime lisible dans les rapports mail | Pur `<cstdio>` |
| `login_throttle.h` (`Throttle<Policy>`) | `n3_common` | Anti-brute-force fail-safe pour tout portail web | Template pur `<cstdint>` |

**Action** : copier les headers, ajouter une suite Unity par brique dans
`shared/tests_native/`, référencer depuis les firmwares. Rien à réécrire.

## Couche 1 — Primitives data / sécurité / temps (risque **faible**)

| Brique | Source | Destination | Contrainte de sécurité vérifiée |
|---|---|---|---|
| **HMAC canonique** `computeHmacHex(secret, ts, nonce, body)` + `generateNonce` | ffp5cs `hmac_sign` | **unité dédiée sans `HTTPClient`** (pas `n3_hmac` tel quel) | Variante **mbedtls sans `String`** ; nonce **aléatoire** ffp5cs ; dédoublonne aussi `n3_data.cpp:131` |
| **`n3DataSendHeartbeat(...)`** | n3pp/msp (verbatim) | `n3_data` | Corps **octet-pour-octet identique** n3pp↔msp ; état 100 % en paramètres |
| **`n3TimeSyncBrokenDown(rtc, &s,&mi,&h,&j,&mo,&a)`** | n3pp/msp | `n3_time` | Resync des 6 globals (dupliqué 3×) ; helper pur |
| Adopter **`n3PrintWakeupReason`** (déjà dans `n3_time:104`) | shared existant | — | n3pp/msp cessent de réimplémenter `print_wakeup_reason` (langue de log en paramètre) |

## Couche 2 — Robustesse capteurs (risque **faible**, fort apport)

`n3_analog_sensors` fait aujourd'hui du filtrage *intra-lecture* mais n'a **aucune
mémoire inter-lectures**. ffp5cs apporte l'état manquant :

| Brique (source ffp5cs) | Destination | Apport | Réserve d'implémentation vérifiée |
|---|---|---|---|
| `sensor_failure_manager.h/.cpp` | `n3_analog_sensors` | Machine d'état : désactive un capteur mort, tente une réactivation espacée | **Retirer `config.h`** (sert juste à la macro de log) ; classe déjà 100 % paramétrée par constructeur |
| `sensor_reading_fallback.h` | `n3_analog_sensors` | Cascade `current → dernier bon → fallback` | **Renommer** l'API `waterLevel` en neutre |

Bénéfice direct : msp/n3pp remplacent leur fallback ad hoc **inférieur**
(`msp_sensors.cpp:108-146` : simple log + constante) par une vraie gestion d'état.

## Couche 3 — Orchestration paramétrée (risque **faible/modéré**)

Ces helpers encapsulent une orchestration **verbatim** entre n3pp/msp ; le seul
travail est de rendre paramétrable ce qui varie (préfixe projet, titre/URL OTA).

| Brique | Source | Destination | Variabilité à paramétrer |
|---|---|---|---|
| **`n3MailNotify(project, severity, subject, msg, cfg, postOk, &budget)`** (A3) | n3pp/msp (verbatim sauf préfixe) | `n3_mail` | Préfixe projet uniquement |
| **`n3_ota_ui`** — harnais OTA + OLED + timer 2 h, `N3OtaPeriodicConfig{title,prodUrl,testUrl,display,version}` (A4) | n3pp/msp (~130 lignes verbatim ×2) | `n3_common` | Titre OLED, URLs, version, handle display ; **encapsuler les buffers module-static dans un contexte** |
| **OTA multi-artefacts** `ota_artifact_select.h` (cascade `env×modèle` + sha256/signature) | ffp5cs | `n3_ota` | Rétro-compatible (fallback legacy) ; **valider la parité ArduinoJson v6/v7** entre toolchains |

## Couche transversale — Logging unifié (`n3_log`)

ffp5cs a une **couche de log mûre** (`log.h` : niveaux `ERROR→VERBOSE`, timestamp,
gating par `LOG_LEVEL`/`SERIAL_ENABLED`). n3pp/msp utilisent ~150 `Serial.print`
bruts, non filtrables. Un `n3_log` (extrait de ffp5cs, **découplé de `config.h`** —
niveau injecté par macro de build) donnerait à tous les firmwares un logging
homogène et coupable en production.

> **Statut** : cible de core légitime mais **chantier mécanique volumineux**
> (migration des ~150 sites `Serial.print`) — faible risque *sémantique*, gros
> volume. À planifier **après** les couches 0-2, firmware par firmware.

## Étoile polaire — squelette applicatif (`n3_app`)

n3pp et msp partagent un **squelette de cycle de vie identique** :
`setup → wifi → time → sensors → POST → poll config → automation → mail → OTA →
sleep`. À terme, ce squelette pourrait devenir un **framework applicatif partagé**
où chaque firmware ne fournit que ses callbacks (`readSensors`, `buildPayload`,
`applyConfig`). C'est la forme aboutie du « core architectural », mais elle
**dépend de la résolution préalable** des divergences RISQUÉ (A6/A7/A8/A10) — donc
**pas maintenant**. Les couches 0-2 sont les fondations qui y mènent sans risque.

## Ordre d'exécution recommandé (100 % sûr d'abord)

1. **Couche 0** (6 briques pures) — risque nul, bénéfice immédiat, valide le socle + les tests natifs.
2. **Couche 1** (HMAC sans String, heartbeat, time helpers) — dédoublonnage sécurité/temps.
3. **Couche 2** (robustesse capteurs) — élève msp/n3pp/pgl au niveau ffp5cs.
4. **Couche 3** (mail notify, OTA UI, OTA multi-cible) — orchestration paramétrée.
5. *(Ensuite seulement)* `n3_log`, puis — après harmonisation des divergences — l'étoile polaire `n3_app`.
