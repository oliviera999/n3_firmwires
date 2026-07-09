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

> **Mise à jour (uploadphotosserver)** : l'assemblage HMAC canonique
> `timestamp\n nonce\n body` existe en fait en **3 implémentations indépendantes** —
> `shared/n3_data.cpp:124-138`, `ffp5cs/hmac_sign.cpp:45-91`, et
> `uploadphotosserver/camera_uploader.cpp:44-67` (`cameraUploadAddSignatureHeaders`,
> qui signe en plus un **digest d'`api_key`** car le corps multipart streamé n'est
> pas signable). Le futur `computeHmacHex` doit couvrir ce cas « digest d'api_key ».

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
| **uploadphotosserver** | mono-boucle → deep sleep (réveil timer, CAM) | **Le plus large et le plus discipliné** : `n3_wifi`, `n3_data`, `n3_hmac`, `n3_time`, `n3_ota` (**multi-cible déjà consommé**), `n3_mail`/`n3_notify`. **En avance** sur n3pp/msp : adopte déjà `n3PrintWakeupReason`. |
| **ffp5cs** | FreeRTOS multi-tâches, light sleep | **Très partiel** : seulement `n3_notify` (taxonomie sévérité), `n3_mail` (session SMTP), `n3_analog_sensors` (luminosité). Tout le reste réimplémenté (par choix d'archi async). |

**Deux cohortes, pas un continuum.** La famille se scinde par **modèle
d'exécution**, ce qui commande ce qui est mutualisable :

- **Cohorte deep-sleep** — n3pp, msp, **uploadphotosserver**, poissonglouton :
  squelette de cycle **identique** (`réveil → wifi → temps → config distante →
  capteurs/capture → POST → OTA → mail → deep sleep`), état en `RTC_DATA_ATTR`,
  harnais OTA périodique **triplé** (n3pp/msp/upload), sondage de commandes
  distantes. **C'est le vrai terrain du framework `n3_app`.**
- **Cohorte FreeRTOS** — ffp5cs seul : async, mutex TLS, files SD/NVS, light
  sleep. Il **contribue vers le haut** (logique pure, primitives, robustesse) mais
  **ne rejoint pas** `n3_app` (paradigme incompatible).

**Observation clé, révisée.** Le « meilleur » n'est pas monopolisé par ffp5cs :
- ffp5cs domine la **logique pure**, la **robustesse capteur**, l'**OTA
  multi-artefacts**, la **sécurité** (throttle, anti-rejeu).
- **uploadphotosserver** domine l'**offline-first deep-sleep** : upload multipart
  **en streaming** (zéro malloc du corps), **file SD/NVS peek-commit** (aucune
  donnée perdue), **batching/pacing de drain** (429-aware), **retry mail persisté
  RTC**, **TZ POSIX inconditionnel**. Briques **absentes de shared**.
- n3pp est le meilleur *modèle de référence* du cycle deep-sleep classique (capteur
  + batterie + OLED), msp en est le jumeau à harmoniser.

Le core de famille se construit donc en **prenant le meilleur de chaque** :
primitives & pur ← ffp5cs ; offline-first & upload ← uploadphotosserver ; squelette
deep-sleep ← n3pp. Voir la **Partie II** pour la matrice « meilleur-de-la-famille »
et le plan révisé.

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

> **✅ Livré (tranche additive, non-régressive par construction)** : les 6 briques
> sont mutualisées dans `shared/n3_time/src/` (`n3_epoch_util.h`,
> `n3_clock_decision.h`, `n3_uptime_format.h`) et `shared/n3_common/src/`
> (`n3_sleep_decision.h`, `n3_reset_reason.h`, `n3_login_throttle.h`), logique
> **octet-identique** aux originaux ffp5cs, **namespaces conservés** (`EpochUtil`,
> `ClockDecision`, `SleepDecision`, `ResetReason`, `UptimeFormat`, `LoginThrottle`)
> pour que la migration ffp5cs ne soit qu'un changement de ligne `#include`. Les 6
> suites Unity d'origine sont portées dans `shared/tests_native/test/` et branchées
> en CI. Migration des consommateurs (ffp5cs dédup + adoption n3pp/msp/upload) =
> tranches suivantes, vérifiées une à une.
>
> **✅ L1b livré (ffp5cs dédupliqué)** : ffp5cs consomme désormais les 6 headers
> shared et ses 6 copies locales (`ffp5cs/include/*.h`) sont **supprimées**.
> Includes repointés dans `power.cpp`, `mailer.cpp`, `diagnostics.cpp`,
> `automatism/automatism_sleep.cpp`, `web_server.cpp` ; les 6 tests natifs ffp5cs
> repointés + `-I ../shared/n3_time/src` / `-I ../shared/n3_common/src` ajoutés à
> `ffp5cs/platformio-native.ini`. Logique octet-identique (garde-fou : les suites
> natives ffp5cs, inchangées, sont le test de non-régression en CI ; `--gc-sections`
> élimine le `n3_time.cpp` non référencé nouvellement tiré). ffp5cs 15.12 → 15.13.
> Reste : adoption par n3pp/msp/upload (L1c) là où c'est un gain.

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

---

# Partie II — Plan consolidé de famille (révisé, 4 firmwares + veille externe)

Cette partie **révise et supersède** la feuille de route ci-dessus après
intégration de `uploadphotosserver` et d'une **veille des pratiques éprouvées**
(ESPHome, Tasmota, docs Espressif/ESP-IDF, ArduinoJson). Principe : **prendre le
meilleur de chaque firmware** pour un socle sain, fonctionnel, robuste.

## II.1 — Inspirations externes (valident le socle, révèlent 2 manques)

La veille **valide** les libs existantes (`n3_wifi` scan RSSI, `n3_hmac` mbedTLS,
`n3_ota` sha256+ECDSA, `n3_sleep`, `n3_data`) et confirme les principes déjà
appliqués (zéro `String` en chemin chaud, ArduinoJson local/filtré, état RTC).
Elle apporte des patterns actionnables et pointe **deux manques du core** :

| Source | Pratique retenue | Impact sur le core |
|---|---|---|
| ESPHome (`Component`/`PollingComponent`, `setup()`→`update()` cadencé, `setup_priority`, `loop()` non bloquant) | Contrat de cycle de vie à callbacks pour `n3_app` | Structure `n3_app` (voir II.4) |
| Tasmota (`Xsns<ID>(callback_id)` : 1 callback + enum d'étape) | Dispatch capteurs léger sans vtable | Modèle d'itération capteurs de `n3_app` |
| arduino-esp32 `WiFi.onEvent()` | Reconnexion **événementielle** (pas de polling `WiFi.status()`) | Option future `n3_wifi` (cohorte FreeRTOS surtout) |
| Backoff exponentiel **borné + jitter** (AWS/ClearBlade) | `min(base·2^n + rand, plafond)`, reset au succès | `n3_data` / file offline / reconnexion |
| ESP-IDF OTA rollback (`esp_ota_mark_app_valid_cancel_rollback`, `IMG_PENDING_VERIFY`) | **Validation au 1er boot + rollback auto** | **MANQUE 1** → enrichir `n3_ota` |
| store-and-forward MQTT / EMS-ESP32 | File offline plafonnée + drop-oldest + watchdog offline | **MANQUE 2** → nouvelle lib `n3_store_forward` |
| ESP-IDF `esp_log` / `CORE_DEBUG_LEVEL` | Niveaux + tag/module, coût réglé au build | Cible de `n3_log` (mapper sur `esp_log`, pas réinventer) |
| Deep sleep : `RTC_NOINIT_ATTR` + **magic-number** | Valider l'état RTC avant de s'y fier | Durcir tous les états `RTC_DATA_ATTR` de la cohorte |
| Espressif brownout / inrush | Condensateur, `WiFi.setTxPower`, sérialiser radio↔capteurs | Doc `n3_app` (déjà vécu par uploadphoto) |

> **Les deux manques (file offline + rollback OTA) sont précisément ce que la
> famille a déjà partiellement résolu en interne** : uploadphotosserver a la file
> offline (SD/NVS peek-commit), et le rollback est le seul angle mort OTA commun.
> La veille ne demande donc pas d'importer du code externe, mais de **remonter
> l'existant maison** + ajouter le rollback.

## II.2 — Matrice « meilleur de la famille » par préoccupation

Pour chaque préoccupation du core : qui a la **meilleure** implémentation
aujourd'hui (la référence à promouvoir), et le sens du mouvement.

| Préoccupation | Meilleur actuel | Pourquoi | Mouvement vers le core |
|---|---|---|---|
| **Logique pure** (epoch, drift, sleep-calc, reset, uptime, throttle) | **ffp5cs** (headers purs testés) | Déjà extraits, 0 dépendance | Remonter → `n3_common`/`n3_time` (Couche 0) |
| **WiFi scan/RSSI** | **shared `n3_wifi`** (+ superset AP/captive ffp5cs gardé) | Déjà mutualisé ; upload ajoute `wifiRadioResetForWake` (spécifique CAM) | Tous **consomment** `n3_wifi` ; garder les surcouches |
| **HMAC canonique** | **ffp5cs** (`hmac_sign`, mbedtls sans `String`) | Sans heap ; nonce aléatoire (anti-rejeu) | Promouvoir → lib dédiée sans HTTPClient ; **3** sites la consomment (+ variante digest api_key d'upload) |
| **POST données URL-encoded** | **shared `n3_data`** | Déjà commun (+ HMAC + stats) | Consommé par n3pp/msp/upload |
| **Upload gros binaire (multipart streaming)** | **uploadphotosserver** (`MultipartCameraStream/FileStream`) | Poste `head+JPEG+tail` **sans malloc du corps**, chunké SD | Promouvoir → **nouvelle brique `n3_upload`** |
| **File offline / store-and-forward** | **uploadphotosserver** (SD/NVS peek-commit) + ffp5cs (`web_client_queue` NVS) | Aucune donnée perdue, deep-sleep-native, **numéro non brûlé** sur échec | Promouvoir → **nouvelle lib `n3_store_forward`** (comble MANQUE 2) |
| **Robustesse capteur (état inter-lecture)** | **ffp5cs** (`sensor_failure_manager`, `reading_fallback`) | Machine d'état désactivation/réactivation | Remonter → `n3_analog_sensors` (Couche 2) |
| **Mail : cœur SMTP** | **shared `n3_mail`** | Session + `n3MailBuild*Body` + `n3Notif*` | Déjà commun |
| **Mail : orchestration failover** | **uploadphotosserver** (retry persisté **RTC** + cap piloté par `serverExchangeOk`) | Ne perd pas une alerte à travers le deep sleep | Modèle de référence pour `n3MailNotify` (Couche 3, enrichi) |
| **Temps / NTP / RTC** | **shared `n3_time`** + **TZ POSIX inconditionnel d'upload** + drift ffp5cs | Corrige le piège newlib/IANA même hors WiFi | `n3_time` + adopter le pattern TZ + `clock_decision` |
| **OTA vérif (sha256+ECDSA)** | **shared `n3_ota`** | Déjà commun et aligné ESP-IDF | Base |
| **OTA multi-artefacts (`env×modèle`)** | **ffp5cs** (`ota_artifact_select`) | Cascade de fallback | Remonter → `n3_ota` |
| **OTA rollback / 1er boot** | **personne** | Angle mort commun | **Ajouter** (veille, MANQUE 1) → `n3_ota` |
| **Harnais OTA périodique (timer 2 h RTC + UI)** | **n3pp** (référence) = msp = upload (**triplé**) | Verbatim sur 3 firmwares deep-sleep | Promouvoir → `n3_ota_ui` (Couche 3) |
| **Deep sleep** | **shared `n3_sleep`** (à étendre timer-only) | upload/n3pp/msp réimplémentent le cœur timer | Étendre `n3_sleep` (mode timer sans GPIO) + magic-number |
| **Wakeup reason** | **shared `n3_time:104`** (upload le consomme déjà !) | upload **en avance** ; n3pp/msp en retard | n3pp/msp **adoptent** l'existant (A2) |
| **Stats réseau** | **shared `n3_net_stats`** | Riche (durée, RSSI, near-timeout) | Brancher partout — **y compris la voie upload photo** (angle mort) ; thread-safe pour ffp5cs |
| **Logging** | **ffp5cs** (`log.h` à niveaux) | n3pp/msp/upload = `Serial.print` bruts | Promouvoir → `n3_log` mappé `esp_log` |
| **Config : parseur de clés serveur** | **chacun le sien** | **Collisions** (n3pp 104/105 ↔ msp servo ; upload 102-106 propres) | **NE PAS mutualiser** le parseur ; seules les clés communes 100/101/103/106/107/110/112 (hors upload) |
| **Cycle de vie applicatif** | **n3pp** (le plus complet de la cohorte) | Squelette deep-sleep de référence | Étoile polaire `n3_app` (cohorte deep-sleep) |

## II.3 — Le core cible en 2 anneaux

```
        ┌──────────────────────────────────────────────────────┐
        │   ANNEAU 1 — Primitives universelles (les 4 + pgl)    │
        │   pur : epoch/clock/sleep-calc/reset/uptime/throttle  │
        │   sécu : n3_hmac (canonique sans String)              │
        │   data : n3_data + n3_net_stats + n3_upload           │
        │   robustesse : n3_store_forward · sensor_failure_mgr  │
        │   plateforme : n3_wifi · n3_time · n3_sleep · n3_ota  │
        │                n3_mail/n3_notify · n3_display · n3_log │
        └──────────────────────────────────────────────────────┘
                              ▲ consommé par
        ┌─────────────────────┴───────────┐   ┌──────────────────┐
        │  ANNEAU 2 — n3_app (deep-sleep) │   │  ffp5cs (FreeRTOS)│
        │  n3pp · msp · upload · pgl       │   │  garde son archi  │
        │  cycle réveil→…→sleep à callbacks│   │  consomme Anneau 1│
        └──────────────────────────────────┘   └──────────────────┘
```

**Anneau 1** = tout le monde (y compris ffp5cs) consomme. **Anneau 2** = le
framework applicatif `n3_app`, réservé à la **cohorte deep-sleep** ; ffp5cs reste
sur son ordonnancement FreeRTOS et ne consomme que l'Anneau 1.

## II.4 — Nouvelles briques core (issues de la famille)

1. **`n3_upload`** (source : uploadphotosserver) — POST multipart d'un binaire via
   une classe `Stream` (head+corps+tail sans malloc du corps), lecture chunkée,
   retry/backoff, conscience 429. Utile à tout envoi de gros blob (photo, log SD).
2. **`n3_store_forward`** (sources : upload SD/NVS peek-commit + ffp5cs
   `web_client_queue`) — file offline **durable** avec curseur, sémantique
   **peek → commit** (aucun élément brûlé sur échec), plafond + drop-oldest,
   pacing/budget-temps de drain. Comble le MANQUE 2 de la veille. Backend de
   stockage **abstrait** (SD ou NVS) pour servir les deux cohortes.
3. **`n3_ota` enrichi** — ajouter (a) `ota_artifact_select` multi-cible (ffp5cs),
   (b) **rollback + validation au 1er boot** (veille, MANQUE 1 :
   `esp_ota_mark_app_valid_cancel_rollback` après auto-test WiFi+serveur).
4. **`n3_app`** (source : n3pp) — orchestrateur de cycle deep-sleep à callbacks,
   façon ESPHome adapté au réveil-unique : le firmware fournit
   `{onWake, readSensorsOrCapture, buildPayload, applyRemoteConfig, onSleep}` ; le
   framework gère wifi/temps/POST/stats/OTA-périodique/mail/sleep. **Dépend** de
   l'harmonisation préalable des divergences RISQUÉ (A6/A7/A8/A10).

## II.5 — Plan révisé par lots (ordre = risque croissant)

Chaque lot indique la **cohorte bénéficiaire** et le **niveau de risque** (repris
de la vérification adversariale). Les lots 1-4 sont **sans risque de
fonctionnement** ; les suivants exigent des préalables explicites.

| Lot | Contenu | Source→Cible | Cohorte | Risque |
|---|---|---|---|---|
| **L1 — Socle pur** ✅ *en cours* | epoch_util, clock_decision, sleep_decision, reset_reason, uptime_format, login_throttle | ffp5cs → `n3_common`/`n3_time` | Toutes | **Nul** |
| **L2 — Primitives** | `computeHmacHex` (sans String, +digest api_key, nonce aléatoire) ; `n3DataSendHeartbeat` ; `n3TimeSyncBrokenDown` ; adoption `n3PrintWakeupReason` par n3pp/msp | ffp5cs+n3pp/msp → `n3_hmac`(dédiée)/`n3_data`/`n3_time` | Toutes | **Faible** |
| **L3 — Robustesse capteurs** | sensor_failure_manager (retirer config.h), sensor_reading_fallback (renommer) | ffp5cs → `n3_analog_sensors` | Deep-sleep + ffp5cs | **Faible** |
| **L4 — Offline-first & stats** | `n3_upload` (multipart streaming) ; `n3_store_forward` (peek-commit) ; brancher `n3_net_stats` **y compris voie upload** ; brancher ffp5cs **sous `s_httpMutex`** | upload+ffp5cs → nouvelles libs / `n3_data` | Toutes | **Faible** (ffp5cs : garde mutex) |
| **L5 — Orchestration** | `n3MailNotify` (enrichi du retry-RTC d'upload) ; `n3_ota_ui` (harnais triplé) ; `ota_artifact_select` multi-cible ; **rollback OTA 1er boot** | famille → `n3_mail`/`n3_ota` | Deep-sleep surtout | **Faible/Modéré** (parité ArduinoJson v6/v7/toolchains) |
| **L6 — Logging** | `n3_log` mappé `esp_log`, découplé de `config.h` ; migrer `Serial.print` firmware par firmware | ffp5cs → `n3_log` | Toutes | **Faible sémantique, gros volume** |
| **L7 — Framework** | `n3_app` (cycle deep-sleep à callbacks) | n3pp → `n3_app` | Deep-sleep | **Élevé** — **préalable : harmoniser A6/A7/A8/A10** |

**Hors périmètre (confirmé)** : `web_client`/`net_request_pool`/`ffp3_post_body`,
`ota_manager` (téléchargement resumable), `power` (light sleep), `nvs_manager`,
`wifi_manager` (surcouche AP/S3) de ffp5cs ; **parseurs de clés serveur** (collisions).

## II.6 — Précautions ajoutées par cette révision

- **Trois toolchains, pas deux** : WROOM pioarduino 3.3.x (n3pp/msp/ffp5cs-WROOM),
  S3 espressif32 6.13.0 (ffp5cs-S3), **et esp32cam espressif32 6.13.0 2.0.x**
  (uploadphoto). Toute brique de l'Anneau 1 doit compiler sous les **trois**
  (piège `ESP32Servo`/ArduinoJson v6/v7).
- **PSRAM** : ne jamais se fier à `psramFound()` (faux sur esp32cam) — utiliser
  `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`. Toute lib supposant `psramFound()`
  casserait la CAM.
- **Budgets pile/DRAM hétérogènes** : ffp5cs WROOM ~99,9 % DRAM ; uploadphoto exige
  `CONFIG_ARDUINO_LOOP_STACK_SIZE=32768` (OTA sha256 + TLS SMTP + framebuffer). Une
  brique Anneau 1 doit rester frugale en pile **et** en heap (zéro `String` chaud).
- **N3NetStats — angle mort upload** : la voie d'upload photo court-circuite
  `n3DataPost` et **échappe aux stats** ; brancher `n3_upload` sur `n3_net_stats`
  en même temps (L4).
