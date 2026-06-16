# Backlog bench-gated : concurrence `s_lastFetchedJson` & unification OTA

> Items issus de l'audit qui **ne doivent PAS être appliqués en validation compile-seule** :
> ils touchent des chemins critiques (reboots / flash OTA) dont la justesse runtime exige
> un **banc réel**. Patchs/designs vérifiés ci-dessous, prêts pour une session avec matériel.
> Le serveur (`n3_serveur`) supporte désormais HTTP Range (206) — pré-requis de l'unification OTA.

---

## 1. `s_lastFetchedJson` — race + référence pendante (cause probable de reboots #2)

### Constat (web_client.cpp)
- `s_lastFetchedJson` (`char[]`, ligne 65) écrit par **netTask** (`fetchRemoteState`, lignes 742-744)
  et lu par **autoTask** via `copyLastFetchedTo` (ligne 758, `deserializeJson`), **sans verrou**.
- **Piège supplémentaire (sous-estimé)** : `deserializeJson(doc, s_lastFetchedJson)` reçoit un
  `char*` **mutable** → ArduinoJson opère en **zero-copy** : `doc` conserve des **pointeurs vers
  le buffer** après le retour de `copyLastFetchedTo`. Donc `doc`, utilisé ensuite par autoTask,
  référence toujours `s_lastFetchedJson` que netTask peut réécrire → `LoadProhibited` (le code le
  note lignes 64/721/801).
- ⚠️ Un simple mutex autour du `deserializeJson` **ne suffit pas**, et copier vers un buffer
  **local** (proposé par une analyse) est **faux** (le `doc` pointerait vers un buffer détruit au
  retour) — et de toute façon impossible sur la pile d'autoTask (HWM ~95 %, 976 o libres).

### Correctif retenu (mutex dédié + désérialisation en mode COPIE)
1. Ajouter `static SemaphoreHandle_t s_lastFetchedJsonMutex = nullptr;` près de `s_httpMutex`
   (ligne ~23), créé paresseusement (même motif que `s_httpMutex`). `<freertos/semphr.h>` déjà inclus.
2. **Écriture** (742-744) : entourer le `memcpy` + `s_lastFetchedSize = jsonLen` de
   `xSemaphoreTake/Give(s_lastFetchedJsonMutex, …)`.
3. **Lecture** (`copyLastFetchedTo`) : sous le mutex, désérialiser en **forçant la copie** :
   `deserializeJson(doc, (const char*) s_lastFetchedJson);` — le cast en `const char*` fait
   dupliquer les chaînes dans le pool de `doc` (élastique en ArduinoJson 7) → `doc` devient
   **indépendant** du buffer. Relâcher le mutex juste après ; l'unwrap outputs/switches opère
   ensuite sur un `doc` autonome.
   - Pas de gros buffer sur la pile (on tient le mutex pendant la désérialisation ~ms ; seul le
     `memcpy` d'écriture de netTask est brièvement bloqué — acceptable, écriture peu fréquente).
   - Ordre de verrous sûr : `copyLastFetchedTo` ne prend QUE `s_lastFetchedJsonMutex` (jamais
     `s_httpMutex`) → pas d'inversion avec le wrapper `fetchRemoteState` (qui prend `s_httpMutex`).

### À valider sur banc
- Confirmer disparition des `LoadProhibited` sous toggle WiFi + polling auto concurrent (2-4 h).
- Vérifier l'impact heap du mode copie (transitoire ~1,5 Ko) — négligeable attendu.
- Confirmer qu'aucun appelant ne dépendait du zero-copy (revue : seuls autoTask/netTask-boot).

---

## 2. Unification des 3 modes de download OTA (audit S3)

### Constat
Trois implémentations en cascade (`ota_manager_download.cpp` / `_alt.cpp`) :
`downloadFirmwareModern` (esp_http_client) → `downloadFirmware` (HTTPClient) →
`downloadFirmwareUltraRevolutionary` (micro-chunks 2 Ko). Le mode micro-chunks **n'aide pas**
contre la latence o2switch (front-loadée à la connexion, pas au streaming).

### Design retenu (téléchargement adaptatif **résumable** unique)
- **Un seul chemin** (recommandé : `esp_http_client`), avec **reprise via HTTP Range** :
  sur coupure, rouvrir avec `Range: bytes=<octets_écrits>-`, attendre **206** et poursuivre
  l'écriture flash ; gérer **200** (serveur ignore Range → redémarrage propre depuis 0) et **416**
  (erreur fatale). Le serveur supporte maintenant 206 (cf. `OtaFileController` côté n3_serveur).
- Conserver : validation **taille + MD5** de bout en bout, **watchdog** (`esp_task_wdt_reset`) dans
  la boucle de lecture, partition cible sauvegardée avant `Update.begin()`, `esp_ota_set_boot_partition`.
- Retries bornés avec backoff exponentiel ; timeout global 5 min conservé.
- `updateTask()` : remplacer la cascade Modern→Classic→Ultra par un appel unique ;
  **supprimer** `downloadFirmwareUltraRevolutionary` (et l'un des deux chemins HTTP).

### Point dur — MD5 sur reprise
`Update.setMD5()` calcule le MD5 du **flux écrit**. Si l'on reprend à l'octet N en ne streamant
que N..EOF, le contexte MD5 d'`Update` ne correspondra pas au MD5 du fichier complet. **À vérifier
sur banc** : comportement exact d'`Update` sur reprise (réinitialise-t-il le contexte ?). Sinon,
n'autoriser la reprise que si la cohérence MD5 complète est garantie, ou valider via un hash applicatif.

### À valider sur banc (obligatoire avant merge)
Reprise à 10/50/90 %, coupures multiples, serveur renvoyant 200 au lieu de 206, 416, watchdog sous
réseau lent, MD5 corrompu → échec propre sans flash partiel. Rollout progressif (beta → canary → prod)
avec plan de rollback (revert = ré-activer la cascade). **Ne pas merger sans banc.**

---

## 3. Autres items bench-gated (rappel)
- Réduction de profondeur de pile `autoTask`/`netTask` (déporter gros `JsonDocument` locaux en statique/BSS).
- God-class `automatism` : extraire état + interface `IActuators`.
- Bornage du `portMAX_DELAY` du mutex HTTP (`web_client.cpp:116`) — comportement timeout à valider.

---

## 4. Mise à jour 2026-06-14 (analyse multi-agents)

### Appliqué (concurrence, atomic — strictement plus sûr, compile-validé CI)
- **`net_request_pool.cpp`** : `s_netRequestPoolUsed[]` (bool) → `std::atomic<bool>` + `compare_exchange_strong`
  dans `netRequestAllocTrySlot`. Corrige une **vraie course alloc inter-tâches** (check-then-set ; l'accès
  parallèle netTask/auto/web est déjà documenté dans le code, cf. `app_tasks.cpp:370`) → évitait deux tâches
  prenant le même slot.
- **`app_tasks.cpp`** : `s_heartbeatDroppedCount` (uint32_t) → `std::atomic<uint32_t>` (compteur diag multi-tâches).

### Investigué mais NON appliqué — pièges identifiés
- **Bornage mutex HTTP** : déjà borné (tous les `xSemaphoreTake` de `web_client.cpp` utilisent
  `pdMS_TO_TICKS(...)`, aucun `portMAX_DELAY`). **Rien à faire.**
- **Compteur HTTP `diagnostics.cpp`** : passer les membres `httpSuccessCount/FailCount` en `atomic` casserait
  la copiabilité de `DiagnosticStats` (struct copiée en de nombreux endroits) → **trop risqué pour un compteur
  diag, écarté**.
- **Profondeur de pile (static-conversion)** : ⚠️ les plus gros candidats sont **multi-tâches** — les rendre
  `static` introduirait une **NOUVELLE course** (pire que la pile économisée) :
  - `AutomatismSync::processFetchedRemoteConfig` (3× `JsonDocument` ≈ 6 Ko) — appelé par netTask
    (`app_tasks_net.cpp:86`) **ET** autoTask (`automatism.cpp:269`, `automatism_sleep.cpp:477`).
  - `WebClient::copyLastFetchedTo` (`tmp` ≈ 1 Ko) — appelé par netTask-boot (`app_tasks_net.cpp:85`) **ET**
    autoTask (`automatism_sync.cpp:527`).
  → La vraie solution reste de **réduire la profondeur d'appel** (refactor), validée sur banc. Ne PAS
    static-iser ces buffers sans verrou.

### OTA résumable — impl complète rédigée, BENCH-REQUISE
Une implémentation complète `downloadFirmwareAdaptiveResumable` (esp_http_client + Range 206 + backoff) a été
produite. **Point dur non résolu sans banc** : le contexte MD5 d'`Update` sur reprise — si le serveur renvoie
206 partiel, le hash du flux N..EOF doit correspondre au MD5 du fichier complet, ce qui **n'est pas garanti** et
DOIT être validé sur matériel (sinon flash corrompu). Stratégie hybride retenue (206→continuer, 200→redémarrer
propre, 416→fatal) mais **à prouver sur banc avant tout merge**.

---

## 5. Mise à jour 2026-06-15 (chantiers C2/C3/C4 — PR draft bench-gated)

### C3 — Réduction profondeur de pile (PR draft, CI-validable, **HWM banc-requise**)
Appliqué : déplacement des gros buffers de travail **de la pile vers le heap local** (alloués/libérés à
chaque appel via `std::unique_ptr` + `new(std::nothrow)`), **sans** static-isation (la course était le piège
identifié au §4). Fonctions multi-tâches concernées :
- `AutomatismSync::processFetchedRemoteConfig` (`automatism_sync.cpp`) : `docJson[2048]` + `inputCopy`
  (`StaticJsonDocument<2048>`) + `normalizedDoc` (`StaticJsonDocument<2048>`) + `jsonStr[REMOTE_JSON_CACHE_SIZE]`
  ≈ 6-8 Ko regroupés dans une struct `ProcessBuffers` allouée en heap. Dominant de la pile autoTask (HWM ~95%).
- `WebClient::copyLastFetchedTo` (`web_client.cpp`) : `tmp` (`StaticJsonDocument<JSON_DOCUMENT_SIZE>`, jusqu'à
  4 Ko sur S3) déplacé en heap dans la branche unwrap.

Comportement inchangé ; dégradation gracieuse sous OOM (`new(std::nothrow)`→nullptr→`return false`, retry au
cycle suivant) au lieu d'un débordement de pile. **À valider sur banc** : `uxTaskGetStackHighWaterMark`
avant/après (DoD : marge autoTask/netTask > 20 %), 0 stack-overflow, et impact heap des allocations transitoires
(~8 Ko) sous polling soutenu + faible heap. CI : builds wroom + S3 valident la compilation.

---

## 6. Mise à jour 2026-06-15 — C2 unification OTA (PR draft, **BENCH-REQUISE**)

### Implémenté (CI-validable, NON mergeable sans banc)
- Cascade `downloadFirmwareModern → downloadFirmware → downloadFirmwareUltraRevolutionary` **supprimée**.
  Remplacée par **un seul chemin** `OTAManager::downloadFirmwareAdaptiveResumable` (esp_http_client),
  + helper `openFirmwareConnection(url, rangeStart, &status)` qui pose l'en-tête `Range` si reprise.
- `updateTask` n'appelle plus que ce chemin unique (rollback = revert de la PR → ré-active la cascade).
- `downloadFirmwareUltraRevolutionary` (micro-chunks) **retiré** de `ota_manager_download_alt.cpp`
  (seul `downloadFilesystem` y reste).
- Stratégie reprise : **206**→poursuivre la MÊME session `Update` (offset = octets écrits) ;
  **200**→`Update.abort()`+`begin()` puis ré-écriture depuis 0 ; **416**→fatal. Backoff exponentiel
  plafonné (`OTA_RESUME_*` dans `config_network.h`), timeout global 5 min, feed TWDT dans la boucle.
- **Garde-fou MD5** : la partition boot n'est marquée (`esp_ota_set_boot_partition`) **que** si
  `Update.end()` (qui vérifie taille + MD5 du flux complet) réussit. Donc même si la logique de reprise
  était subtilement fausse, un flash corrompu ne sera **jamais** booté.
- **Bug latent corrigé** : l'ancien `downloadFirmwareModern` comparait le retour de
  `esp_http_client_fetch_headers()` (= content-length) au code 200 ; le nouveau code lit le code via
  `esp_http_client_get_status_code()`.

### À VALIDER SUR BANC (bloquant avant merge) — WROOM **et** S3
- Continuité réelle du contexte MD5 d'`Update` entre fragments 206 contigus (point dur §2/§4).
- Reprise à 10/50/90 %, coupures multiples, serveur renvoyant 200 au lieu de 206, 416, réseau lent
  (watchdog), MD5 corrompu → **échec propre sans flash partiel booté**.
- Rollout progressif beta→canary→prod, plan de rollback = revert de la PR (ré-active la cascade).
- NB : on pourrait renforcer en parsant `Content-Range` (offset renvoyé == offset demandé) via un
  handler d'en-tête — non fait ici (le garde-fou MD5 final couvre déjà la corruption). À évaluer au banc.
---

## 7. Mise à jour 2026-06-15 — C4 découpe god-class (PR draft, CI-verte)

### C4 — Casser la god-class `automatism` (1re étape sous filet de tests, CI-verte)
Première extraction (petite étape, comportement strictement identique, validée nativement) :
- Nouveau module **pur** `include/automatism/flood_alert.h` (`FloodAlert::evaluate` + `markEmailSent`) :
  machine d'état anti-spam « aquarium trop plein » (debounce + cooldown + hystérésis de sortie). Aucune
  dépendance Arduino/config → testable en `g++`.
- `Automatism::handleAlerts` (`automatism_display.cpp`) délègue la **décision** à `FloodAlert::evaluate` ;
  les **effets de bord** (email, NVS, blink OLED, flag `_highAquaSent`) restent dans l'appelant.
- Suite de tests native `test/test_flood_alert/` (10 cas : debounce, cooldown, 1er email, mail désactivé,
  sortie d'hystérésis, zone morte, reset compteurs) ajoutée à `firmware-ci.yml` **et** `platformio-native.ini`.

DoD partielle atteinte : logique extraite + testée, **aucune régression de comportement** (parité ligne à ligne
avec l'ancien bloc inline ; vérifiée en local `-Wall -Wextra`). Reste bench-gated pour les chemins matériels
non touchés ici (nourrissage/pompe/chauffage). Étapes suivantes proposées : extraire l'interface `IActuators`
et les contrôleurs refill/display restants — **par petites étapes**, chacune CI-verte.

#### C4 — 2e étape : extraction `ReservoirLowSecurity` (debounce + hystérésis « réserve basse »)
Deuxième petite extraction, même patron que `FloodAlert`, **comportement strictement identique** :
- Nouveau module **pur** `include/automatism/reservoir_low_security.h` (`ReservoirLowSecurity::evaluate`) :
  machine de debounce (2 mesures pour verrouiller, 3 pour déverrouiller) + hystérésis + zone morte.
  Aucune dépendance Arduino/config → testable en `g++`.
- `Automatism::handleRefillReservoirLowSecurity` (`automatism_refill.cpp`) délègue la **décision** au module ;
  les **effets de bord** (arrêt pompe, email, flags `_emailTank*`, logs, motif de verrou) restent dans l'appelant.
- **Smell god-class supprimé** : les 2 `static` locaux cachés (`aboveCount`/`belowCount`, état global masqué)
  deviennent un membre explicite `_reservoirLowState` (mono-tâche autoTask, pas d'atomic requis).
- Suite native `test/test_reservoir_low_security/` (11 cas : debounce verrou/déverrou, re-verrou bloqué,
  zone morte + bornage, plafond compteur, resets croisés, cycle complet) ajoutée à `firmware-ci.yml` **et**
  `platformio-native.ini`. Parité vérifiée en local (`g++ -Wall -Wextra`, 20/20 checks).

⚠️ **Banc requis pour le chemin pompe** : la *décision* est CI-testée, mais l'intégration touche
`handleRefill` (arrêt pompe réservoir) — à valider sur banc avant rollout (verrou/déverrou réels,
non-régression réserve basse → pompe à sec évitée).

#### C4 — 3e étape : extraction `HeaterControl` (régulation chauffage par hystérésis)
Troisième petite extraction, même patron, **comportement strictement identique** :
- Nouveau module **pur** `include/automatism/heater_hysteresis.h` (`HeaterControl::evaluate`) :
  décision ON/OFF du chauffage (ON si temp < seuil ; OFF si temp > seuil + 2 °C ; bande morte sinon).
  **Sans état** (heaterPrevState reste dans l'appelant). Aucune dépendance Arduino/config → testable `g++`.
- `Automatism::handleAlerts` (`automatism_display.cpp`) délègue la **décision** au module ;
  les **effets de bord** (relais `startHeater`/`stopHeater`, email ON/OFF, blink, flag `heaterPrevState`)
  restent dans l'appelant.
- Suite native `test/test_heater_hysteresis/` (9 cas : ON sous seuil, pas de re-déclenchement, OFF au-dessus
  de seuil+hyst, bornes strictes, bande morte montante/descendante, cycle complet) ajoutée à `firmware-ci.yml`
  **et** `platformio-native.ini`. Parité vérifiée en local (`g++ -Wall -Wextra`, 10/10 checks).

⚠️ **Banc requis pour le chemin chauffage** : la *décision* est CI-testée, mais l'intégration touche le
relais de chauffage — à valider sur banc avant rollout (allumage/extinction réels, pas de battement relais).

#### C4 — 4e étape : extraction `LevelAlert` (alerte niveau seuil+hystérésis, factorisée aqua+réserve)
Quatrième petite extraction + **déduplication** (deux blocs d'alerte quasi-identiques) :
- Nouveau module **pur** `include/automatism/level_alert.h` (`LevelAlert::evaluate`) : décision Raise/Clear
  d'une alerte « niveau bas » par seuil + hystérésis. **Sans état** (flag `_lowAquaSent`/`_lowTankSent`
  reste dans l'appelant). Aucune dépendance Arduino/config → testable `g++`.
- `Automatism::handleAlerts` (`automatism_display.cpp`) utilise le module pour **les deux** alertes
  (aquarium bas et réserve basse). Effets de bord conservés et **distincts** : l'aquarium reset en silence,
  la réserve envoie un mail « Réserve OK » — gérés dans l'appelant.
- Suite native `test/test_level_alert/` (9 cas : Raise/None/Clear, bande morte, bornes strictes, cycle)
  ajoutée à `firmware-ci.yml` **et** `platformio-native.ini`. Parité vérifiée en local (`g++ -Wall -Wextra`,
  10/10 checks). Aucun effet de bord matériel touché (email/blink uniquement) → **pas de banc requis**.

---

## 8. Mise à jour 2026-06-15 — C4 inversion de dépendance `IActuators` (PR draft)

Étape **structurante** du découpage god-class (réf. §3 « extraire état + interface `IActuators` ») : on casse
le couplage direct `Automatism` → matériel concret `SystemActuators`.

### Implémenté (CI-validable, behavior-preserving)
- Nouvelle interface **`include/iactuators.h`** : surface MINIMALE (16 méthodes) effectivement consommée par le
  module `automatism` (vérifié par grep exhaustif des `_acts.`/`acts.`). Les stats pompe (`getTankPump*`),
  non consommées par Automatism, restent hors interface sur le concret.
- `SystemActuators` **implémente** `IActuators` (héritage + `override`). Strictement additif : tous les autres
  consommateurs (`web_server`, `app_context`, `realtime_websocket`, global `acts`) continuent d'utiliser le
  concret. Aucune agrégation-init / POD cassée (vérifié).
- **Tous** les passages d'actionneur du module `automatism` retypés `SystemActuators&` → `IActuators&` :
  `Automatism::_acts` (membre + ctor), `prepareActuatorsForSleep`/`restoreActuatorsAfterWake`,
  `AutomatismSync::update`/`sendFullUpdate`, `AutomatismSleep::handleAutoSleep`/`handleBlockingConditions`,
  `AutomatismFeedingSchedule` (ctor + membre). La conversion dérivée→base est implicite au site de construction
  (`app.cpp` passe le `SystemActuators` global). Comportement inchangé (dispatch virtuel sur le même objet).
- **Test double `FakeActuators`** + suite native `test/test_iactuators/` (5 cas : dispatch polymorphe,
  arguments par défaut via interface, bascule de chaque actionneur, `feedSequential`, destruction virtuelle).
  Enregistrée dans `firmware-ci.yml` **et** `platformio-native.ini`. Validé en local (`g++ -Wall -Wextra`, 18/18).
  → Débloque le **test natif de la logique extraite** des prochaines étapes (injection d'un faux acteur).

### ⚠️ Banc requis (chemins matériels)
La justesse runtime du **dispatch virtuel sur ESP32** (vtable, surcoût négligeable mais à confirmer) sur les
chemins nourrissage/pompe/chauffage/lumière n'est pas prouvée par la compilation. À valider sur banc avant
rollout. Aucun changement de logique — seulement le mécanisme d'appel.

### Étapes suivantes (proposées)
Extraire des contrôleurs (refill/feeding) en fonctions/objets prenant `IActuators&`, désormais **testables
nativement** avec `FakeActuators`, une petite étape CI-verte à la fois.

#### C4 — 1er contrôleur testé via `IActuators` : `ActuatorSnapshot` (veille/réveil)
Démonstration concrète de l'apport de l'interface : la logique d'actionneur de
`prepareActuatorsForSleep` / `restoreActuatorsAfterWake` est extraite en module **testable nativement**.
- Nouveau module `include/automatism/actuator_snapshot.h` : `capture()` (lit aqua/chauffage/lumière),
  `stopAll()` (coupe les 3 avant veille), `apply()` (restaure ceux actifs au réveil). Ne touche QUE
  `IActuators` ; la **persistance NVS** et les logs restent dans l'appelant.
- `prepareActuatorsForSleep`/`restoreActuatorsAfterWake` délèguent à ce module (parité ligne à ligne).
- Suite native `test/test_actuator_snapshot/` (6 cas : capture, lecture seule, stopAll ne touche pas la
  pompe réservoir, restauration sélective, snapshot vide no-op, cycle veille→réveil) avec un `FakeActuators`.
  Enregistrée CI + ini. Validé local (`g++ -Wall -Wextra`, 10/10). **C'est le premier morceau de logique
  d'actionneur couvert par des tests** — impossible avant l'interface.

⚠️ **Banc requis** : chemin veille/réveil réel (NVS + actionneurs) à confirmer au banc ; seule la logique
d'orchestration est CI-testée.

#### C4 — Contrôleur refill : décision de démarrage `RefillStart` (gating + blocage réserve basse)
Extraction du gating de `handleRefillAutomaticStart` en décision pure testée :
- Nouveau module `include/automatism/refill_start.h` (`RefillStart::evaluate`) : retourne `Start` /
  `BlockReserveLow` / `None` selon niveau aquarium/réserve, verrou, essais, mode manuel, pompe active.
  Aucune dépendance Arduino → testable `g++`.
- `handleRefillAutomaticStart` délègue la **décision** ; les **effets de bord** (startTankPump, verrou,
  email, `sendFullUpdate`, timers/countdown, logs) restent dans l'appelant (parité ligne à ligne).
- Suite native `test/test_refill_start/` (10 cas : start, blocage réserve basse, niveau inconnu, seuils
  stricts, verrou, essais épuisés, mode manuel, pompe déjà active). Enregistrée CI + ini. Local 10/10.
- **Chemin de sécurité couvert par des tests** : évite un démarrage pompe à sec (réserve basse) ou un
  remplissage refusé à tort — auparavant non testé.

⚠️ **Banc requis** : l'intégration touche le **démarrage réel de la pompe** ; à valider au banc (la décision
est CI-testée, mais le déclenchement matériel + email + sync ne le sont pas).

#### C4 — Contrôleur refill : décision de timing `RefillDuration` (anomalie / arrêt forcé)
Extraction de la décision de timing de `handleRefillMaxDurationStop` :
- Nouveau module `include/automatism/refill_duration.h` (`RefillDuration::evaluate`) : `Continue` /
  `AnomalyReset` (durée > ~50 min : capte un `_pumpStartMs` invalide) / `ForcedStop` (durée max atteinte).
  Anomalie prioritaire, comparaisons strictes conservées. Seuil paramétrable (testable). Pure → `g++`.
- `handleRefillMaxDurationStop` délègue la décision de timing ; l'évaluation efficacité/retry et les
  effets de bord (stopTankPump, email, sync, lock, reset chrono) restent dans l'appelant (parité).
- Suite native `test/test_refill_duration/` (9 cas : continue, arrêt forcé au seuil, anomalie, priorité
  anomalie, bornes strictes, seuil custom). Enregistrée CI + ini. Local 9/9.

⚠️ **Banc requis** : l'intégration coupe la pompe réellement ; seule la décision de timing est CI-testée.

#### C4 — Contrôleur refill : décision d'efficacité `RefillEfficiency` (retry / verrou)
Dernière décision de `handleRefillMaxDurationStop` extraite :
- Nouveau module `include/automatism/refill_efficiency.h` (`RefillEfficiency::evaluate`) : `NoEval`
  (niveau inconnu) / `Effective` (amélioration ≥ 1 mm → reset essais) / `Inefficient` (pas d'amélioration,
  essais restants) / `InefficientLock` (essais+1 atteint le max → verrou). Pure → `g++`.
- `handleRefillMaxDurationStop` délègue ; incrément/reset du compteur, verrou, email, sync restent dans
  l'appelant (parité ligne à ligne ; le verrou se déclenche au même incrément que l'ancien code).
- Suite native `test/test_refill_efficiency/` (NoEval, Effective, Inefficient, InefficientLock, bornes).
  Parité exhaustive vs ancien code en local (181/181). Enregistrée CI + ini.

⚠️ **Banc requis** : l'intégration verrouille/notifie la pompe réellement ; seule la décision est CI-testée.

#### C4 — Contrôleur refill : sécurité trop-plein `RefillOverfill` (Lock/Unlock)
- Nouveau module `include/automatism/refill_overfill.h` (`RefillOverfill::evaluate`) : `Lock` (aquarium
  trop plein, distance < limFlood, pas déjà verrouillé) / `Unlock` (niveau revenu OK, verrou trop-plein
  actif et plus en flood) / `None`. Pure → `g++`.
- `handleRefillAquariumOverfillSecurity` délègue ; verrou+motif, arrêt pompe, notif, reset flags email
  restent dans l'appelant (parité ligne à ligne).
- Suite native `test/test_refill_overfill/` (Lock/Unlock/None, déjà verrouillé, flood actif, bord strict).
  Parité exhaustive locale (173/173). Enregistrée CI + ini.

⚠️ **Banc requis** : l'intégration arrête/verrouille la pompe réellement ; seule la décision est CI-testée.

#### C4 — Contrôleur refill : récupération auto `RefillRecovery` (déblocage après inefficacité)
- Nouveau module `include/automatism/refill_recovery.h` (`RefillRecovery::shouldRecover`) : retente le
  déverrouillage si verrouillé + essais épuisés + debounce 30 s écoulé + réserve franchement OK
  (distance < seuil − marge). Pure → `g++`. Arithmétique non signée (gère wrap millis()).
- `handleRefillAutomaticRecovery` délègue la décision ; déverrouillage + reset essais/flags email + logs
  restent dans l'appelant. **Smell supprimé** : le `static lastRecoveryAttempt` devient un membre
  `_lastRecoveryAttemptMs` (comme `ReservoirLowSecurity`).
- Suite native `test/test_refill_recovery/`. Parité exhaustive vs ancien code en local (286/286). CI + ini.

⚠️ **Banc requis** : l'intégration déverrouille la pompe réellement ; seule la décision est CI-testée.

➡️ **Bilan refill (complet)** : **les 6 décisions** de la pompe de remplissage sont extraites et testées
nativement — réserve-basse (`ReservoirLowSecurity`), démarrage (`RefillStart`), durée/anomalie
(`RefillDuration`), efficacité/retry (`RefillEfficiency`), sécurité trop-plein (`RefillOverfill`),
récupération auto (`RefillRecovery`). `handleRefill*` ne contient plus que l'orchestration et les E/S
(pompe/email/sync), toute la logique de décision étant sortie et couverte par des tests purs.

---

## 9. Mise à jour 2026-06-15 — C4 inversion de dépendance `IMailer` (PR draft)

Étape **structurante** suivante du découpage : abstraire la **messagerie** derrière une interface, pour
pouvoir tester l'**orchestration** qui envoie des mails (alertes, fin de nourrissage, veille/réveil) sans
ESP_Mail_Client / réseau. Même patron que `IActuators`.

### Implémenté (CI-validable, behavior-preserving)
- Nouvelle interface **`include/imailer.h`** : surface MINIMALE (4 méthodes : `send`, `sendAlert`,
  `sendSleepMail`, `sendWakeMail`) consommée par le module `automatism` (vérifié par grep exhaustif).
  En-tête **léger** : `SensorReadings` en déclaration anticipée, aucun `config.h`/`ESP_Mail_Client` ;
  seul argument par défaut = littéral (`includeDetailedReport=false`) — tous les autres args sont fournis
  explicitement par les appelants (vérifié, y compris les wrappers `sendSleepMail`/`sendWakeMail`).
- `Mailer` **implémente** `IMailer` (héritage + `override`). Strictement additif : les autres consommateurs
  (`web_server`, `app_context`, OTA, global `mailer`) gardent le concret. Global `Mailer mailer;` (pas
  d'agrégation-init) → vptr sans risque.
- `Automatism::_mailer` (membre + ctor) et `AutomatismFeedingSchedule` (ctor + membre) retypés
  `Mailer&` → `IMailer&`. Conversion dérivée→base implicite à la construction (global `mailer`).
  Comportement inchangé (dispatch virtuel sur le même objet).
- **Test double `FakeMailer`** + suite native `test/test_imailer/` (dispatch polymorphe, arg par défaut,
  sleep/wake, destruction virtuelle). Validé local (`g++ -Wall -Wextra`, 7/7). Enregistrée CI + ini.

### ⚠️ Banc requis (chemins matériels)
Le **dispatch virtuel** sur ESP32 pour les envois mail (chemins alertes/veille) n'est pas prouvé par la
compilation. Aucun changement de logique — seulement le mécanisme d'appel. À confirmer au banc.

### Étape suivante (proposée)
Avec `IActuators` + `IMailer` (+ un futur `IClock`/`INetSender`), extraire l'**orchestration** restante
(`handleAlerts`, `finalizeFeedingIfNeeded`, blocage sleep) en contrôleurs testables nativement.

#### C4 — 1er ORCHESTRATEUR à effets de bord testé : `HeaterOrchestrator`
Concrétisation du chantier `IActuators`+`IMailer` : la régulation chauffage de `handleAlerts` (relais +
mail + blink) est extraite en orchestrateur **testable nativement** (≠ simple décision pure).
- Nouveau `include/automatism/heater_orchestrator.h` (`HeaterOrchestrator::run`) : décide via
  `HeaterControl` (pur) puis exécute via `IActuators`/`IMailer`. Met à jour `heaterPrevState` ; renvoie
  true si un mail a été envoyé → l'appelant déclenche le blink (seul effet non interfacé).
- `handleAlerts` délègue tout le bloc chauffage à `run()`. Dead code retiré (`formatTemperatureAlert`).
- Suite native `test/test_heater_orchestrator/` avec `FakeActuators` + `FakeMailer` : vérifie le **câblage
  complet** (bon relais + bon sujet de mail + blink suit le mail + bande morte/no-retrigger). Local 20/20.
- **C'est le premier bout d'orchestration (effets de bord) couvert par des tests** — la valeur cible du
  chantier. Patron réutilisable pour `finalizeFeedingIfNeeded`, alertes niveau, etc.

⚠️ **Banc requis** : l'intégration commute le relais chauffage réel ; seul le câblage logique est CI-testé.

#### C4 — Orchestrateurs alertes : `LevelAlertOrchestrator` + `FloodOrchestrator`
Sur le patron de `HeaterOrchestrator`, les alertes de `handleAlerts` sont extraites en orchestrateurs
testables nativement :
- `level_alert_orchestrator.h` (`run`) : alertes niveau aquarium/réserve (Raise/Clear via `IMailer`),
  générique (clearSubject=nullptr pour l'aquarium). Test `test_level_alert_orchestrator/` (16/16 local).
- `flood_orchestrator.h` (`run`) : trop-plein. Décision `FloodAlert` (pure) + envoi `IMailer` + markEmailSent ;
  renvoie un `Outcome` (EmailQueued/EmailFailed/ExitedFlood/None) → l'appelant applique NVS/blink/flag/logs.
  epoch + pompe-verrouillée passés en paramètres (pas besoin d'`IClock`). Message reproduit à l'identique
  (format historique + « / Pompe verrouillée »). Test `test_flood_orchestrator/` (6 cas, 16/16 local).
- `handleAlerts` ne contient plus de logique d'alerte inline : tout est délégué (niveau, flood, chauffage)
  à des orchestrateurs CI-testés. Dead code retiré (`formatDistanceAlert`, `formatTemperatureAlert`).

⚠️ **Banc requis** : ces orchestrateurs envoient des mails / commutent des relais réels ; seul le câblage
logique est CI-testé. **`handleAlerts` est désormais entièrement décomposé en unités testables.**

---

## 10. Mise à jour 2026-06-15 — C4 orchestrateur fin de nourrissage `FeedingFinalizeOrchestrator` (PR draft, **BENCH-REQUISE**)

Avant-dernière orchestration couplée matériel rendue testable nativement (cf. cible « 2 dernières
orchestrations »). `Automatism::finalizeFeedingIfNeeded(nowMs)` poussait, à la vraie fin d'un cycle de
nourrissage, une **sync distante** (`sendFullUpdate`) avec des paires `extraPairs` différentes selon le
mode (auto vs manuel) puis appelait `GPIOParser::syncFeedEdgeStateAfterLocalPost(...)` + des logs. Couplages :
`WiFi.status()`, `readSensors()`, `sendFullUpdate`, `GPIOParser`, `Serial`.

### Approche SCOPÉE & PEU INVASIVE (1 seule petite interface + params + Outcome)
- **Nouvelle interface MINIMALE `include/istatus_publisher.h`** (1 méthode) :
  `bool publish(const SensorReadings&, const char* extraPairs)`. En-tête LÉGER (`SensorReadings` en
  forward-decl, pas de config.h/Arduino). `Automatism` l'implémente via un **mince adaptateur** nested
  (`StatusPublisherAdapter{*this}`) qui forwarde vers son `sendFullUpdate(...)` existant (catégorie POST
  par défaut = `Periodic`, parité avec l'appel historique). Strictement additif (aucun autre consommateur
  touché).
- **`WiFi.status()` non abstrait** : l'appelant calcule `connected = WiFi.status()==WL_CONNECTED &&
  _config.isRemoteSendEnabled()` et le passe en **paramètre** ; `readSensors()` n'est appelé **que** si
  `connected` (parité : aucune lecture capteur hors-ligne).
- **`GPIOParser` non abstrait** : l'orchestrateur renvoie un `Outcome`
  (`Offline`/`AutoSynced`/`AutoSyncFailed`/`ManualSynced`/`ManualSyncFailed`) ; l'appelant appelle
  `syncFeedEdgeStateAfterLocalPost(true,true)` (auto) ou `(false,false)` (manuel) et imprime le **message
  Serial historique** selon l'Outcome (parité ligne à ligne, chaînes reproduites à l'identique).
- **Gate + reset des membres d'état** (flags `_manualFeedingActive`, `_currentFeedingPhase`,
  `_feedingPhaseEnd`, `_currentFeedingType`) restent dans l'appelant (offline-first inchangé).

### Tests natifs (`test/test_feeding_finalize/`, 7 cas — local 7/7, `-Wall -Wextra`)
`FakeStatusPublisher` (enregistre les `extraPairs` reçus) : hors-ligne auto/manuel (aucune publication),
en-ligne auto/manuel × sync OK/échec (Outcome + **parité exacte** des paires
`bouffePetits=1&108=1&bouffeGros=1&109=1` / `…=0`), + assertion des constantes. Enregistrée CI + ini.

### ⚠️ Banc requis (chemin nourrissage / sync réseau)
L'intégration pousse un **POST réel** (évènement nourrissage côté BDD serveur) + appelle GPIOParser sur le
matériel ; seul le câblage logique (quelles paires partent, dans quel cas) est CI-testé. À valider sur banc :
enregistrement correct du nourrissage auto vs manuel, absence de faux front 0→1 au poll GET suivant.

### Reste (dernière cible)
`AutomatismSleep::handleBlockingConditions(...)` — machine à état veille (timestamps membres). Extraction de
la **décision de blocage** à évaluer prudemment (chemin veille = risque batterie/réveil) ; documenter et
s'arrêter si le couplage reste trop fort pour une parité sûre.

---

## 11. Mise à jour 2026-06-15 — C4 décision de blocage veille `SleepBlocking::decide` (PR draft, **BENCH-REQUISE**)

DERNIÈRE des « 2 dernières orchestrations couplées matériel » : la décision de blocage de la veille de
`AutomatismSleep::handleBlockingConditions(...)`. Chemin VEILLE critique (un mauvais blocage = batterie
vidée ou réveils manqués), traité avec prudence : on N'extrait QUE la **décision pure** (« faut-il bloquer
la veille, et pour quelle raison »), en laissant logs/throttle/mutations à l'appelant. **Couplage jugé
extractible proprement** (≠ « s'arrêter ») car le seul état influençant la décision est `_wsBlockStartMs`.

### Approche PURE & peu invasive (aucune nouvelle interface — décision pure)
- **Nouveau module pur `include/automatism/sleep_blocking.h`** (`SleepBlocking::decide`) : prend l'état
  explicite (`State{wsBlockStartMs}`) + les entrées (`Inputs` : wsClients, forceWakeFromWeb, lastWebActivityMs,
  forceWakeUp, tankPumpRunning, feedingInProgress, countdownEnd, **nowMs** et **webActivityTimeoutMs** injectés)
  et renvoie un `Result` : la `Reason` terminale (Allow / WsClients / WebActivity / ForceWakeUp / TankPump /
  Feeding / CountdownLong / CountdownShort) + les transitions « fall-through » (`wsTimedOut`, `webExpired`) +
  les effets à appliquer (`refreshLastWake`, `wsElapsedMs`, `remainingCountdownSec`). Aucune dépendance
  Arduino/config → `g++`. `uint32_t` partout → wrap-around millis() **32 bits identique** ESP32 / natif.
- **Temps non abstrait** : `nowMs = millis()` passé en paramètre (pas d'`IClock`). `WiFi`/capteurs : néant ici.
- **Signature de `handleBlockingConditions` inchangée** (appelant `handleAutoSleep` non touché) : seul le
  **corps** délègue à `decide`, puis applique `_wsBlockStartMs`, les **logs Serial** (chaînes historiques
  reproduites à l'identique), l'**anti-spam/throttle** (`_lastWsLogMs`/`_lastWebLogMs`/`_lastForceWakeLogMs`,
  cosmétique, jamais décisionnel → laissé à l'appelant) et les mutations `forceWakeFromWeb`/`lastWakeMs`.
- Smell retiré : `(void)acts` (param inutilisé, déjà le cas avant).

### Tests natifs (`test/test_sleep_blocking/`, 23 cas — local 23/23, `-Wall -Wextra`)
Chaque raison, l'**ordre de priorité** des 6 conditions, les **bornes strictes** (timeout WS 300 s, seuil
décompte 300 s, expiration web), les transitions (WS timeout / web expiré), et le **wrap-around millis()**.
Enregistré CI + `platformio-native.ini`. **Parité prouvée par harnais brute-force** : ancien corps inline vs
`decide`+appelant refactoré sur **2 211 840 combinaisons → 0 divergence** (retour, logs séquencés, 4 timestamps
de throttle, mutations `lastWakeMs`/`forceWakeFromWeb`, cas wrap inclus).

### ⚠️ VALIDATION BANC REQUISE (chemin veille/réveil)
La *décision* est CI-testée + prouvée à parité, mais l'intégration veille réelle (entrée/sortie de sleep,
timing batterie, réveil) n'est pas prouvée par compilation. À valider sur banc : blocage WS (clients réels +
timeout 5 min), activité web, forceWakeUp GPIO, pompe/nourrissage, décomptes long/court, et **non-régression
de l'autonomie** (pas de veille manquée ni de batterie vidée).

➡️ **Bilan C4** : les **2 dernières orchestrations couplées matériel** (`finalizeFeedingIfNeeded` §10,
`handleBlockingConditions` §11) sont désormais déléguées à des unités testées nativement. `handleAlerts`,
`handleRefill*`, veille/réveil (snapshot + blocage), fin de nourrissage : la logique de décision de la
god-class `Automatism` est sortie et couverte par des tests purs (effets matériels restant bench-gated).

---

## 12. Mise à jour 2026-06-16 — 3 chantiers parallèles (décision horloge + durcissement concurrence)

Trois PR draft indépendantes (fichiers disjoints → merge sans conflit), développées en parallèle.
**Toutes BENCH-REQUISES** pour la preuve runtime (CI = compilation + tests natifs uniquement).

### 12.a — `ClockDecision` : validation NTP + dérive RTC (extraction pure, testée) — PR `claude/ffp5cs-c4-clock-decision`
Suite directe du chantier C4 (hors `Automatism`, dans `power.cpp`). Extraction de la logique numérique de
`syncTimeFromNTP` en module **pur** `include/clock_decision.h` (namespace `ClockDecision`, deps `<cmath>`/
`<ctime>` + `epoch_util.h` ; **réutilise** `EpochUtil::epochAbsDiff`) :
- `isPlausibleYear` (≥ 2024), `isNtpEpochPlausible` (`epochChangedEnough || nearCompile`),
  `computeDriftPpm` (formule PPM + bornage `fmax/fmin`), `computeDriftSeconds`.
- `power.cpp` délègue la **décision** ; tout l'I/O (RTC/NVS/`settimeofday`/`getLocalTime`/Serial) + les
  messages `LOG_*` verbatim restent dans l'appelant. `applyDriftCorrection`/`getSleptTime` laissés intacts
  (intriqués I/O — pas une petite étape propre).
- Test natif `test/test_clock_decision/` (18 cas) + **2 boucles brute-force** (~250k combos PPM, ~3500 NTP)
  vs transcription de l'inline d'origine → 0 divergence. Enregistré CI + ini. ⚠️ Banc : sync NTP réelle
  (accepte/loggue même PPM/rejette stale ; pas de divergence float ESP32 vs hôte).

### 12.b — Concurrence : états partagés multi-tâches → `std::atomic` — PR `claude/ffp5cs-concurrency-atomics`
Suite du §4 (atomics anti-course). 3 variables **vérifiées réellement multi-tâches** (chaînes d'appel
tracées) converties, strictement préservateur de comportement :
- `s_bootConfigFetchDone` (`app_tasks.cpp`) : netTask écrit / loopTask lit (`volatile bool`→`atomic<bool>`).
- `s_wakeProtectionStartMs` (`app_tasks.cpp`) : autoTask écrit / otaTask+loopTask lisent ; `isInWakeProtectionWindow`
  lit désormais **un seul snapshot** (corrige aussi un TOCTOU test/soustraction).
- `s_nextSeq` (`sd_logger.cpp`, BOARD_S3) : RMW `seq++` atteint d'autoTask **et** async_tcp/web → `atomic<uint32_t>`.
- Style calqué sur l'existant (`s_heartbeatDroppedCount`). `diagnostics.h` non touché (BACKLOG §4 : casse la
  copiabilité de `DiagnosticStats`). ⚠️ Banc : preuve runtime sur chemins réels boot/réveil/SD.

### 12.c — Concurrence : mutex dédié `s_remoteJsonCache` — PR `claude/ffp5cs-concurrency-remote-cache-mutex`
**Vraie course** confirmée (chaînes tracées) sur le cache RAM `remote_vars` (`config.cpp`, 2048 o + flag),
non documentée auparavant — même classe que `s_lastFetchedJson` (§1) :
- WRITE : async_tcp (`/dbvars/update`, `toggleEmailNotifications`) **et** autoTask (`processDeferredRemoteVarsSave`).
- READ : netTask (`loadFromNVSFallback`/boot), autoTask (`fetchRemoteState`/restore), async_tcp (`/dbvars`).
- Aucune sérialisation préexistante (le web server est **ESPAsyncWebServer** → handlers sur tâche async_tcp,
  pas webTask). `strncpy`/`strcmp` déchirables → JSON malformé → parse KO/OOB.
- Correctif : `s_remoteJsonCacheMutex` (lazy-init, motif `s_lastFetchedJsonMutex`, timeout 1000 ms). Verrou
  **FEUILLE** : sections critiques = cache RAM uniquement ; **appels NVS hors mutex** (pas d'inversion).
  Dégradation gracieuse au timeout : lecture → retombe sur NVS (source durable). Diff `config.cpp` seul.
  ⚠️ Banc (bloquant) : toggle WiFi + polling + trafic `/dbvars` concurrent → 0 LoadProhibited, 0 stalls.
